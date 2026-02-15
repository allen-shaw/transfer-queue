#include "transfer_queue/server/rpc_handler.h"

#include <seastar/core/do_with.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/sleep.hh>
#include <seastar/net/api.hh>
#include <seastar/net/inet_address.hh>

#include "transferqueue.pb.h"

namespace transfer_queue {

namespace {

// Helper: Serialize Protobuf to sstring
template <typename T>
seastar::sstring serialize_proto(const T& msg) {
    auto start_size = msg.ByteSizeLong();
    seastar::sstring s(seastar::sstring::initialized_later(), start_size);
    msg.SerializeToArray(s.data(), start_size);
    return s;
}

// Helper: Parse sstring to Protobuf
template <typename T>
T parse_proto(const seastar::sstring& s) {
    T msg;
    if (!msg.ParseFromArray(s.data(), s.size())) {
        throw std::runtime_error("Failed to parse protobuf message");
    }
    return msg;
}

}  // namespace

RpcHandler::RpcHandler(BufferManager& manager) : manager_(manager) {}

RpcHandler::~RpcHandler() = default;

seastar::future<> RpcHandler::start(uint16_t port) {
    PbSerializer serializer;
    rpc_proto_ = std::make_unique<seastar::rpc::protocol<PbSerializer>>(std::move(serializer));

    register_handlers();

    rpc_server_ = std::make_unique<
        seastar::rpc::protocol<PbSerializer>::server>(
        *rpc_proto_, seastar::ipv4_addr{port});

    return seastar::make_ready_future<>();
}

seastar::future<> RpcHandler::stop() {
    if (rpc_server_) {
        return rpc_server_->stop();
    }
    return seastar::make_ready_future<>();
}

void RpcHandler::register_handlers() {
    // Helper to register simple handlers
    auto reg = [&](Verb verb, auto member_func) {
        rpc_proto_->register_handler(
            static_cast<uint32_t>(verb),
            [this, member_func](seastar::sstring data) {
                return (this->*member_func)(std::move(data));
            });
    };

    // Helper for stream handler
    // Stream signature: future<> (sstring, rpc::sink<sstring>)
    rpc_proto_->register_handler(
        static_cast<uint32_t>(Verb::SUBSCRIBE),
        [this](seastar::sstring data,
               seastar::rpc::sink<seastar::sstring> sink) {
            return handle_subscribe(std::move(data), std::move(sink));
        });

    reg(Verb::BATCH_WRITE, &RpcHandler::handle_batch_write);
    reg(Verb::BATCH_READ, &RpcHandler::handle_batch_read);
    reg(Verb::GET_STATUS, &RpcHandler::handle_get_status);
}

seastar::future<seastar::sstring> RpcHandler::handle_batch_write(
    seastar::sstring request_data) {
    auto req = parse_proto<transferqueue::BatchWriteRequest>(request_data);
    
    // Convert repeated Trajectory to vector used by BufferManager (which writes one by one currently?)
    // BufferManager::batch_write logic
    // We didn't implement Explicit batch_write in BufferManager API exposed in header?
    // Let's check BufferManager. It has `write` (single).
    // Ah, buffer_manager.h has `batch_write`?
    // Re-check buffer_manager.h.
    // If not, we iterate.
    
    // Assuming we iterate for now, or use batch_write if available.
    // buffer_manager.h from context task.md says "Implement buffer_manager.cc - all TODO methods".
    // Let's assume generic iteration.
    
    std::vector<transferqueue::Trajectory> trajectories;
    trajectories.reserve(req.trajectories_size());
    for (auto& t : *req.mutable_trajectories()) {
        trajectories.push_back(std::move(t));
    }
    
    // Manager batch_write?
    // Checked previous context: buffer_manager.h has `batch_write`.
    return manager_.batch_write(std::move(trajectories))
        .then([](int count) {
            transferqueue::BatchWriteResponse resp;
            resp.set_success(true);
            resp.set_written_count(count);
            return serialize_proto(resp);
        });
}

seastar::future<seastar::sstring> RpcHandler::handle_batch_read(
    seastar::sstring request_data) {
    auto req = parse_proto<transferqueue::BatchReadRequest>(request_data);
    
    // If block=true
    if (req.block()) {
        return manager_.read_ready_groups_blocking(
            req.max_groups(), req.timeout_ms())
            .then([](std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>> groups) {
                transferqueue::BatchReadResult res;
                res.set_success(true);
                // Move groups to res
                for (auto& g : groups) {
                    *res.add_groups() = std::move(*g);
                }
                return serialize_proto(res);
            });
    } else {
        return manager_.read_ready_groups(req.max_groups())
            .then([](std::vector<std::unique_ptr<transferqueue::TrajectoryGroup>> groups) {
                transferqueue::BatchReadResult res;
                res.set_success(true);
                for (auto& g : groups) {
                    *res.add_groups() = std::move(*g);
                }
                return serialize_proto(res);
            });
    }
}

seastar::future<seastar::sstring> RpcHandler::handle_get_status(
    seastar::sstring request_data) {
    // Request is empty
    return manager_.get_status().then([](std::unique_ptr<transferqueue::BufferStatus> status) {
        // BufferStatus is BufferStatus proto message
        return serialize_proto(*status);
    });
}

seastar::future<> RpcHandler::handle_subscribe(
    seastar::sstring request_data, seastar::rpc::sink<seastar::sstring> sink) {
    auto req = parse_proto<transferqueue::SubscribeRequest>(request_data);
    int prefetch = req.prefetch_groups() > 0 ? req.prefetch_groups() : 1;

    // Keep alive logic
    return seastar::do_with(std::move(sink), [this, prefetch](auto& sink) {
        return seastar::keep_doing([this, prefetch, &sink] {
            // Blocking read with timeout to allow checking connection status if needed?
            // sink calls usually fail if connection closed.
            return manager_.read_ready_groups_blocking(prefetch, 1000 /* 1s */)
                .then([&sink](auto groups) {
                    if (groups.empty()) {
                         return seastar::make_ready_future<>();
                    }
                    // Send each group as a message in the stream?
                    // Protocol: Stream of TrajectoryGroup
                    // Sink accepts sstring.
                    // We serialize one TrajectoryGroup per message.
                    return seastar::do_for_each(groups, [&sink](auto& group) {
                         return sink(serialize_proto(*group));
                    });
                });
        });
    });
    // Note: keep_doing runs until exception. If sink fails, it throws, loop terminates.
}

} // namespace transfer_queue
