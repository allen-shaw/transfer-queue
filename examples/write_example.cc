#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/thread.hh>

#include "transfer_queue/client/client.h"

static seastar::logger logger("tq-write-example");

int main(int argc, char** argv) {
    seastar::app_template app;

    return app.run(argc, argv, [] {
        return seastar::async([] {
            try {
                logger.info("Starting TransferQueue Write Example");

                // Create client
                transfer_queue::TransferQueueClient client("localhost", 8890);
                
                // Connect to server
                client.connect().get();
                logger.info("Connected to TransferQueue server");

                // Create sample trajectory
                transferqueue::Trajectory trajectory;
                trajectory.set_uid("sample-uid-12345");
                trajectory.set_instance_id("sample-instance-001");
                trajectory.set_reward(0.85);
                
                // Add chat messages
                auto* system_msg = trajectory.add_messages();
                system_msg->set_role("system");
                system_msg->set_content("You are a helpful AI assistant.");
                
                auto* user_msg = trajectory.add_messages();
                user_msg->set_role("user");
                user_msg->set_content("Explain quantum computing in simple terms.");
                
                auto* assistant_msg = trajectory.add_messages();
                assistant_msg->set_role("assistant");
                assistant_msg->set_content("Quantum computing uses quantum bits (qubits) that can exist in multiple states at once, unlike classical bits that are either 0 or 1.");

                // Write trajectory
                bool success = client.write(std::move(trajectory)).get();
                if (success) {
                    logger.info("Successfully wrote trajectory");
                } else {
                    logger.error("Failed to write trajectory");
                }

                // Close connection
                client.close().get();
                logger.info("Disconnected from TransferQueue server");

            } catch (const std::exception& e) {
                logger.error("Error: {}", e.what());
            }
        });
    });
}
