#include <gtest/gtest.h>
#include <seastar/core/app-template.hh>
#include <seastar/core/thread.hh>
#include <seastar/core/sleep.hh>
#include <filesystem>
#include "transfer_queue/server/buffer_shard.h"


using namespace transfer_queue;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

TEST(BufferShardSpillTest, SpillToDisk) {
    // Determine test directory
    std::string test_dir = "test_shard_spill_dir";
    if (fs::exists(test_dir)) fs::remove_all(test_dir);
    fs::create_directory(test_dir);

    // Config: extremely small memory limit to force spill
    TransferQueueConfig config;
    config.max_memory_bytes = 1024; // 1KB
    config.spill_to_disk_threshold = 0.5; // Spill at 512 bytes
    config.spdk_bdev_name = test_dir;
    config.uid_dedup = false;
    config.group_size = 1; // Mark ready immediately

    // Create shard and start
    BufferShard shard(config);
    shard.start().get();

    // Write data
    int num_groups = 10;
    // Each group: ~50-100 bytes?
    // "content" len 100.
    
    for (int i = 0; i < num_groups; ++i) {
        transferqueue::Trajectory traj;
        traj.set_uid("uid_" + std::to_string(i));
        traj.set_instance_id("inst_" + std::to_string(i));
        auto* msg = traj.add_messages();
        msg->set_content(std::string(200, 'x')); // 200 bytes payload
        
        shard.write(std::move(traj)).get();
    }

    // Check status
    auto status = shard.get_status().get();
    
    // Should have spilled some
    std::cout << "Memory: " << status->memory_usage_bytes() 
              << ", Disk: " << status->disk_usage_bytes() << std::endl;
    
    EXPECT_GT(status->disk_usage_bytes(), 0);
    EXPECT_LT(status->memory_usage_bytes(), 2048); // Should be roughly under control (depends on implementation details)

    EXPECT_EQ(status->pending_groups(), 10);

    // Read back
    auto groups = shard.read_ready_groups().get();
    EXPECT_EQ(groups.size(), 10);
    
    // Verify content
    for (auto& g : groups) {
        EXPECT_EQ(g->trajectories(0).messages(0).content().size(), 200);
    }

    // Verify status update
    status = shard.get_status().get();
    EXPECT_EQ(status->disk_usage_bytes(), 0);
    EXPECT_EQ(status->pending_groups(), 0);

    shard.stop().get();
    
    if (fs::exists(test_dir)) fs::remove_all(test_dir);
}

} // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    seastar::app_template app;
    int ret = 0;
    return app.run(argc, argv, [&] {
        return seastar::async([&] {
            ret = RUN_ALL_TESTS();
        });
    });
}
