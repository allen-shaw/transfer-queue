#include "transfer_queue/server/http_handler.h"

namespace transfer_queue {

HttpHandler::HttpHandler(BufferManager& manager)
    : manager_(manager) {
}

HttpHandler::~HttpHandler() = default;

void HttpHandler::register_routes(seastar::httpd::http_server& server) {
    // TODO: 实现
    // 注册 POST /buffer/write
    // 注册 POST /get_rollout_data
    // 注册 POST /config
    // 注册 POST /buffer/reset
    // 注册 DELETE /buffer/instance/{instance_id}
}

// ============================================================================
// 路由处理函数
// ============================================================================

seastar::future<std::unique_ptr<seastar::http::reply>>
HttpHandler::handle_write(std::unique_ptr<seastar::http::request> req,
                          std::unique_ptr<seastar::http::reply> rep) {
    // TODO: 实现
    // 1. 解析 JSON body → transferqueue::Trajectory
    // 2. 调用 manager_.write()
    // 3. 构建 JSON 响应
    return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(std::move(rep));
}

seastar::future<std::unique_ptr<seastar::http::reply>>
HttpHandler::handle_get_rollout_data(std::unique_ptr<seastar::http::request> req,
                                     std::unique_ptr<seastar::http::reply> rep) {
    // TODO: 实现
    // 1. 调用 manager_.read_ready_groups()
    // 2. 序列化为兼容 JSON 格式
    return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(std::move(rep));
}

seastar::future<std::unique_ptr<seastar::http::reply>>
HttpHandler::handle_config(std::unique_ptr<seastar::http::request> req,
                           std::unique_ptr<seastar::http::reply> rep) {
    // TODO: 实现
    return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(std::move(rep));
}

seastar::future<std::unique_ptr<seastar::http::reply>>
HttpHandler::handle_reset(std::unique_ptr<seastar::http::request> req,
                          std::unique_ptr<seastar::http::reply> rep) {
    // TODO: 实现
    return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(std::move(rep));
}

seastar::future<std::unique_ptr<seastar::http::reply>>
HttpHandler::handle_delete_instance(std::unique_ptr<seastar::http::request> req,
                                    std::unique_ptr<seastar::http::reply> rep) {
    // TODO: 实现
    return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(std::move(rep));
}

// ============================================================================
// JSON 工具
// ============================================================================

transferqueue::Trajectory HttpHandler::parse_write_request(const seastar::sstring& body) {
    // TODO: 实现 JSON 解析
    return transferqueue::Trajectory{};
}

seastar::sstring HttpHandler::serialize_rollout_data(
        const std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>>& groups) {
    // TODO: 实现 JSON 序列化
    return "{}";
}

} // namespace transfer_queue
