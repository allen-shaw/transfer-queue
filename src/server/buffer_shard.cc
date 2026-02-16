#include "transfer_queue/server/buffer_shard.h"

#include <chrono>

namespace transfer_queue {

// ============================================================================
// 构造 / 析构
// ============================================================================

BufferShard::BufferShard(const TransferQueueConfig& config)
    : config_(&config)
    , dedup_filter_(100000, 0.01)
    , disk_storage_(config.spdk_bdev_name + "/shard_" + std::to_string(seastar::this_shard_id())) {
}

seastar::future<> BufferShard::start() {
    return disk_storage_.init();
}

seastar::future<> BufferShard::stop() {
    return disk_storage_.shutdown();
}

BufferShard::~BufferShard() = default;

// ============================================================================
// 写入
// ============================================================================

seastar::future<bool> BufferShard::write(transferqueue::Trajectory trajectory) {
    // 1. UID 去重检查
    if (config_->uid_dedup) {
        if (!dedup_filter_.insert(trajectory.uid())) {
            return seastar::make_ready_future<bool>(false);  // 重复，去重
        }
    }

    // 2. 按 instance_id 查找/创建 group
    const std::string& instance_id = trajectory.instance_id();
    auto& entry = groups_[instance_id];

    // 首次创建 group：初始化元数据
    // 注意：如果 entry 已经在磁盘上，说明以前满了被 spill 了。
    // 这里我们假设业务逻辑上，如同一个 instance_id 的 group 满了（is_complete），
    // 就不应该再往里写了，或者应该创建新的 group?
    // 目前逻辑是：如果 incomplete，继续写。如果 complete，这里其实会有问题。
    // 但根据 `batch_write` 和上层逻辑，这里主要处理 append。
    
    if (entry.is_on_disk()) {
        // 已经在磁盘上，且我们要写新数据？
        // 简单处理：如果已经在磁盘上，通常意味着它已经 complete 了。
        // 如果业务允许 append 到已 complete 的 group... 目前假设不允许，或者忽略。
        // 只要 group 还在 groups_ map 里，就说明还没被消费。
        // 如果在磁盘上，我们无法 append（除非读出来-写进去-再spill，代价太大）。
        // 暂时策略：Log warning 并丢弃? 或者 Crash?
        // 假设：Complete 的 group 不会再收到数据。
        return seastar::make_ready_future<bool>(true); 
    }

    if (!entry.group_ptr) {
        entry.group_ptr = std::make_unique<transferqueue::TrajectoryGroup>();
        entry.group_ptr->set_instance_id(instance_id);
        entry.group_ptr->set_group_size(config_->group_size);
        entry.group_ptr->set_is_complete(false);
        entry.created_time = std::chrono::steady_clock::now();
        // group_created_times_ 移除，统归 entry 管理
    }

    // 3. 添加 trajectory 到 group
    *entry.group_ptr->add_trajectories() = std::move(trajectory);
    ++total_trajectories_;

    // 4. 检查 group 是否已满 → 标记 ready
    if (entry.group_ptr->trajectories_size() >= entry.group_ptr->group_size()) {
        entry.group_ptr->set_is_complete(true);
    }
    
    // 5. 检查内存并尝试 Spill
    // 这是一个异步操作，但 write 接口期望快速返回。
    // 我们可以 detach spill 任务? 或者 chain it.
    // 为保证一致性，最好等待 spill 检查（至少是内存检查）完成。
    return try_spill_groups().then([](size_t) {
        return true;
    });
}

seastar::future<int32_t> BufferShard::batch_write(std::vector<transferqueue::Trajectory> trajectories) {
    int32_t written = 0;
    for (auto& traj : trajectories) {
        // write() 返回 make_ready_future，可以安全 .get()
        if (write(std::move(traj)).get()) {
            ++written;
        }
    }
    return seastar::make_ready_future<int32_t>(written);
}

// ============================================================================
// 读取
// ============================================================================

seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
BufferShard::read_ready_groups(int32_t max_groups) {
    return seastar::do_with(
        std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>(),
        std::vector<uint64_t>(), // offsets_to_load
        std::vector<std::string>(), // ids_to_remove
        [this, max_groups](auto& result, auto& offsets_to_load, auto& ids_to_remove) {
            
            // 1. 扫描 groups_，区分内存中和磁盘上的 ready groups
            for (auto& [instance_id, entry] : groups_) {
                if (!entry.is_complete()) {
                    continue;
                }

                if (max_groups > 0 && 
                    static_cast<int32_t>(result.size() + offsets_to_load.size()) >= max_groups) {
                    break;
                }

                ids_to_remove.push_back(instance_id);

                if (entry.is_on_disk()) {
                    offsets_to_load.push_back(entry.disk_offset);
                    // 统计修正
                    disk_usage_bytes_ -= entry.cached_bytes;
                } else {
                    total_consumed_ += entry.group_ptr->trajectories_size();
                    result.push_back(std::move(entry.group_ptr));
                }
            }
            
            // 2. 批量加载磁盘上的 groups
            return disk_storage_.load_batch(offsets_to_load)
                .then([this, &result, &ids_to_remove, &offsets_to_load](std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>> loaded_groups) {
                    // 合并结果
                    for (auto& g : loaded_groups) {
                        if (g) {
                            total_consumed_ += g->trajectories_size();
                            result.push_back(std::move(g));
                        }
                    }

                    // 3. 将已加载的磁盘文件标记删除
                    // 注意：这里我们只是异步发起删除，不等待结果，以免阻塞 read 路径太久
                    // 也可以选择等待。鉴于 remove 开销较小，do_for_each 等待也行。
                    // 为了保证可靠性，最好等待。
                    return seastar::do_for_each(offsets_to_load, [this](uint64_t offset) {
                        return disk_storage_.remove(offset);
                    });
                })
                .then([this, &ids_to_remove, &result] {
                    // 4. 从 map 中移除
                    for (const auto& id : ids_to_remove) {
                        groups_.erase(id);
                    }
                    return seastar::make_ready_future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>(
                        std::move(result));
                });
        }
    );
}

seastar::future<bool> BufferShard::has_ready_groups() const {
    for (const auto& [_, entry] : groups_) {
        if (entry.is_complete()) {
            return seastar::make_ready_future<bool>(true);
        }
    }
    return seastar::make_ready_future<bool>(false);
}

// ============================================================================
// 管理
// ============================================================================

seastar::future<> BufferShard::delete_instance(const std::string& instance_id) {
    auto it = groups_.find(instance_id);
    if (it != groups_.end()) {
        if (it->second.is_on_disk()) {
            disk_usage_bytes_ -= it->second.cached_bytes;
            // 异步删除文件，不等待
            (void)disk_storage_.remove(it->second.disk_offset);
        }
        groups_.erase(it);
    }
    return seastar::make_ready_future<>();
}

seastar::future<> BufferShard::reset() {
    // Collect offsets to delete
    std::vector<uint64_t> offsets;
    for (const auto& [_, entry] : groups_) {
        if (entry.is_on_disk()) {
            offsets.push_back(entry.disk_offset);
        }
    }
    
    // Clear maps
    groups_.clear();
    dedup_filter_.clear();
    total_trajectories_ = 0;
    total_consumed_ = 0;
    disk_usage_bytes_ = 0;

    // Async delete files
    return seastar::do_for_each(offsets, [this](uint64_t offset) {
        return disk_storage_.remove(offset);
    });
}

seastar::future<std::unique_ptr<transferqueue::BufferStatus>> BufferShard::get_status() const {
    auto status = std::make_unique<transferqueue::BufferStatus>();

    status->set_total_trajectories(total_trajectories_);
    status->set_total_consumed(total_consumed_);

    int32_t pending = 0;
    int32_t incomplete = 0;
    for (const auto& [_, entry] : groups_) {
        if (entry.is_complete()) {
            ++pending;
        } else {
            ++incomplete;
        }
    }
    status->set_pending_groups(pending);
    status->set_incomplete_groups(incomplete);
    status->set_memory_usage_bytes(static_cast<int64_t>(estimate_memory_usage()));
    status->set_disk_usage_bytes(disk_usage_bytes_);

    return seastar::make_ready_future<std::unique_ptr<transferqueue::BufferStatus>>(
        std::move(status));
}

seastar::future<std::unique_ptr<transferqueue::MetaInfo>> BufferShard::get_meta_info() const {
    auto meta = std::make_unique<transferqueue::MetaInfo>();

    int64_t total_samples = 0;
    double total_reward = 0.0;
    std::vector<std::string> finished_ids;

    for (const auto& [instance_id, entry] : groups_) {
        // 如果在磁盘上，我们可能没有详细信息（除非 load 或者 cache）
        // 这里暂时用 cached info 或者忽略
        int32_t size = entry.trajectories_size(); // 0 if on disk & not cached
        if (entry.is_on_disk()) {
             size = entry.cached_size;
             // 磁盘上的 group 默认 complete
        }
        
        total_samples += size;
        
        // reward 统计比较麻烦，如果是 disk 数据，可能得不到 reward
        // 暂时只统计内存中的
        if (entry.group_ptr) {
            for (const auto& traj : entry.group_ptr->trajectories()) {
                total_reward += traj.reward();
            }
        }

        if (entry.is_complete()) {
            finished_ids.push_back(instance_id);
        }
    }

    meta->set_total_samples(total_samples);
    meta->set_num_groups(static_cast<int32_t>(groups_.size()));
    meta->set_avg_group_size(
        groups_.empty() ? 0.0
                        : static_cast<double>(total_samples) / static_cast<double>(groups_.size()));
    meta->set_avg_reward(
        total_samples == 0 ? 0.0 : total_reward / static_cast<double>(total_samples));
    for (const auto& id : finished_ids) {
        meta->add_finished_group_ids(id);
    }

    return seastar::make_ready_future<std::unique_ptr<transferqueue::MetaInfo>>(
        std::move(meta));
}

// ============================================================================
// 配置
// ============================================================================

void BufferShard::update_config(const TransferQueueConfig& config) {
    config_ = &config;
}

// ============================================================================
// 私有方法
// ============================================================================

seastar::future<> BufferShard::check_group_timeouts() {
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(config_->group_timeout_seconds);

    for (auto& [instance_id, entry] : groups_) {
        if (entry.is_complete()) {
            continue;
        }
        if ((now - entry.created_time) >= timeout) {
             if (entry.group_ptr) {
                 entry.group_ptr->set_is_complete(true);
             }
        }
    }

    return seastar::make_ready_future<>();
}

size_t BufferShard::estimate_memory_usage() const {
    size_t usage = 0;
    for (const auto& [_, entry] : groups_) {
        // 每个 Trajectory 粗略估算：uid + instance_id + messages + extra_info
        if (entry.group_ptr) {
            usage += static_cast<size_t>(entry.group_ptr->ByteSizeLong());
        }
    }
    // 加上 dedup filter 的内存
    if (config_->uid_dedup) {
        usage += dedup_filter_.bloom_bit_count() / 8;
        usage += dedup_filter_.size() * 64;  // 平均每个 uid 64 字节（LRU 节点+map 条目）
    }
    return usage;
}


seastar::future<size_t> BufferShard::try_spill_groups() {
    size_t current_mem = estimate_memory_usage();
    size_t threshold = static_cast<size_t>(config_->max_memory_bytes * config_->spill_to_disk_threshold);

    if (current_mem <= threshold) {
        return seastar::make_ready_future<size_t>(0);
    }

    size_t target_spill = current_mem - static_cast<size_t>(config_->max_memory_bytes * 0.7); // Spill until 70%
    size_t spilled_bytes = 0;

    // Select groups to spill
    std::vector<transferqueue::TrajectoryGroup> groups_to_spill;
    std::vector<std::string> keys_to_spill;
    
    // Simple strategy: Spill finished groups
    // Optimization: Could sort by creation time (LRU) or size. But unordered_map iteration is random.
    for (auto& [id, entry] : groups_) {
        if (entry.group_ptr && entry.is_complete()) {
            size_t size = entry.group_ptr->ByteSizeLong();
            groups_to_spill.push_back(*entry.group_ptr); // Copy? Protobuf copy is heavy... 
                                                         // Ideally move from unique_ptr then restore if fail?
                                                         // Or copy for now to be safe. 
                                                         // Actually `spill_batch` takes const ref vector.
            keys_to_spill.push_back(id);
            spilled_bytes += size;
            
            if (spilled_bytes >= target_spill) {
                break;
            }
        }
    }

    if (groups_to_spill.empty()) {
        return seastar::make_ready_future<size_t>(0);
    }

    return disk_storage_.spill_batch(std::move(groups_to_spill))
        .then([this, keys_to_spill = std::move(keys_to_spill), spilled_bytes](std::vector<uint64_t> offsets) {
            if (keys_to_spill.size() != offsets.size()) {
                 // Should not happen
                 return seastar::make_exception_future<size_t>(std::runtime_error("Spill count mismatch"));
            }

            for (size_t i = 0; i < keys_to_spill.size(); ++i) {
                const auto& id = keys_to_spill[i];
                auto it = groups_.find(id);
                if (it != groups_.end()) {
                    // Cache size before deleting
                    size_t size = it->second.group_ptr ? it->second.group_ptr->ByteSizeLong() : 0;
                    int32_t num_traj = it->second.group_ptr ? it->second.group_ptr->trajectories_size() : 0;
                    
                    it->second.disk_offset = offsets[i];
                    it->second.cached_size = num_traj;
                    it->second.cached_bytes = size;
                    it->second.group_ptr.reset(); // Free memory
                    
                    disk_usage_bytes_ += size;
                }
            }
            return seastar::make_ready_future<size_t>(spilled_bytes);
        });
}

} // namespace transfer_queue
