#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/thread.hh>
#include <seastar/util/log.hh>

#include "transfer_queue/common/config.h"
#include "transfer_queue/server/buffer_manager.h"
#include "transfer_queue/server/http_handler.h"
#include "transfer_queue/server/rpc_handler.h"
#include "transfer_queue/server/metrics.h"

static seastar::logger tq_logger("transferqueue");

int main(int argc, char** argv) {
    seastar::app_template app;

    // TODO: 添加命令行参数
    // app.add_options()
    //     ("port", ..., "HTTP port")
    //     ("rpc-port", ..., "RPC port")
    //     ("group-size", ..., "Group size")
    //     ...

    return app.run(argc, argv, [] {
        return seastar::async([] {
            tq_logger.info("TransferQueue Server starting...");

            // TODO: 实现启动流程
            // 1. 解析配置
            // auto config = transfer_queue::parse_config(argc, argv);

            // 2. 初始化 BufferManager (sharded)
            // auto manager = transfer_queue::BufferManager(config);
            // manager.start().get();

            // 3. 启动 HTTP server
            // auto http_handler = transfer_queue::HttpHandler(manager);
            // seastar::httpd::http_server http_server("transferqueue");
            // http_handler.register_routes(http_server);
            // http_server.listen(config.http_port).get();

            // 4. 启动 RPC server
            // auto rpc_handler = transfer_queue::RpcHandler(manager);
            // rpc_handler.start(config.rpc_port).get();

            // 5. 启动 Metrics
            // auto metrics = transfer_queue::Metrics(manager);
            // metrics.start().get();

            tq_logger.info("TransferQueue Server started");

            // 阻塞等待退出信号
            // seastar::engine().at_exit([&] {
            //     return rpc_handler.stop()
            //         .then([&] { return http_server.stop(); })
            //         .then([&] { return metrics.stop(); })
            //         .then([&] { return manager.stop(); });
            // });
        });
    });
}
