#include "transfer_queue/server/buffer_manager.h"

#include <seastar/core/smp.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/when_all.hh>
#include <functional>
#include <unordered_map>

namespace transfer_queue {

BufferManager::BufferManager(const TransferQueueConfig& config)
    : config_(config) {
}

BufferManager::~BufferManager() = default;

seastar::future<> BufferManager::start() {
    return shards_.start(std::ref(config_)).then([this] {
        return shards_.invoke_on_all([](BufferShard& shard) {
            return shard.start();
        });
    });
}

seastar::future<> BufferManager::stop() {
    return shards_.stop();
}

// ============================================================================
// 写入
// ============================================================================

seastar::future<bool> BufferManager::write(transferqueue::Trajectory trajectory) {
    auto sid = shard_for(trajectory.instance_id());
    return shards_.invoke_on(sid,
        [t = std::move(trajectory)](BufferShard& shard) mutable {
            return shard.write(std::move(t));
        });
}

seastar::future<int32_t> BufferManager::batch_write(std::vector<transferqueue::Trajectory> trajectories) {
    // 1. 按 instance_id 分组，确定目标 shard
    std::unordered_map<unsigned, std::vector<transferqueue::Trajectory>> by_shard;
    for (auto& t : trajectories) {
        auto sid = shard_for(t.instance_id());
        by_shard[sid].push_back(std::move(t));
    }

    // 2. 并行提交到各 shard
    std::vector<seastar::future<int32_t>> futs;
    futs.reserve(by_shard.size());
    for (auto& [sid, batch] : by_shard) {
        futs.push_back(shards_.invoke_on(sid,
            [b = std::move(batch)](BufferShard& shard) mutable {
                return shard.batch_write(std::move(b));
            }));
    }

    // 3. 聚合写入数
    return seastar::when_all_succeed(futs.begin(), futs.end())
        .then([](std::vector<int32_t> results) {
            int32_t total = 0;
            for (auto n : results) {
                total += n;
            }
            return total;
        });
}

// ============================================================================
// 读取
// ============================================================================

seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
BufferManager::read_ready_groups(int32_t max_groups) {
    using ResultVec = std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>;

    // max_groups 传给每个 shard（per-shard 限制），然后 manager 层截断
    // 注意：因读取是消费式的，截断后丢失的组无法恢复
    // 如需精确控制总量，需串行查询
    return shards_.map([max_groups](BufferShard& shard) {
        return shard.read_ready_groups(max_groups);
    }).then([max_groups](std::vector<ResultVec> per_shard_results) {
        ResultVec merged;
        for (auto& shard_vec : per_shard_results) {
            for (auto& group : shard_vec) {
                merged.push_back(std::move(group));
            }
        }
        if (max_groups > 0 && static_cast<int32_t>(merged.size()) > max_groups) {
            merged.resize(max_groups);
        }
        return merged;
    });
}

seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
BufferManager::read_ready_groups_blocking(int32_t max_groups, int32_t timeout_ms) {
    using ResultVec = std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    return seastar::do_with(
        deadline, max_groups,
        [this](auto& deadline, auto& max_groups) -> seastar::future<ResultVec> {
            return seastar::repeat([this, &deadline, &max_groups]() -> seastar::future<seastar::stop_iteration> {
                return has_ready_groups().then([this, &deadline, &max_groups](bool ready) {
                    if (ready) {
                        return seastar::make_ready_future<seastar::stop_iteration>(
                            seastar::stop_iteration::yes);
                    }
                    if (std::chrono::steady_clock::now() >= deadline) {
                        return seastar::make_ready_future<seastar::stop_iteration>(
                            seastar::stop_iteration::yes);
                    }
                    return seastar::sleep(std::chrono::milliseconds(10)).then([] {
                        return seastar::stop_iteration::no;
                    });
                });
            }).then([this, &max_groups]() {
                return read_ready_groups(max_groups);
            });
        });
}

seastar::future<bool> BufferManager::has_ready_groups() const {
    return shards_.map_reduce0(
        [](const BufferShard& shard) {
            return shard.has_ready_groups();
        },
        false,
        std::logical_or<bool>());
}

// ============================================================================
// 管理
// ============================================================================

seastar::future<> BufferManager::delete_instance(const std::string& instance_id) {
    auto sid = shard_for(instance_id);
    return shards_.invoke_on(sid,
        [instance_id](BufferShard& shard) {
            return shard.delete_instance(instance_id);
        });
}

seastar::future<> BufferManager::reset() {
    return shards_.invoke_on_all([](BufferShard& shard) {
        return shard.reset();
    });
}

seastar::future<std::unique_ptr<transferqueue::BufferStatus>> BufferManager::get_status() const {
    return shards_.map_reduce0(
        [](const BufferShard& shard) {
            return shard.get_status();
        },
        std::make_unique<transferqueue::BufferStatus>(),
        [](std::unique_ptr<transferqueue::BufferStatus> acc,
           std::unique_ptr<transferqueue::BufferStatus> shard_status) {
            acc->set_total_trajectories(
                acc->total_trajectories() + shard_status->total_trajectories());
            acc->set_pending_groups(
                acc->pending_groups() + shard_status->pending_groups());
            acc->set_incomplete_groups(
                acc->incomplete_groups() + shard_status->incomplete_groups());
            acc->set_total_consumed(
                acc->total_consumed() + shard_status->total_consumed());
            acc->set_memory_usage_bytes(
                acc->memory_usage_bytes() + shard_status->memory_usage_bytes());
            acc->set_disk_usage_bytes(
                acc->disk_usage_bytes() + shard_status->disk_usage_bytes());
            return acc;
        });
}

seastar::future<> BufferManager::update_config(const transferqueue::ConfigRequest& config_req) {
    // 更新本地 config_
    if (config_req.group_size() > 0) {
        config_.group_size = config_req.group_size();
    }
    if (!config_req.task_type().empty()) {
        config_.task_type = config_req.task_type();
    }
    if (config_req.max_memory_bytes() > 0) {
        config_.max_memory_bytes = config_req.max_memory_bytes();
    }
    if (config_req.spill_to_disk_threshold() > 0) {
        config_.spill_to_disk_threshold = config_req.spill_to_disk_threshold();
    }
    config_.uid_dedup = config_req.uid_dedup();
    if (config_req.group_timeout_seconds() > 0) {
        config_.group_timeout_seconds = config_req.group_timeout_seconds();
    }

    // 广播到所有 shard
    return shards_.invoke_on_all([this](BufferShard& shard) {
        shard.update_config(config_);
        return seastar::make_ready_future<>();
    });
}

// ============================================================================
// 私有方法
// ============================================================================

unsigned BufferManager::shard_for(const std::string& instance_id) const {
    return std::hash<std::string>{}(instance_id) % seastar::smp::count;
}

} // namespace transfer_queue
