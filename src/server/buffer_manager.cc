#include "transfer_queue/server/buffer_manager.h"

#include <seastar/core/smp.hh>
#include <functional>

namespace transfer_queue {

BufferManager::BufferManager(const TransferQueueConfig& config)
    : config_(config) {
}

BufferManager::~BufferManager() = default;

seastar::future<> BufferManager::start() {
    // TODO: 实现
    // shards_.start(std::ref(config_));
    return seastar::make_ready_future<>();
}

seastar::future<> BufferManager::stop() {
    // TODO: 实现
    // return shards_.stop();
    return seastar::make_ready_future<>();
}

// ============================================================================
// 写入
// ============================================================================

seastar::future<bool> BufferManager::write(TrajectoryData trajectory) {
    // TODO: 实现
    // auto sid = shard_for(trajectory.instance_id);
    // return shards_.invoke_on(sid, &BufferShard::write, std::move(trajectory));
    return seastar::make_ready_future<bool>(false);
}

seastar::future<int32_t> BufferManager::batch_write(std::vector<TrajectoryData> trajectories) {
    // TODO: 实现
    // 1. 按 instance_id 分组
    // 2. 将各组分发到对应 shard
    // 3. 聚合写入结果
    return seastar::make_ready_future<int32_t>(0);
}

// ============================================================================
// 读取
// ============================================================================

seastar::future<std::vector<TrajectoryGroupData>> BufferManager::read_ready_groups(int32_t max_groups) {
    // TODO: 实现
    // return shards_.map_reduce(...)
    return seastar::make_ready_future<std::vector<TrajectoryGroupData>>();
}

seastar::future<std::vector<TrajectoryGroupData>> BufferManager::read_ready_groups_blocking(
        int32_t max_groups, int32_t timeout_ms) {
    // TODO: 实现
    // 轮询或条件变量等待 ready groups
    return seastar::make_ready_future<std::vector<TrajectoryGroupData>>();
}

seastar::future<bool> BufferManager::has_ready_groups() const {
    // TODO: 实现
    return seastar::make_ready_future<bool>(false);
}

// ============================================================================
// 管理
// ============================================================================

seastar::future<> BufferManager::delete_instance(const std::string& instance_id) {
    // TODO: 实现
    // auto sid = shard_for(instance_id);
    // return shards_.invoke_on(sid, &BufferShard::delete_instance, instance_id);
    return seastar::make_ready_future<>();
}

seastar::future<> BufferManager::reset() {
    // TODO: 实现
    // return shards_.invoke_on_all(&BufferShard::reset);
    return seastar::make_ready_future<>();
}

seastar::future<transferqueue::BufferStatus> BufferManager::get_status() const {
    // TODO: 实现
    // 聚合所有 shard 的状态
    return seastar::make_ready_future<transferqueue::BufferStatus>();
}

seastar::future<> BufferManager::update_config(const transferqueue::ConfigRequest& config_req) {
    // TODO: 实现
    // 广播到所有 shard
    return seastar::make_ready_future<>();
}

// ============================================================================
// 私有方法
// ============================================================================

unsigned BufferManager::shard_for(const std::string& instance_id) const {
    // TODO: 实现
    // return std::hash<std::string>{}(instance_id) % seastar::smp::count;
    return 0;
}

} // namespace transfer_queue
