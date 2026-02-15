#include "transfer_queue/server/metrics.h"
#include "transfer_queue/server/buffer_shard.h"
#include <seastar/core/metrics.hh>

namespace transfer_queue {

Metrics::Metrics(BufferManager& manager) : manager_(manager) {
    register_metrics();
}

Metrics::~Metrics() = default;

seastar::future<> Metrics::start() {
    // Already registered in constructor
    return seastar::make_ready_future<>();
}

seastar::future<> Metrics::stop() {
    // Deregistration happens on destruction of metric_groups_ in destructor?
    // metric_groups clears on destruction.
    // But we might want explicit stop.
    // metric_groups has clear().
    // However, since Metrics object is destroyed on exit, it's fine.
    return seastar::make_ready_future<>();
}

void Metrics::register_metrics() {
    // Access local shard
    // manager_ is BufferManager
    // friend access to shards_
    auto& shard = manager_.shards_.local();

    // Register metrics for this shard
    // We use the "transferqueue" label or prefix?
    // Seastar metrics use groups.
    namespace sm = seastar::metrics;
    
    // Note: ensure shard stays alive as long as metrics are registered.
    // Metrics object lifetime should check with BufferManager lifetime.
    // Assuming Metrics created after BufferManager and destroyed before?
    // Actually, in main.cc, Metrics created after.
    // So Metrics destroyed first. Good.

    metric_groups_.add_group("transfer_queue", {
        sm::make_gauge("total_trajectories", 
            sm::description("Total trajectories received"),
            [&shard] { return static_cast<double>(shard.total_trajectories_); }),
        sm::make_gauge("total_consumed",
            sm::description("Total trajectories consumed"),
            [&shard] { return static_cast<double>(shard.total_consumed_); }),
        sm::make_gauge("active_groups",
             sm::description("Current number of active trajectory groups"),
            [&shard] { return static_cast<double>(shard.groups_.size()); }),
        sm::make_gauge("memory_usage_bytes",
            sm::description("Estimated memory usage in bytes"),
            [&shard] { return static_cast<double>(shard.estimate_memory_usage()); })
    });
}

} // namespace transfer_queue
