#pragma once

#include "transfer_queue/serializer.h"
#include <seastar/core/future.hh>
#include <seastar/core/sstring.hh>
#include <seastar/rpc/rpc.hh>
#include <memory>

/// Verb IDs for the echo service RPC protocol.
enum class echo_verb : uint32_t {
    ECHO = 1,
};

/// Echo RPC service built on Seastar's native RPC framework.
/// Protobuf messages (EchoRequest/EchoResponse) are serialized to sstring
/// for transport over Seastar RPC.
class echo_service {
public:
    using rpc_protocol = seastar::rpc::protocol<serializer>;

    /// Start the echo RPC server on the given port.
    seastar::future<> start(uint16_t port);

    /// Stop the server gracefully.
    seastar::future<> stop();

private:
    /// Handle an echo request: deserialize EchoRequest from payload,
    /// build EchoResponse, return serialized bytes.
    seastar::sstring handle_echo(seastar::sstring request_payload);

    std::unique_ptr<rpc_protocol> _protocol;
    std::unique_ptr<rpc_protocol::server> _server;
};
