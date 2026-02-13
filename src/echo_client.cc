#include "transfer_queue/serializer.h"
#include "echo.pb.h"

#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/sleep.hh>
#include <seastar/rpc/rpc.hh>
#include <seastar/util/log.hh>
#include <fmt/core.h>
#include <chrono>

static seastar::logger clog("echo_client");

/// Verb IDs — must match the server.
enum class echo_verb : uint32_t {
    ECHO = 1,
};

using rpc_protocol = seastar::rpc::protocol<serializer>;

seastar::future<> run_echo_client(const std::string& server_addr, uint16_t port,
                                   const std::string& message) {
    static rpc_protocol protocol(serializer{});

    // Create a typed RPC client function for the ECHO verb.
    // Signature: sstring(sstring) — sends serialized EchoRequest, receives serialized EchoResponse.
    auto echo_rpc = protocol.make_client<seastar::sstring(seastar::sstring)>(
        static_cast<uint32_t>(echo_verb::ECHO));

    seastar::rpc::client_options co;
    co.tcp_nodelay = true;

    auto client = std::make_unique<rpc_protocol::client>(
        protocol, co, seastar::ipv4_addr{server_addr, port});

    // Build protobuf request
    echo::EchoRequest request;
    request.set_message(message);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    request.set_timestamp(now);

    std::string serialized;
    request.SerializeToString(&serialized);
    seastar::sstring payload(serialized.data(), serialized.size());

    clog.info("Sending echo request: \"{}\"", message);

    return echo_rpc(*client, payload).then([](seastar::sstring response_payload) {
        echo::EchoResponse response;
        if (!response.ParseFromArray(response_payload.data(), response_payload.size())) {
            clog.error("Failed to parse EchoResponse");
            return;
        }
        fmt::print("──────────────────────────────\n");
        fmt::print("  Echo Response\n");
        fmt::print("  Message:   \"{}\"\n", response.message());
        fmt::print("  Server:    {}\n", response.server_id());
        fmt::print("  Timestamp: {}\n", response.timestamp());
        fmt::print("──────────────────────────────\n");
    }).finally([client = std::move(client)]() mutable {
        return client->stop().finally([c = std::move(client)] {});
    });
}
