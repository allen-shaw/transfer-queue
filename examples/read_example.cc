#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/thread.hh>

#include "transfer_queue/client/client.h"

static seastar::logger logger("tq-read-example");

int main(int argc, char** argv) {
    seastar::app_template app;

    return app.run(argc, argv, [] {
        return seastar::async([] -> seastar::future<> {
            try {
                logger.info("Starting TransferQueue Read Example");

                // Create client
                transfer_queue::TransferQueueClient client("localhost", 8890);
                
                // Connect to server
                co_await client.connect();
                logger.info("Connected to TransferQueue server");

                // Read trajectory groups
                auto groups = co_await client.batch_read(5, false, 0);  // Read up to 5 groups, non-blocking
                
                if (groups.empty()) {
                    logger.info("No trajectory groups available");
                } else {
                    logger.info("Read {} trajectory groups:", groups.size());
                    
                    for (size_t i = 0; i < groups.size(); ++i) {
                        const auto& group = *groups[i];
                        logger.info("Group {}: instance_id={}, trajectories={}, complete={}", 
                                   i, group.instance_id(), group.trajectories_size(), group.is_complete());
                        
                        // Print trajectory details
                        for (int j = 0; j < group.trajectories_size(); ++j) {
                            const auto& traj = group.trajectories(j);
                            logger.info("  Trajectory {}: uid={}, reward={}", j, traj.uid(), traj.reward());
                        }
                    }
                }

                // Close connection
                co_await client.close();
                logger.info("Disconnected from TransferQueue server");

            } catch (const std::exception& e) {
                logger.error("Error: {}", e.what());
            }
        });
    });
}
