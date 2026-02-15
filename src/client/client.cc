#include "transfer_queue/client/client.h"

namespace transfer_queue {

TransferQueueClient::TransferQueueClient(const std::string& host, uint16_t port)
    : host_(host), port_(port) {
}

TransferQueueClient::~TransferQueueClient() = default;

// ============================================================================
// 连接管理
// ============================================================================

seastar::future<> TransferQueueClient::connect() {
    // TODO: 实现
    // 1. 创建 rpc::protocol::client
    // 2. 连接到 host_:port_
    // 3. connected_ = true
    return seastar::make_ready_future<>();
}

seastar::future<> TransferQueueClient::close() {
    // TODO: 实现
    // 1. 关闭 RPC 连接
    // 2. connected_ = false
    return seastar::make_ready_future<>();
}

bool TransferQueueClient::is_connected() const {
    return connected_;
}

// ============================================================================
// 写入
// ============================================================================

seastar::future<int32_t> TransferQueueClient::batch_write(std::vector<TrajectoryData> trajectories) {
    // TODO: 实现
    // 1. 转换为 BatchWriteRequest protobuf
    // 2. 序列化并通过 RPC 发送
    // 3. 反序列化 BatchWriteResponse
    return seastar::make_ready_future<int32_t>(0);
}

seastar::future<bool> TransferQueueClient::write(TrajectoryData trajectory) {
    // TODO: 实现
    // 便捷方法：将单条轨迹包装为 batch_write
    return seastar::make_ready_future<bool>(false);
}

// ============================================================================
// 读取
// ============================================================================

seastar::future<std::vector<TrajectoryGroupData>>
TransferQueueClient::batch_read(int32_t max_groups, bool block, int32_t timeout_ms) {
    // TODO: 实现
    // 1. 构建 BatchReadRequest
    // 2. 通过 RPC 发送
    // 3. 反序列化 BatchReadResult
    return seastar::make_ready_future<std::vector<TrajectoryGroupData>>();
}

// ============================================================================
// 状态查询
// ============================================================================

seastar::future<transferqueue::BufferStatus> TransferQueueClient::get_status() {
    // TODO: 实现
    // 1. 发送 GetStatusRequest
    // 2. 反序列化 BufferStatus
    return seastar::make_ready_future<transferqueue::BufferStatus>();
}

// ============================================================================
// 流式订阅
// ============================================================================

seastar::future<> TransferQueueClient::subscribe(
        int32_t prefetch_groups,
        std::function<seastar::future<>(TrajectoryGroupData)> callback) {
    // TODO: 实现
    // 1. 发送 SubscribeRequest
    // 2. 启动 rpc::source 读取循环
    // 3. 每收到一个 TrajectoryGroup 调用 callback
    return seastar::make_ready_future<>();
}

seastar::future<> TransferQueueClient::unsubscribe() {
    // TODO: 实现
    return seastar::make_ready_future<>();
}

} // namespace transfer_queue
