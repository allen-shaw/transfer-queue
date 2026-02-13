#include "transfer_queue/echo_server.h"
#include "echo.pb.h"

#include <seastar/core/reactor.hh>
#include <seastar/util/log.hh>
#include <fmt/core.h>
#include <chrono>

static seastar::logger slog("echo_server");

seastar::sstring echo_service::handle_echo(seastar::sstring request_payload) {
    // Deserialize protobuf request
    echo::EchoRequest request;
    if (!request.ParseFromArray(request_payload.data(), request_payload.size())) {
        throw std::runtime_error("Failed to parse EchoRequest");
    }

    slog.info("Received echo request: \"{}\" (timestamp={})",
              request.message(), request.timestamp());

    // Build protobuf response
    echo::EchoResponse response;
    response.set_message(request.message());  // echo back

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    response.set_timestamp(now);
    response.set_server_id(fmt::format("seastar-echo@shard-{}", seastar::this_shard_id()));

    // Serialize to sstring
    std::string out;
    response.SerializeToString(&out);
    return seastar::sstring(out.data(), out.size());
}

seastar::future<> echo_service::start(uint16_t port) {
    _protocol = std::make_unique<rpc_protocol>(serializer{});

    seastar::rpc::resource_limits limits;
    limits.bloat_factor = 1;
    limits.basic_request_size = 0;
    limits.max_memory = 10'000'000;

    // Register the echo verb handler
    _protocol->register_handler(
        static_cast<uint32_t>(echo_verb::ECHO),
        [this](seastar::sstring request_payload) {
            return handle_echo(std::move(request_payload));
        }
    );

    seastar::rpc::server_options so;
    _server = std::make_unique<rpc_protocol::server>(
        *_protocol, so, seastar::ipv4_addr{port}, limits);

    slog.info("Echo server started on port {}", port);
    return seastar::make_ready_future<>();
}

seastar::future<> echo_service::stop() {
    if (_server) {
        return _server->stop();
    }
    return seastar::make_ready_future<>();
}
