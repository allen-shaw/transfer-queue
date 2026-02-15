#include "transfer_queue/server/disk_storage.h"
#include <seastar/core/fstream.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/byteorder.hh>
#include <seastar/util/log.hh>
#include <cstring>
#include <sys/stat.h>

namespace transfer_queue {

static seastar::logger disk_log("disk_storage");

// Helper to write LE uint32
static void write_u32_le(char* data, uint32_t val) {
    val = seastar::cpu_to_le(val);
    std::memcpy(data, &val, sizeof(val));
}

static uint32_t read_u32_le(const char* data) {
    uint32_t val;
    std::memcpy(&val, data, sizeof(val));
    return seastar::le_to_cpu(val);
}

DiskStorage::DiskStorage(const std::string& bdev_name)
    : bdev_name_(bdev_name) {}

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
            if (stat.type != seastar::directory_entry_type::directory) {
                return seastar::make_exception_future<>(
                    std::runtime_error("Storage path is not a directory: " + bdev_name_));
            }
            initialized_ = true;
            disk_log.info("Initialized storage directory: {}", bdev_name_);
            return seastar::make_ready_future<>();
        });
    });
}

seastar::future<> DiskStorage::shutdown() {
    initialized_ = false;
    // Nothing to close as we open files per op.
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

    return seastar::open_file_dma(filename, 
        seastar::open_flags::rw | seastar::open_flags::create | seastar::open_flags::truncate)
        .then([data = std::move(data), data_len](seastar::file f) mutable {
             // Create output stream
             return seastar::make_file_output_stream(f).then([f, data = std::move(data), data_len](seastar::output_stream<char> out) mutable {
                 // Create header as sstring or buffer
                 seastar::sstring header(seastar::sstring::initialized_later(), 4);
                 write_u32_le(header.data(), data_len);

                 return seastar::do_with(std::move(out), std::move(f), std::move(data), std::move(header), 
                    [](auto& out, auto& f, auto& data, auto& header) {
                     return out.write(header.data(), 4)
                        .then([&out, &data] {
                            return out.write(data.c_str(), data.size());
                        })
                        .then([&out] {
                            return out.flush();
                        })
                        .then([&out] {
                            return out.close();
                        })
                        .finally([&f] {
                            return f.close();
                        });
                 });
             });
        }).then([id] {
            return seastar::make_ready_future<uint64_t>(id);
        });
}

seastar::future<std::vector<uint64_t>> DiskStorage::spill_batch(
    const std::vector<transferqueue::TrajectoryGroup>& groups) {
    
    return seastar::do_with(std::vector<uint64_t>(), [this, &groups](std::vector<uint64_t>& offsets) {
        offsets.reserve(groups.size());
        return seastar::do_for_each(groups, [this, &offsets](const auto& group) {
            return spill(group).then([&offsets](uint64_t offset) {
                offsets.push_back(offset);
            });
        }).then([&offsets] {
            return seastar::make_ready_future<std::vector<uint64_t>>(std::move(offsets));
        });
    });
}

seastar::future<std::unique_ptr<transferqueue::TrajectoryGroup>> DiskStorage::load(uint64_t offset) {
    if (!initialized_) return seastar::make_exception_future<std::unique_ptr<transferqueue::TrajectoryGroup>>(std::runtime_error("Not initialized"));

    std::string filename = bdev_name_ + "/" + std::to_string(offset);
    
    return seastar::open_file_dma(filename, seastar::open_flags::ro)
        .then([](seastar::file f) {
             // Use input stream
             auto in = seastar::make_file_input_stream(f);
             return seastar::do_with(std::move(in), std::move(f), [](auto& in, auto& f) {
                 return in.read_exactly(4)
                    .then([&in](seastar::temporary_buffer<char> header_buf) {
                        if (header_buf.size() < 4) {
                            return seastar::make_exception_future<std::unique_ptr<transferqueue::TrajectoryGroup>>(
                                std::runtime_error("Unexpected EOF reading header"));
                        }
                        uint32_t len = read_u32_le(header_buf.get());
                        if (len > 100 * 1024 * 1024) { 
                             return seastar::make_exception_future<std::unique_ptr<transferqueue::TrajectoryGroup>>(
                                std::runtime_error("Group size too large"));
                        }
                        return in.read_exactly(len)
                            .then([len](seastar::temporary_buffer<char> body_buf) {
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
                    .finally([&in, &f] {
                        return in.close().finally([&f]{ return f.close(); });
                    });
             });
        });
}

seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
DiskStorage::load_batch(const std::vector<uint64_t>& offsets) {
     return seastar::do_with(std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>(), 
        [this, &offsets](std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>& results) {
        results.reserve(offsets.size());
        return seastar::do_for_each(offsets, [this, &results](uint64_t offset) {
            return load(offset).then([&results](auto group) {
                results.push_back(std::move(group));
            });
        }).then([&results] {
            return seastar::make_ready_future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>(std::move(results));
        });
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
