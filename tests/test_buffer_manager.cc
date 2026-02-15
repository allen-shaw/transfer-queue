#include <gtest/gtest.h>

#include <seastar/core/app-template.hh>
#include <seastar/core/thread.hh>
#include <seastar/core/sleep.hh>

#include "transfer_queue/server/buffer_manager.h"
#include "transferqueue.pb.h"
#include <string>
#include <vector>

using transfer_queue::BufferManager;
using transfer_queue::TransferQueueConfig;

// ============================================================================
// 辅助函数
// ============================================================================

static transferqueue::Trajectory make_trajectory(
    const std::string& uid,
    const std::string& instance_id,
    double reward = 1.0) {
    transferqueue::Trajectory t;
    t.set_uid(uid);
    t.set_instance_id(instance_id);
    t.set_reward(reward);
    auto* msg = t.add_messages();
    msg->set_role("user");
    msg->set_content("test prompt for " + uid);
    return t;
}

static TransferQueueConfig make_config(int32_t group_size = 4, bool uid_dedup = true) {
    TransferQueueConfig cfg;
    cfg.group_size = group_size;
    cfg.uid_dedup = uid_dedup;
    cfg.group_timeout_seconds = 300;
    return cfg;
}

// ============================================================================
// 基础生命周期
// ============================================================================

TEST(BufferManager, StartStop) {
    auto cfg = make_config();
    BufferManager manager(cfg);

    manager.start().get();
    manager.stop().get();
}

// ============================================================================
// 写入和读取
// ============================================================================

TEST(BufferManager, WriteAndRead) {
    auto cfg = make_config(2);
    BufferManager manager(cfg);
    manager.start().get();

    // 写入 2 条同 instance → group 就绪
    EXPECT_TRUE(manager.write(make_trajectory("uid-1", "inst-A")).get());
    EXPECT_TRUE(manager.write(make_trajectory("uid-2", "inst-A")).get());

    EXPECT_TRUE(manager.has_ready_groups().get());

    auto groups = manager.read_ready_groups().get();
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0]->instance_id(), "inst-A");
    EXPECT_EQ(groups[0]->trajectories_size(), 2);

    manager.stop().get();
}

TEST(BufferManager, GroupNotReadyUntilFull) {
    auto cfg = make_config(3);
    BufferManager manager(cfg);
    manager.start().get();

    manager.write(make_trajectory("uid-1", "inst-A")).get();
    manager.write(make_trajectory("uid-2", "inst-A")).get();

    EXPECT_FALSE(manager.has_ready_groups().get());

    auto groups = manager.read_ready_groups().get();
    EXPECT_TRUE(groups.empty());

    manager.stop().get();
}

// ============================================================================
// 去重
// ============================================================================

TEST(BufferManager, DedupRejectsDuplicate) {
    auto cfg = make_config(4);
    BufferManager manager(cfg);
    manager.start().get();

    EXPECT_TRUE(manager.write(make_trajectory("uid-1", "inst-A")).get());
    // 同一 uid → 路由到同一 shard → 去重
    EXPECT_FALSE(manager.write(make_trajectory("uid-1", "inst-A")).get());

    auto status = manager.get_status().get();
    EXPECT_EQ(status->total_trajectories(), 1);

    manager.stop().get();
}

TEST(BufferManager, DedupDisabled) {
    auto cfg = make_config(4, /*uid_dedup=*/false);
    BufferManager manager(cfg);
    manager.start().get();

    EXPECT_TRUE(manager.write(make_trajectory("uid-1", "inst-A")).get());
    EXPECT_TRUE(manager.write(make_trajectory("uid-1", "inst-A")).get());

    auto status = manager.get_status().get();
    EXPECT_EQ(status->total_trajectories(), 2);

    manager.stop().get();
}

// ============================================================================
// 批量写入
// ============================================================================

