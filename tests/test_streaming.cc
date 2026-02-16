#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>

#include "transfer_queue/client/client.h"

static seastar::logger logger("tq-streaming-test");

int main(int argc, char** argv) {
    seastar::app_template app;

    return app.run(argc, argv, [] {
        return seastar::sleep(std::chrono::seconds(1)).then([] {
            logger.info("Starting TransferQueue Streaming Test");

            // Create client
            auto client = std::make_unique<transfer_queue::TransferQueueClient>("localhost", 8890);
            
            // Connect to server
            return client->connect().then([client = std::move(client)]() mutable {
                logger.info("Connected to TransferQueue server");

                // Test streaming subscribe
                return client->subscribe(1, [](const transferqueue::TrajectoryGroup& group) -> seastar::future<> {
                    logger.info("Received trajectory group: instance_id={}, trajectories={}", 
                               group.instance_id(), group.trajectories_size());
                    return seastar::make_ready_future<>();
                }).then([client = std::move(client)]() mutable {
                    logger.info("Streaming subscription started");

                    // Wait for some data
                    return seastar::sleep(std::chrono::seconds(5)).then([client = std::move(client)]() mutable {
                        // Unsubscribe
                        return client->unsubscribe().then([client = std::move(client)]() mutable {
                            logger.info("Unsubscribed from streaming");

                            // Close connection
                            return client->close().then([] {
                                logger.info("Disconnected from TransferQueue server");
                            });
                        });
                    });
                });
            }).handle_exception([](auto ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (const std::exception& e) {
                    logger.error("Error: {}", e.what());
                }
            });
        });
    });
}
