#pragma once

#include <seastar/core/future.hh>
#include <seastar/rpc/rpc.hh>

#include "transfer_queue/server/buffer_manager.h"

namespace transfer_queue {

/// Seastar RPC API 处理器
///
/// 提供高性能 Protobuf RPC 接口：
/// - BatchWrite   — 批量写入轨迹
/// - BatchRead    — 批量读取已就绪组（支持阻塞模式）
/// - GetStatus    — 查询 Buffer 状态
/// - Subscribe    — 流式推送就绪数据
class RpcHandler {
public:
    /// 构造函数
    /// @param manager Buffer 管理器引用
    explicit RpcHandler(BufferManager& manager);

    ~RpcHandler();

    /// 启动 RPC server 并监听指定端口
    /// @param port RPC 监听端口
    seastar::future<> start(uint16_t port);

    /// 停止 RPC server
    seastar::future<> stop();

private:
    /// 注册所有 RPC verb
    void register_handlers();

    // ========================================================================
    // RPC 处理函数
    // ========================================================================

    /// BatchWrite — 批量写入轨迹
    /// 接收 Protobuf 编码的 BatchWriteRequest，返回 BatchWriteResponse
    seastar::future<seastar::sstring> handle_batch_write(seastar::sstring request_data);

    /// BatchRead — 批量读取已就绪的轨迹组
    /// 接收 BatchReadRequest，支持阻塞等待模式，返回 BatchReadResult
    seastar::future<seastar::sstring> handle_batch_read(seastar::sstring request_data);

    /// GetStatus — 查询 Buffer 状态
    /// 返回 BufferStatus
    seastar::future<seastar::sstring> handle_get_status(seastar::sstring request_data);

    /// Subscribe — 流式推送
    /// 当有组就绪时主动推送 TrajectoryGroup 给客户端
    seastar::future<> handle_subscribe(seastar::sstring request_data,
                                       seastar::rpc::sink<seastar::sstring> sink);

    BufferManager& manager_;
    // TODO: seastar::rpc::protocol 和 server 实例
};

} // namespace transfer_queue