TEST(BufferManager, BatchWrite) {
    auto cfg = make_config(2);
    BufferManager manager(cfg);
    manager.start().get();

    std::vector<transferqueue::Trajectory> batch;
    batch.push_back(make_trajectory("uid-1", "inst-A"));
    batch.push_back(make_trajectory("uid-2", "inst-A"));
    batch.push_back(make_trajectory("uid-3", "inst-B"));

    auto written = manager.batch_write(std::move(batch)).get();
    EXPECT_EQ(written, 3);

    // inst-A 应该就绪（2条 = group_size）
    EXPECT_TRUE(manager.has_ready_groups().get());
    auto groups = manager.read_ready_groups().get();
    // inst-A 满了，inst-B 未满 → 只有 1 个就绪组
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0]->instance_id(), "inst-A");

    manager.stop().get();
}

TEST(BufferManager, BatchWriteWithDuplicates) {
    auto cfg = make_config(4);
    BufferManager manager(cfg);
    manager.start().get();

    std::vector<transferqueue::Trajectory> batch;
    batch.push_back(make_trajectory("uid-1", "inst-A"));
    batch.push_back(make_trajectory("uid-2", "inst-A"));
    batch.push_back(make_trajectory("uid-1", "inst-A"));  // 重复
    batch.push_back(make_trajectory("uid-3", "inst-A"));

    auto written = manager.batch_write(std::move(batch)).get();
    EXPECT_EQ(written, 3);  // uid-1 第二次被去重

    manager.stop().get();
}

// ============================================================================
// 跨 shard 聚合读取
// ============================================================================

TEST(BufferManager, ReadAggregatesFromAllShards) {
    auto cfg = make_config(1);  // group_size=1 → 每条即就绪
    BufferManager manager(cfg);
    manager.start().get();

    // 写入多个不同 instance（可能分布到不同 shard）
    manager.write(make_trajectory("uid-1", "inst-A")).get();
    manager.write(make_trajectory("uid-2", "inst-B")).get();
    manager.write(make_trajectory("uid-3", "inst-C")).get();

    auto groups = manager.read_ready_groups().get();
    EXPECT_EQ(groups.size(), 3u);

    // 消费后没有就绪组
    EXPECT_FALSE(manager.has_ready_groups().get());

    manager.stop().get();
}

TEST(BufferManager, MaxGroupsLimit) {
    auto cfg = make_config(1);
    BufferManager manager(cfg);
    manager.start().get();

    // 用同一个 instance_id 保证所有轨迹路由到同一个 shard
    // group_size=1 → 每条轨迹创建一个独立 group（因为同 instance 的第二条会进入
    // 新 group 而不是已满的旧 group）
    // 改为使用不同 instance 但验证 per-shard 限制
    manager.write(make_trajectory("uid-1", "inst-A")).get();
    manager.write(make_trajectory("uid-2", "inst-B")).get();
    manager.write(make_trajectory("uid-3", "inst-C")).get();

    // read_ready_groups(2) 在每个 shard 上限制 2 组
    // 总返回量 <= 2 * shard_count，manager 层截断到 2
    auto groups = manager.read_ready_groups(2).get();
    EXPECT_LE(static_cast<int32_t>(groups.size()), 2);
    EXPECT_GE(static_cast<int32_t>(groups.size()), 1);

    manager.stop().get();
}

// ============================================================================
// 管理操作
// ============================================================================

TEST(BufferManager, DeleteInstance) {
    auto cfg = make_config(4);
    BufferManager manager(cfg);
    manager.start().get();

    manager.write(make_trajectory("uid-1", "inst-A")).get();
    manager.write(make_trajectory("uid-2", "inst-B")).get();

    manager.delete_instance("inst-A").get();

    auto status = manager.get_status().get();
    // inst-A 被删除后，只剩 inst-B 的 1 条
    EXPECT_EQ(status->pending_groups() + status->incomplete_groups(), 1);

    manager.stop().get();
}

