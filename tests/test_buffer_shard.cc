#include <gtest/gtest.h>

#include "transfer_queue/server/buffer_shard.h"
#include "transferqueue.pb.h"
#include <string>
#include <vector>

using transfer_queue::BufferShard;
using transfer_queue::TransferQueueConfig;

// ============================================================================
// 辅助函数
// ============================================================================

/// 创建一条测试 Trajectory
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

/// 创建默认配置
static TransferQueueConfig make_config(int32_t group_size = 4, bool uid_dedup = true) {
    TransferQueueConfig cfg;
    cfg.group_size = group_size;
    cfg.uid_dedup = uid_dedup;
    cfg.group_timeout_seconds = 300;
    return cfg;
}

// ============================================================================
// 基础写入和读取
// ============================================================================

TEST(BufferShard, BasicWriteAndRead) {
    auto cfg = make_config(2);
    BufferShard shard(cfg);

    // 写入 2 条同 instance 的轨迹 → group 就绪
    EXPECT_TRUE(shard.write(make_trajectory("uid-1", "inst-A")).get());
    EXPECT_TRUE(shard.write(make_trajectory("uid-2", "inst-A")).get());

    EXPECT_TRUE(shard.has_ready_groups().get());

    auto groups = shard.read_ready_groups().get();
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0]->instance_id(), "inst-A");
    EXPECT_EQ(groups[0]->trajectories_size(), 2);
    EXPECT_TRUE(groups[0]->is_complete());
}

TEST(BufferShard, GroupNotReadyUntilFull) {
    auto cfg = make_config(3);
    BufferShard shard(cfg);

    // 只写 2 条 → group 未满
    EXPECT_TRUE(shard.write(make_trajectory("uid-1", "inst-A")).get());
    EXPECT_TRUE(shard.write(make_trajectory("uid-2", "inst-A")).get());

    EXPECT_FALSE(shard.has_ready_groups().get());

    auto groups = shard.read_ready_groups().get();
    EXPECT_TRUE(groups.empty());
}

TEST(BufferShard, MultipleGroups) {
    auto cfg = make_config(2);
    BufferShard shard(cfg);

    // 两个不同 instance，各写满
    shard.write(make_trajectory("uid-1", "inst-A")).get();
    shard.write(make_trajectory("uid-2", "inst-A")).get();
    shard.write(make_trajectory("uid-3", "inst-B")).get();
    shard.write(make_trajectory("uid-4", "inst-B")).get();

    auto groups = shard.read_ready_groups().get();
    EXPECT_EQ(groups.size(), 2u);
}

TEST(BufferShard, ReadConsumesGroups) {
    auto cfg = make_config(1);
    BufferShard shard(cfg);

    shard.write(make_trajectory("uid-1", "inst-A")).get();

    // 第一次读取
    auto groups = shard.read_ready_groups().get();
    ASSERT_EQ(groups.size(), 1u);

    // 消费后应该没有就绪组
    EXPECT_FALSE(shard.has_ready_groups().get());
    auto groups2 = shard.read_ready_groups().get();
    EXPECT_TRUE(groups2.empty());
}

// ============================================================================
// 去重
// ============================================================================

TEST(BufferShard, DedupRejectsDuplicate) {
    auto cfg = make_config(4);
    BufferShard shard(cfg);

    EXPECT_TRUE(shard.write(make_trajectory("uid-1", "inst-A")).get());
    EXPECT_FALSE(shard.write(make_trajectory("uid-1", "inst-A")).get());  // 重复

    // 只写入了 1 条
    auto status = shard.get_status().get();
    EXPECT_EQ(status->total_trajectories(), 1);
}

TEST(BufferShard, DedupDisabled) {
    auto cfg = make_config(4, /*uid_dedup=*/false);
    BufferShard shard(cfg);

    // 去重关闭，两次都应该成功
    EXPECT_TRUE(shard.write(make_trajectory("uid-1", "inst-A")).get());
    EXPECT_TRUE(shard.write(make_trajectory("uid-1", "inst-A")).get());

    auto status = shard.get_status().get();
    EXPECT_EQ(status->total_trajectories(), 2);
}

TEST(BufferShard, DedupAcrossInstances) {
    auto cfg = make_config(4);
    BufferShard shard(cfg);

    // 同一个 uid 不同 instance → 也应去重
    EXPECT_TRUE(shard.write(make_trajectory("uid-1", "inst-A")).get());
    EXPECT_FALSE(shard.write(make_trajectory("uid-1", "inst-B")).get());
}

// ============================================================================
// 批量写入
// ============================================================================

TEST(BufferShard, BatchWrite) {
    auto cfg = make_config(2);
    BufferShard shard(cfg);

    std::vector<transferqueue::Trajectory> batch;
    batch.push_back(make_trajectory("uid-1", "inst-A"));
    batch.push_back(make_trajectory("uid-2", "inst-A"));
    batch.push_back(make_trajectory("uid-3", "inst-B"));

    auto written = shard.batch_write(std::move(batch)).get();
    EXPECT_EQ(written, 3);

    // inst-A 应该就绪，inst-B 未满
    EXPECT_TRUE(shard.has_ready_groups().get());
    auto groups = shard.read_ready_groups().get();
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0]->instance_id(), "inst-A");
}

TEST(BufferShard, BatchWriteWithDuplicates) {
    auto cfg = make_config(4);
    BufferShard shard(cfg);

    std::vector<transferqueue::Trajectory> batch;
    batch.push_back(make_trajectory("uid-1", "inst-A"));
    batch.push_back(make_trajectory("uid-2", "inst-A"));
    batch.push_back(make_trajectory("uid-1", "inst-A"));  // 重复
    batch.push_back(make_trajectory("uid-3", "inst-A"));

    auto written = shard.batch_write(std::move(batch)).get();
    EXPECT_EQ(written, 3);  // uid-1 第二次被去重
}

