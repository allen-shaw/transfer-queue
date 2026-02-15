#include "transfer_queue/server/rpc_handler.h"

namespace transfer_queue {

RpcHandler::RpcHandler(BufferManager& manager)
    : manager_(manager) {
}

RpcHandler::~RpcHandler() = default;

seastar::future<> RpcHandler::start(uint16_t port) {
    // TODO: 实现
    // 1. 创建 rpc::protocol 实例
    // 2. register_handlers()
    // 3. 监听端口
    return seastar::make_ready_future<>();
}

seastar::future<> RpcHandler::stop() {
    // TODO: 实现
    return seastar::make_ready_future<>();
}

void RpcHandler::register_handlers() {
    // TODO: 实现
    // 注册 BatchWrite, BatchRead, GetStatus, Subscribe verb
}

// ============================================================================
// RPC 处理函数
// ============================================================================

seastar::future<seastar::sstring> RpcHandler::handle_batch_write(seastar::sstring request_data) {
    // TODO: 实现
    // 1. 反序列化 BatchWriteRequest
    // 2. 转换为 TrajectoryData 列表
    // 3. 调用 manager_.batch_write()
    // 4. 序列化 BatchWriteResponse
    return seastar::make_ready_future<seastar::sstring>("");
}

seastar::future<seastar::sstring> RpcHandler::handle_batch_read(seastar::sstring request_data) {
    // TODO: 实现
    // 1. 反序列化 BatchReadRequest
    // 2. 根据 block 参数选择 read_ready_groups 或 read_ready_groups_blocking
    // 3. 序列化 BatchReadResult
    return seastar::make_ready_future<seastar::sstring>("");
}

seastar::future<seastar::sstring> RpcHandler::handle_get_status(seastar::sstring request_data) {
    // TODO: 实现
    // 1. 调用 manager_.get_status()
    // 2. 序列化 BufferStatus
    return seastar::make_ready_future<seastar::sstring>("");
}

seastar::future<> RpcHandler::handle_subscribe(seastar::sstring request_data,
                                                seastar::rpc::sink<seastar::sstring> sink) {
    // TODO: 实现
    // 1. 反序列化 SubscribeRequest
    // 2. 启动轮询/事件循环，有 ready group 时通过 sink 推送
    return seastar::make_ready_future<>();
}

} // namespace transfer_queue
