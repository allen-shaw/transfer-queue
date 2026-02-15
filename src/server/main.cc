#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/thread.hh>
#include <seastar/util/log.hh>
#include <seastar/core/prometheus.hh>



#include "transfer_queue/common/config.h"
#include "transfer_queue/server/buffer_manager.h"
#include "transfer_queue/server/http_handler.h"
#include "transfer_queue/server/rpc_handler.h"
#include "transfer_queue/server/metrics.h"

using namespace seastar;
using namespace transfer_queue;

static logger tq_logger("transferqueue");

namespace transfer_queue {
// Implement parse_config here since it's used by main
TransferQueueConfig parse_config(int argc, char** argv) {
    TransferQueueConfig config;
    // Simple parsing logic or just return default for now if no custom args library
    // app_template handles partial args.
    // If we want to use app_template options, we should parse them there.
    // But parse_config signature implies manual parsing.
    // For now, let's rely on hardcoded defaults or simple check.
    // In a real app, we'd use boost::program_options via app_template.
    return config;
}
}

int main(int argc, char** argv) {
    app_template app;
    
    // Define options
    app.add_options()
        ("http-port", boost::program_options::value<uint16_t>()->default_value(8889), "HTTP port")
        ("rpc-port", boost::program_options::value<uint16_t>()->default_value(8890), "RPC port")
        ("group-size", boost::program_options::value<int32_t>()->default_value(16), "Group size");

    return app.run(argc, argv, [&] {
        return seastar::async([&] {
            tq_logger.info("TransferQueue Server starting...");

            auto& options = app.configuration();
            TransferQueueConfig config;
            config.http_port = options["http-port"].as<uint16_t>();
            config.rpc_port = options["rpc-port"].as<uint16_t>();
            config.group_size = options["group-size"].as<int32_t>();
            // ... load other configs

            // 1. BufferManager (Single instance managing sharded logic internally)
            auto manager = std::make_shared<BufferManager>(config);
            manager->start().get();
            tq_logger.info("BufferManager started");

            // 2. Metrics (Sharded to register per-shard metrics)
            sharded<Metrics> metrics;
            metrics.start(std::ref(*manager)).get();
            tq_logger.info("Metrics started");

            // 3. RPC Handler (Sharded to run server on all cores)
            sharded<RpcHandler> rpc_handler;
            // RpcHandler constructor takes BufferManager&
            rpc_handler.start(std::ref(*manager)).get();
            // Start listening on all shards
            rpc_handler.invoke_on_all([port = config.rpc_port](RpcHandler& h) {
                return h.start(port);
            }).get();
            tq_logger.info("RPC Server started on port {}", config.rpc_port);

            // 4. HTTP Handler
            // HttpHandler implementation is stateless wrapper around BufferManager
            HttpHandler http_handler(*manager);
            
            seastar::httpd::http_server_control http_server;
            http_server.start("transferqueue").get();
            http_server.set_routes([&http_handler](httpd::routes& r) {
                // HttpHandler::register_routes takes http_server&, but here we have routes object?
                // Wait, HttpHandler::register_routes(http_server& server).
                // http_server_control::set_routes takes a functor that receives routes&.
                // But HttpHandler needs to add routes to `server._routes`.
                // If I have `routes& r`, I can't pass it to `register_routes` if it expects `http_server&`.
                // I need to refactor HttpHandler to take `routes&` OR change how I call it.
                // HttpHandler::register_routes impl uses `server._routes`.
                // So I can't use `set_routes` helper easily if I want to use my `register_routes` method as is.
                // Instead, I can use `http_server.server().invoke_on_all`.
            }).get();

            // Add Prometheus metrics
            seastar::prometheus::config pctx;
            pctx.metric_help = "Transfer Queue Metrics";
            // Check if we should use start() or add_prometheus_routes()
            // header shows: future<> start(httpd::http_server_control& http_server, config ctx);
            seastar::prometheus::start(http_server, pctx).get();
            
            // Correct way to use existing HttpHandler::register_routes signature:
            http_server.server().invoke_on_all([&http_handler](httpd::http_server& s) {
                http_handler.register_routes(s);
                return make_ready_future<>();
            }).get();

            http_server.listen(socket_address(ipv4_addr{config.http_port})).get();
            tq_logger.info("HTTP Server started on port {}", config.http_port);

            // Shutdown signal
            promise<> stop_signal;
            bool stopping = false;
            
            engine().handle_signal(SIGINT, [&] { 
                if (!stopping) { stopping = true; stop_signal.set_value(); } 
            });
            engine().handle_signal(SIGTERM, [&] { 
                if (!stopping) { stopping = true; stop_signal.set_value(); }
            });

            // Wait for shutdown
            stop_signal.get_future().get();

            tq_logger.info("Stopping server...");
            http_server.stop().get();
            rpc_handler.stop().get();
            metrics.stop().get();
            manager->stop().get();
            tq_logger.info("Server stopped");
            


        });
    });
}
