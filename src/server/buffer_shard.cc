#include "transfer_queue/server/buffer_shard.h"

namespace transfer_queue {

BufferShard::BufferShard(const TransferQueueConfig& config)
    : config_(&config) {
    // TODO: 实现
}

BufferShard::~BufferShard() = default;

// ============================================================================
// 写入
// ============================================================================

seastar::future<bool> BufferShard::write(transferqueue::Trajectory trajectory) {
    // TODO: 实现
    // 1. UID 去重检查
    // 2. 按 instance_id 插入到对应 group
    // 3. 检查 group 是否已满 → 标记 ready
    return seastar::make_ready_future<bool>(false);
}

seastar::future<int32_t> BufferShard::batch_write(std::vector<transferqueue::Trajectory> trajectories) {
    // TODO: 实现
    return seastar::make_ready_future<int32_t>(0);
}

// ============================================================================
// 读取
// ============================================================================

seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
BufferShard::read_ready_groups(int32_t max_groups) {
    // TODO: 实现
    // 1. 遍历 groups_，收集 is_complete == true 的组
    // 2. 从 groups_ 中删除已读取的组（消费语义）
    // 3. 更新 total_consumed_
    return seastar::make_ready_future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>();
}

seastar::future<bool> BufferShard::has_ready_groups() const {
    // TODO: 实现
    return seastar::make_ready_future<bool>(false);
}

// ============================================================================
// 管理
// ============================================================================

seastar::future<> BufferShard::delete_instance(const std::string& instance_id) {
    // TODO: 实现
    return seastar::make_ready_future<>();
}

seastar::future<> BufferShard::reset() {
    // TODO: 实现
    return seastar::make_ready_future<>();
}

seastar::future<std::unique_ptr<transferqueue::BufferStatus>> BufferShard::get_status() const {
    // TODO: 实现
    return seastar::make_ready_future<std::unique_ptr<transferqueue::BufferStatus>>(
        std::make_unique<transferqueue::BufferStatus>());
}

seastar::future<std::unique_ptr<transferqueue::MetaInfo>> BufferShard::get_meta_info() const {
    // TODO: 实现
    return seastar::make_ready_future<std::unique_ptr<transferqueue::MetaInfo>>(
        std::make_unique<transferqueue::MetaInfo>());
}

// ============================================================================
// 配置
// ============================================================================

void BufferShard::update_config(const TransferQueueConfig& config) {
    // TODO: 实现
}

// ============================================================================
// 私有方法
// ============================================================================

seastar::future<> BufferShard::check_group_timeouts() {
    // TODO: 实现
    return seastar::make_ready_future<>();
}

size_t BufferShard::estimate_memory_usage() const {
    // TODO: 实现
    return 0;
}

} // namespace transfer_queue