// ============================================================================
// 管理操作
// ============================================================================

TEST(BufferShard, DeleteInstance) {
    auto cfg = make_config(4);
    BufferShard shard(cfg);

    shard.write(make_trajectory("uid-1", "inst-A")).get();
    shard.write(make_trajectory("uid-2", "inst-B")).get();

    shard.delete_instance("inst-A").get();

    auto meta = shard.get_meta_info().get();
    EXPECT_EQ(meta->num_groups(), 1);  // 只剩 inst-B
}

TEST(BufferShard, Reset) {
    auto cfg = make_config(2);
    BufferShard shard(cfg);

    shard.write(make_trajectory("uid-1", "inst-A")).get();
    shard.write(make_trajectory("uid-2", "inst-A")).get();

    shard.reset().get();

    auto status = shard.get_status().get();
    EXPECT_EQ(status->total_trajectories(), 0);
    EXPECT_EQ(status->total_consumed(), 0);
    EXPECT_EQ(status->pending_groups(), 0);
    EXPECT_EQ(status->incomplete_groups(), 0);

    // reset 后应该可以写入相同 uid
    EXPECT_TRUE(shard.write(make_trajectory("uid-1", "inst-A")).get());
}

// ============================================================================
// 状态和元信息
// ============================================================================

TEST(BufferShard, GetStatus) {
    auto cfg = make_config(2);
    BufferShard shard(cfg);

    shard.write(make_trajectory("uid-1", "inst-A")).get();
    shard.write(make_trajectory("uid-2", "inst-A")).get();  // inst-A 已满
    shard.write(make_trajectory("uid-3", "inst-B")).get();  // inst-B 未满

    auto status = shard.get_status().get();
    EXPECT_EQ(status->total_trajectories(), 3);
    EXPECT_EQ(status->total_consumed(), 0);
    EXPECT_EQ(status->pending_groups(), 1);      // inst-A
    EXPECT_EQ(status->incomplete_groups(), 1);   // inst-B
    EXPECT_GT(status->memory_usage_bytes(), 0);

    // 消费一组
    shard.read_ready_groups().get();

    auto status2 = shard.get_status().get();
    EXPECT_EQ(status2->total_consumed(), 2);
    EXPECT_EQ(status2->pending_groups(), 0);
}

TEST(BufferShard, GetMetaInfo) {
    auto cfg = make_config(2);
    BufferShard shard(cfg);

    shard.write(make_trajectory("uid-1", "inst-A", 1.0)).get();
    shard.write(make_trajectory("uid-2", "inst-A", 3.0)).get();
    shard.write(make_trajectory("uid-3", "inst-B", 2.0)).get();

    auto meta = shard.get_meta_info().get();
    EXPECT_EQ(meta->total_samples(), 3);
    EXPECT_EQ(meta->num_groups(), 2);
    EXPECT_DOUBLE_EQ(meta->avg_group_size(), 1.5);  // 3 / 2
    EXPECT_DOUBLE_EQ(meta->avg_reward(), 2.0);      // (1+3+2) / 3
    // inst-A is complete
    EXPECT_EQ(meta->finished_group_ids_size(), 1);
}

// ============================================================================
// 组超时
// ============================================================================

TEST(BufferShard, GroupTimeout) {
    auto cfg = make_config(10);
    cfg.group_timeout_seconds = 0;  // 立即超时
    BufferShard shard(cfg);

    shard.write(make_trajectory("uid-1", "inst-A")).get();

    // 此时 group 未满但配置了 0 秒超时
    EXPECT_FALSE(shard.has_ready_groups().get());  // 尚未检查超时

    shard.check_group_timeouts().get();

    // 超时后应标记为 ready
    EXPECT_TRUE(shard.has_ready_groups().get());

    auto groups = shard.read_ready_groups().get();
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0]->trajectories_size(), 1);
}

// ============================================================================
// Max groups 限制
// ============================================================================

TEST(BufferShard, MaxGroupsLimit) {
    auto cfg = make_config(1);
    BufferShard shard(cfg);

    // 3 个独立 group，每个 group_size=1
    shard.write(make_trajectory("uid-1", "inst-A")).get();
    shard.write(make_trajectory("uid-2", "inst-B")).get();
    shard.write(make_trajectory("uid-3", "inst-C")).get();

    // 限制只读 2 组
    auto groups = shard.read_ready_groups(2).get();
    EXPECT_EQ(groups.size(), 2u);

    // 还剩 1 组
    EXPECT_TRUE(shard.has_ready_groups().get());
    auto remaining = shard.read_ready_groups().get();
    EXPECT_EQ(remaining.size(), 1u);
}

// ============================================================================
// 配置更新
// ============================================================================

TEST(BufferShard, UpdateConfig) {
    auto cfg = make_config(4);
    BufferShard shard(cfg);

    shard.write(make_trajectory("uid-1", "inst-A")).get();
    shard.write(make_trajectory("uid-2", "inst-A")).get();

    // 此时 group_size=4，2 条不够 → 不就绪
    EXPECT_FALSE(shard.has_ready_groups().get());

    // 动态改为 group_size=2
    auto cfg2 = make_config(2);
    shard.update_config(cfg2);

    // 但已有组的 group_size 不会变（组创建时已固定），新组会用新配置
    shard.write(make_trajectory("uid-3", "inst-B")).get();
    shard.write(make_trajectory("uid-4", "inst-B")).get();

    // inst-B 用新的 group_size=2 → 就绪
    EXPECT_TRUE(shard.has_ready_groups().get());
}
