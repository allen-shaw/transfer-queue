#include <gtest/gtest.h>
#include <seastar/core/app-template.hh>
#include <seastar/core/thread.hh>
#include <seastar/core/sleep.hh>
#include <seastar/util/log.hh>
#include <filesystem>
#include "transfer_queue/server/disk_storage.h"
#include "transferqueue.pb.h"

using namespace transfer_queue;
using namespace transferqueue;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

TEST(DiskStorageTest, SpillAndLoad) {
    std::string dirname = "test_storage_dir";
    if (fs::exists(dirname)) fs::remove_all(dirname);

    {
        DiskStorage storage(dirname);
        storage.init().get();
        
        // Create a dummy group
        TrajectoryGroup group;
        group.set_instance_id("123");
        auto* t = group.add_trajectories();
        t->set_uid("traj_1");
        auto* msg = t->add_messages();
        msg->set_content("payload_data");

        // Spill
        uint64_t offset = storage.spill(group).get();
        EXPECT_GT(offset, 0); 

        // Load
        auto loaded_group = storage.load(offset).get();
        EXPECT_NE(loaded_group, nullptr);
        EXPECT_EQ(loaded_group->instance_id(), "123");
        EXPECT_EQ(loaded_group->trajectories_size(), 1);
        EXPECT_EQ(loaded_group->trajectories(0).uid(), "traj_1");
        EXPECT_EQ(loaded_group->trajectories(0).messages(0).content(), "payload_data");

        storage.shutdown().get();
    }
    
    if (fs::exists(dirname)) fs::remove_all(dirname);
}

TEST(DiskStorageTest, BatchSpillAndLoad) {
    std::string dirname = "test_storage_batch_dir";
    if (fs::exists(dirname)) fs::remove_all(dirname);

    {
        DiskStorage storage(dirname);
        storage.init().get();

        std::vector<TrajectoryGroup> groups;
        for (int i = 0; i < 5; ++i) {
            TrajectoryGroup g;
            g.set_instance_id(std::to_string(100 + i));
            groups.push_back(std::move(g));
        }

        auto offsets = storage.spill_batch(groups).get();
        EXPECT_EQ(offsets.size(), 5);

        auto loaded_groups = storage.load_batch(offsets).get();
        EXPECT_EQ(loaded_groups.size(), 5);

        for (int i = 0; i < 5; ++i) {
            EXPECT_EQ(loaded_groups[i]->instance_id(), std::to_string(100 + i));
        }

        storage.shutdown().get();
    }
    if (fs::exists(dirname)) fs::remove_all(dirname);
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