TEST(BufferManager, Reset) {
    auto cfg = make_config(2);
    BufferManager manager(cfg);
    manager.start().get();

    manager.write(make_trajectory("uid-1", "inst-A")).get();
    manager.write(make_trajectory("uid-2", "inst-A")).get();

    manager.reset().get();

    auto status = manager.get_status().get();
    EXPECT_EQ(status->total_trajectories(), 0);
    EXPECT_EQ(status->total_consumed(), 0);

    // reset 后可以写入相同 uid
    EXPECT_TRUE(manager.write(make_trajectory("uid-1", "inst-A")).get());

    manager.stop().get();
}

// ============================================================================
// 状态聚合
// ============================================================================

TEST(BufferManager, GetStatusAggregates) {
    auto cfg = make_config(2);
    BufferManager manager(cfg);
    manager.start().get();

    manager.write(make_trajectory("uid-1", "inst-A")).get();
    manager.write(make_trajectory("uid-2", "inst-A")).get();  // inst-A 就绪
    manager.write(make_trajectory("uid-3", "inst-B")).get();

    auto status = manager.get_status().get();
    EXPECT_EQ(status->total_trajectories(), 3);
    EXPECT_EQ(status->pending_groups(), 1);
    EXPECT_GT(status->memory_usage_bytes(), 0);

    // 消费后 consumed 增加
    manager.read_ready_groups().get();
    auto status2 = manager.get_status().get();
    EXPECT_EQ(status2->total_consumed(), 2);

    manager.stop().get();
}

// ============================================================================
// 阻塞读取
// ============================================================================

TEST(BufferManager, BlockingReadTimeout) {
    auto cfg = make_config(100);  // 很大的 group_size → 不会就绪
    BufferManager manager(cfg);
    manager.start().get();

    manager.write(make_trajectory("uid-1", "inst-A")).get();

    // 阻塞读取 50ms 超时 → 返回空
    auto groups = manager.read_ready_groups_blocking(10, 50).get();
    EXPECT_TRUE(groups.empty());

    manager.stop().get();
}

TEST(BufferManager, BlockingReadImmediate) {
    auto cfg = make_config(1);  // group_size=1 → 立即就绪
    BufferManager manager(cfg);
    manager.start().get();

    manager.write(make_trajectory("uid-1", "inst-A")).get();

    // 有数据时应立即返回
    auto groups = manager.read_ready_groups_blocking(10, 1000).get();
    ASSERT_EQ(groups.size(), 1u);

    manager.stop().get();
}

// ============================================================================
// 配置更新
// ============================================================================

TEST(BufferManager, UpdateConfig) {
    auto cfg = make_config(4);
    BufferManager manager(cfg);
    manager.start().get();

    manager.write(make_trajectory("uid-1", "inst-A")).get();
    manager.write(make_trajectory("uid-2", "inst-A")).get();
    EXPECT_FALSE(manager.has_ready_groups().get());  // group_size=4

    // 动态改为 group_size=2
    transferqueue::ConfigRequest config_req;
    config_req.set_group_size(2);
    manager.update_config(config_req).get();

    // 已有组不受影响，但新组使用新配置
    manager.write(make_trajectory("uid-3", "inst-B")).get();
    manager.write(make_trajectory("uid-4", "inst-B")).get();
    EXPECT_TRUE(manager.has_ready_groups().get());  // inst-B 就绪

    manager.stop().get();
}

// ============================================================================
// 自定义 main()：启动 Seastar reactor 后运行 GTest
// ============================================================================

int main(int argc, char** argv) {
    // 分离 GTest 和 Seastar 参数
    // GTest 参数: --gtest_*
    // Seastar 参数: -- 之后的（如 --smp 2）
    testing::InitGoogleTest(&argc, argv);

    seastar::app_template app;
    int ret = 0;

    app.run(argc, argv, [&ret] {
        return seastar::async([&ret] {
            ret = RUN_ALL_TESTS();
        });
    });

    return ret;
}
