#include "transfer_queue/server/buffer_shard.h"

#include <chrono>

namespace transfer_queue {

// ============================================================================
// 构造 / 析构
// ============================================================================

BufferShard::BufferShard(const TransferQueueConfig& config)
    : config_(&config)
    , dedup_filter_(100000, 0.01) {
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
    auto& group = groups_[instance_id];

    // 首次创建 group：初始化元数据
    if (group.instance_id().empty()) {
        group.set_instance_id(instance_id);
        group.set_group_size(config_->group_size);
        group.set_is_complete(false);
        group_created_times_[instance_id] = std::chrono::steady_clock::now();
    }

    // 3. 添加 trajectory 到 group
    *group.add_trajectories() = std::move(trajectory);
    ++total_trajectories_;

    // 4. 检查 group 是否已满 → 标记 ready
    if (group.trajectories_size() >= group.group_size()) {
        group.set_is_complete(true);
    }

    return seastar::make_ready_future<bool>(true);
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
    std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>> result;

    // 收集要删除的 instance_id
    std::vector<std::string> to_remove;

    for (auto& [instance_id, group] : groups_) {
        if (!group.is_complete()) {
            continue;
        }

        // 限制返回组数
        if (max_groups > 0 && static_cast<int32_t>(result.size()) >= max_groups) {
            break;
        }

        total_consumed_ += group.trajectories_size();
        auto ptr = std::make_unique<transferqueue::TrajectoryGroup>(std::move(group));
        result.push_back(std::move(ptr));
        to_remove.push_back(instance_id);
    }

    // 从 groups_ 中删除已消费的组
    for (const auto& id : to_remove) {
        groups_.erase(id);
        group_created_times_.erase(id);
    }

    return seastar::make_ready_future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>(
        std::move(result));
}

seastar::future<bool> BufferShard::has_ready_groups() const {
    for (const auto& [_, group] : groups_) {
        if (group.is_complete()) {
            return seastar::make_ready_future<bool>(true);
        }
    }
    return seastar::make_ready_future<bool>(false);
}

// ============================================================================
// 管理
// ============================================================================

seastar::future<> BufferShard::delete_instance(const std::string& instance_id) {
    groups_.erase(instance_id);
    group_created_times_.erase(instance_id);
    return seastar::make_ready_future<>();
}

seastar::future<> BufferShard::reset() {
    groups_.clear();
    group_created_times_.clear();
    dedup_filter_.clear();
    total_trajectories_ = 0;
    total_consumed_ = 0;
    return seastar::make_ready_future<>();
}

seastar::future<std::unique_ptr<transferqueue::BufferStatus>> BufferShard::get_status() const {
    auto status = std::make_unique<transferqueue::BufferStatus>();

    status->set_total_trajectories(total_trajectories_);
    status->set_total_consumed(total_consumed_);

    int32_t pending = 0;
    int32_t incomplete = 0;
    for (const auto& [_, group] : groups_) {
        if (group.is_complete()) {
            ++pending;
        } else {
            ++incomplete;
        }
    }
    status->set_pending_groups(pending);
    status->set_incomplete_groups(incomplete);
    status->set_memory_usage_bytes(static_cast<int64_t>(estimate_memory_usage()));
    status->set_disk_usage_bytes(0);

    return seastar::make_ready_future<std::unique_ptr<transferqueue::BufferStatus>>(
        std::move(status));
}

seastar::future<std::unique_ptr<transferqueue::MetaInfo>> BufferShard::get_meta_info() const {
    auto meta = std::make_unique<transferqueue::MetaInfo>();

    int64_t total_samples = 0;
    double total_reward = 0.0;
    std::vector<std::string> finished_ids;

    for (const auto& [instance_id, group] : groups_) {
        total_samples += group.trajectories_size();
        for (const auto& traj : group.trajectories()) {
            total_reward += traj.reward();
        }
        if (group.is_complete()) {
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

    for (auto& [instance_id, group] : groups_) {
        if (group.is_complete()) {
            continue;
        }
        auto it = group_created_times_.find(instance_id);
        if (it != group_created_times_.end() && (now - it->second) >= timeout) {
            group.set_is_complete(true);
        }
    }

    return seastar::make_ready_future<>();
}

size_t BufferShard::estimate_memory_usage() const {
    size_t usage = 0;
    for (const auto& [_, group] : groups_) {
        // 每个 Trajectory 粗略估算：uid + instance_id + messages + extra_info
        usage += static_cast<size_t>(group.ByteSizeLong());
    }
    // 加上 dedup filter 的内存
    usage += dedup_filter_.bloom_bit_count() / 8;
    usage += dedup_filter_.size() * 64;  // 平均每个 uid 64 字节（LRU 节点+map 条目）
    return usage;
}

} // namespace transfer_queue
