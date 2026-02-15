#pragma once

#include <memory>

#include <seastar/core/future.hh>
#include <seastar/http/httpd.hh>

#include "transfer_queue/server/buffer_manager.h"
#include "transferqueue.pb.h"

namespace transfer_queue {

/// HTTP REST API 处理器
///
/// 提供与现有 Python 客户端完全兼容的 HTTP JSON 接口：
/// - POST /buffer/write       — 写入单条轨迹
/// - POST /get_rollout_data   — 读取已就绪的训练数据
/// - POST /config             — 动态配置
/// - POST /buffer/reset       — 清空 Buffer
/// - DELETE /buffer/instance/{instance_id} — 删除指定 instance
class HttpHandler {
public:
    /// 构造函数
    /// @param manager Buffer 管理器引用
    explicit HttpHandler(BufferManager& manager);

    ~HttpHandler();

    /// 注册所有 HTTP 路由到 Seastar httpd server
    /// @param server Seastar HTTP server 实例
    void register_routes(seastar::httpd::http_server& server);

private:
    // ========================================================================
    // 路由处理函数
    // ========================================================================

    /// POST /buffer/write — 写入单条轨迹
    /// 解析 JSON 请求体，转换为 Trajectory protobuf，调用 BufferManager::write()
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle_write(std::unique_ptr<seastar::http::request> req,
                 std::unique_ptr<seastar::http::reply> rep);

    /// POST /get_rollout_data — 读取已就绪的训练数据
    /// 调用 BufferManager::read_ready_groups()，将结果序列化为兼容 JSON 格式
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle_get_rollout_data(std::unique_ptr<seastar::http::request> req,
                            std::unique_ptr<seastar::http::reply> rep);

    /// POST /config — 动态配置更新
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle_config(std::unique_ptr<seastar::http::request> req,
                  std::unique_ptr<seastar::http::reply> rep);

    /// POST /buffer/reset — 清空 Buffer
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle_reset(std::unique_ptr<seastar::http::request> req,
                 std::unique_ptr<seastar::http::reply> rep);

    /// DELETE /buffer/instance/{instance_id} — 删除指定 instance
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle_delete_instance(std::unique_ptr<seastar::http::request> req,
                           std::unique_ptr<seastar::http::reply> rep);

    // ========================================================================
    // JSON 工具
    // ========================================================================

    /// 将 JSON 字符串解析为 Trajectory protobuf
    transferqueue::Trajectory parse_write_request(const seastar::sstring& body);

    /// 将读取结果序列化为兼容的 JSON 响应
    seastar::sstring serialize_rollout_data(
        const std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>& groups);

    BufferManager& manager_;
};

} // namespace transfer_queue
