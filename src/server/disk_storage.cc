#include "transfer_queue/server/disk_storage.h"
#include <seastar/core/fstream.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/seastar.hh>
#include <seastar/util/log.hh>
#include <seastar/core/byteorder.hh>
#include <cstring>
#include <cstdlib>
#include <atomic>

namespace transfer_queue {

namespace {
    void write_u32_le(char* buf, uint32_t val) {
        val = seastar::cpu_to_le(val);
        std::memcpy(buf, &val, sizeof(val));
    }
    uint32_t read_u32_le(const char* buf) {
        uint32_t val;
        std::memcpy(&val, buf, sizeof(val));
        return seastar::le_to_cpu(val);
    }
}

struct free_deleter {
    void operator()(void* p) { std::free(p); }
};

static seastar::logger disk_log("disk_storage");

DiskStorage::DiskStorage(const std::string& bdev_name)
    : bdev_name_(bdev_name), initialized_(false) {}

DiskStorage::~DiskStorage() {}

seastar::future<> DiskStorage::init() {
    // Treat bdev_name_ as directory.
    return seastar::file_exists(bdev_name_).then([this](bool exists) {
        if (!exists) {
            return seastar::make_directory(bdev_name_);
        }
        return seastar::make_ready_future<>();
    }).then([this] {
        return seastar::file_stat(bdev_name_).then([this](seastar::stat_data stat) {
            if (!S_ISDIR(stat.mode)) {
                return seastar::make_exception_future<>(std::runtime_error("Storage path matches an existing file, not directory"));
            }
            initialized_ = true;
            disk_log.info("Initialized storage directory: {}", bdev_name_);
            return seastar::make_ready_future<>();
        });
    });
}

seastar::future<> DiskStorage::shutdown() {
    initialized_ = false;
    return seastar::make_ready_future<>();
}

seastar::future<uint64_t> DiskStorage::spill(const transferqueue::TrajectoryGroup& group) {
    if (!initialized_) return seastar::make_exception_future<uint64_t>(std::runtime_error("Not initialized"));

    std::string data;
    if (!group.SerializeToString(&data)) {
        return seastar::make_exception_future<uint64_t>(
            std::runtime_error("Failed to serialize group"));
    }

    // Generate ID: timestamp + counter to ensure uniqueness.
    static std::atomic<uint64_t> counter{0};
    uint64_t id = std::chrono::steady_clock::now().time_since_epoch().count() + (counter++);
    
    std::string filename = bdev_name_ + "/" + std::to_string(id);
    uint32_t data_len = data.size();

    disk_log.info("Spilling to {}", filename);
    return seastar::open_file_dma(filename, 
        seastar::open_flags::rw | seastar::open_flags::create | seastar::open_flags::truncate)
        .then([data = std::move(data), data_len](seastar::file f) mutable {
             
             size_t total_size = 4 + data_len;
             size_t align = 4096;
             size_t aligned_size = (total_size + align - 1) / align * align;
             
             void* raw_buf = std::aligned_alloc(align, aligned_size);
             if (!raw_buf) throw std::bad_alloc();
             
             std::unique_ptr<char[], free_deleter> buffer(static_cast<char*>(raw_buf));
             
             // Write header
             write_u32_le(buffer.get(), data_len);
             // Write data
             std::memcpy(buffer.get() + 4, data.c_str(), data_len);
             // Zero padding
             std::memset(buffer.get() + total_size, 0, aligned_size - total_size);

             return f.dma_write(0, buffer.get(), aligned_size)
                .then([f, buffer = std::move(buffer)](size_t written) mutable {
                    return f.close();
                });
        }).then([id] {
            disk_log.info("Spilled id {}", id);
            return seastar::make_ready_future<uint64_t>(id);
        });
}

seastar::future<std::vector<uint64_t>> DiskStorage::spill_batch(
    std::vector<transferqueue::TrajectoryGroup> groups) {
    
    struct BatchContext {
        std::vector<uint64_t> offsets;
        std::vector<transferqueue::TrajectoryGroup> groups;
    };
    auto ctx = seastar::make_lw_shared<BatchContext>();
    ctx->groups = std::move(groups);
    ctx->offsets.reserve(ctx->groups.size());
    
    return seastar::do_for_each(ctx->groups, [this, ctx](const auto& group) {
        return spill(group).then([ctx](uint64_t offset) {
            ctx->offsets.push_back(offset);
        });
    }).then([ctx] {
        return seastar::make_ready_future<std::vector<uint64_t>>(std::move(ctx->offsets));
    });
}

seastar::future<std::unique_ptr<transferqueue::TrajectoryGroup>> DiskStorage::load(uint64_t offset) {
    if (!initialized_) return seastar::make_exception_future<std::unique_ptr<transferqueue::TrajectoryGroup>>(std::runtime_error("Not initialized"));

    std::string filename = bdev_name_ + "/" + std::to_string(offset);
    
    return seastar::open_file_dma(filename, seastar::open_flags::ro)
        .then([](seastar::file f) {
             struct LoadContext {
                 seastar::file f;
                 seastar::input_stream<char> in;
             };
             // tricky: make_file_input_stream takes file by value.
             // We want to keep file alive in context.
             // We can copy f into ctx first.
             auto ctx = seastar::make_lw_shared<LoadContext>();
             ctx->f = f;
             ctx->in = seastar::make_file_input_stream(f);
             
             // Now f is safe to consume? 
             // Logic:
             return ctx->in.read_exactly(4)
                .then([ctx](seastar::temporary_buffer<char> header_buf) {
                    if (header_buf.size() < 4) {
                        return seastar::make_exception_future<std::unique_ptr<transferqueue::TrajectoryGroup>>(
                            std::runtime_error("Unexpected EOF reading header"));
                    }
                    uint32_t len = read_u32_le(header_buf.get());
                    if (len > 100 * 1024 * 1024) { 
                         return seastar::make_exception_future<std::unique_ptr<transferqueue::TrajectoryGroup>>(
                            std::runtime_error("Group size too large"));
                    }
                    return ctx->in.read_exactly(len)
                        .then([ctx, len](seastar::temporary_buffer<char> body_buf) {
                            if (body_buf.size() < len) {
                                return seastar::make_exception_future<std::unique_ptr<transferqueue::TrajectoryGroup>>(
                                    std::runtime_error("Unexpected EOF reading body"));
                            }
                            auto group = std::make_unique<transferqueue::TrajectoryGroup>();
                            if (!group->ParseFromArray(body_buf.get(), len)) {
                                 return seastar::make_exception_future<std::unique_ptr<transferqueue::TrajectoryGroup>>(
                                    std::runtime_error("Failed to parse group"));
                            }
                            return seastar::make_ready_future<std::unique_ptr<transferqueue::TrajectoryGroup>>(std::move(group));
                        });
                })
                .finally([ctx] {
                    return ctx->in.close().finally([ctx]{ return ctx->f.close(); });
                });
        });
}

seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
DiskStorage::load_batch(const std::vector<uint64_t>& offsets) {
     struct LoadBatchContext {
         std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>> results;
     };
     auto ctx = seastar::make_lw_shared<LoadBatchContext>();
     ctx->results.reserve(offsets.size());
     
     return seastar::do_for_each(offsets, [this, ctx](uint64_t offset) {
        return load(offset).then([ctx](auto group) {
            ctx->results.push_back(std::move(group));
        });
     }).then([ctx] {
         return seastar::make_ready_future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>(std::move(ctx->results));
     });
}

seastar::future<> DiskStorage::remove(uint64_t offset) {
    if (!initialized_) return seastar::make_ready_future<>();
    std::string filename = bdev_name_ + "/" + std::to_string(offset);
    return seastar::remove_file(filename);
}

seastar::future<int64_t> DiskStorage::get_disk_usage() const {
    // Walk directory and sum size? 
    // For now, return 0 as recursive directory walk in Seastar is manual.
    return seastar::make_ready_future<int64_t>(0);
}

bool DiskStorage::is_initialized() const {
    return initialized_;
}

} // namespace transfer_queue
