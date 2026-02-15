#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

#include <seastar/core/future.hh>

#include "transferqueue.pb.h"

namespace transfer_queue {

/// TransferQueue C++ 客户端
///
/// 通过 Seastar RPC 连接 TransferQueue Server，提供：
/// - 批量写入 / 读取
/// - 状态查询
/// - 流式订阅
///
/// 使用示例：
///   auto client = TransferQueueClient("192.168.1.10", 8890);
///   co_await client.connect();
///   co_await client.batch_write(trajectories);
///   auto result = co_await client.batch_read(8);
///   co_await client.close();
class TransferQueueClient {
public:
    /// 构造函数
    /// @param host 服务端地址
    /// @param port 服务端 RPC 端口
    TransferQueueClient(const std::string& host, uint16_t port);

    ~TransferQueueClient();

    // ========================================================================
    // 连接管理
    // ========================================================================

    /// 建立到服务端的 RPC 连接
    seastar::future<> connect();

    /// 关闭连接
    seastar::future<> close();

    /// 是否已连接
    bool is_connected() const;

    // ========================================================================
    // 写入
    // ========================================================================

    /// 批量写入轨迹
    /// @param trajectories 要写入的轨迹列表
    /// @return 实际写入条数
    seastar::future<int32_t> batch_write(std::vector<transferqueue::Trajectory> trajectories);

    /// 写入单条轨迹（便捷方法）
    seastar::future<bool> write(transferqueue::Trajectory trajectory);

    // ========================================================================
    // 读取
    // ========================================================================

    /// 批量读取已就绪的轨迹组
    /// @param max_groups 最多返回多少组
    /// @param block 是否阻塞等待
    /// @param timeout_ms 阻塞超时（ms），仅在 block=true 时有效
    seastar::future<std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>>
    batch_read(int32_t max_groups = 0, bool block = false, int32_t timeout_ms = 0);

    // ========================================================================
    // 状态查询
    // ========================================================================

    /// 查询 Buffer 状态
    seastar::future<std::unique_ptr<transferqueue::BufferStatus>> get_status();

    // ========================================================================
    // 流式订阅
    // ========================================================================

    /// 订阅就绪数据推送
    /// @param prefetch_groups 预取组数
    /// @param callback 每个就绪组到达时的回调
    seastar::future<> subscribe(
        int32_t prefetch_groups,
        std::function<seastar::future<>(const transferqueue::TrajectoryGroup&)> callback);

    /// 取消订阅
    seastar::future<> unsubscribe();

private:
    std::string host_;
    uint16_t port_;
    bool connected_ = false;
    // TODO: seastar::rpc::protocol::client 实例
};

} // namespace transfer_queue
