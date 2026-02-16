#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/thread.hh>

#include "transfer_queue/client/client.h"

static seastar::logger logger("tq-status-example");

int main(int argc, char** argv) {
    seastar::app_template app;

    return app.run(argc, argv, [] {
        return seastar::async([] -> seastar::future<> {
            try {
                logger.info("Starting TransferQueue Status Example");

                // Create client
                transfer_queue::TransferQueueClient client("localhost", 8890);
                
                // Connect to server
                co_await client.connect();
                logger.info("Connected to TransferQueue server");

                // Get buffer status
                auto status = co_await client.get_status();
                
                logger.info("Buffer Status:");
                logger.info("  Total trajectories: {}", status->total_trajectories());
                logger.info("  Total consumed: {}", status->total_consumed());
                logger.info("  Pending groups: {}", status->pending_groups());
                logger.info("  Incomplete groups: {}", status->incomplete_groups());
                logger.info("  Memory usage: {} bytes", status->memory_usage_bytes());
                logger.info("  Disk usage: {} bytes", status->disk_usage_bytes());

                // Close connection
                co_await client.close();
                logger.info("Disconnected from TransferQueue server");

            } catch (const std::exception& e) {
                logger.error("Error: {}", e.what());
            }
        });
    });
}
