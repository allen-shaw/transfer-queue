#include <gtest/gtest.h>

#include "transfer_queue/server/dedup_filter.h"
#include <string>

using transfer_queue::DedupFilter;

// ============================================================================
// 基础功能
// ============================================================================

TEST(DedupFilter, EmptyFilter) {
    DedupFilter f(100);
    EXPECT_EQ(f.size(), 0u);
    EXPECT_FALSE(f.contains("uid-1"));
}

TEST(DedupFilter, InsertAndContains) {
    DedupFilter f(100);

    EXPECT_TRUE(f.insert("uid-1"));  // 新插入 → true
    EXPECT_EQ(f.size(), 1u);
    EXPECT_TRUE(f.contains("uid-1"));
    EXPECT_FALSE(f.contains("uid-2"));
}

TEST(DedupFilter, DuplicateInsertReturnsFalse) {
    DedupFilter f(100);

    EXPECT_TRUE(f.insert("uid-1"));   // 第一次 → true
    EXPECT_FALSE(f.insert("uid-1"));  // 重复 → false
    EXPECT_EQ(f.size(), 1u);          // 数量不变
}

TEST(DedupFilter, MultipleUids) {
    DedupFilter f(1000);

    for (int i = 0; i < 100; ++i) {
        std::string uid = "uid-" + std::to_string(i);
        EXPECT_TRUE(f.insert(uid));
    }
    EXPECT_EQ(f.size(), 100u);

    for (int i = 0; i < 100; ++i) {
        std::string uid = "uid-" + std::to_string(i);
        EXPECT_TRUE(f.contains(uid));
    }

    // 再次插入全部失败
    for (int i = 0; i < 100; ++i) {
        std::string uid = "uid-" + std::to_string(i);
        EXPECT_FALSE(f.insert(uid));
    }
    EXPECT_EQ(f.size(), 100u);
}

// ============================================================================
// Clear
// ============================================================================

TEST(DedupFilter, ClearResetsEverything) {
    DedupFilter f(100);

    f.insert("uid-1");
    f.insert("uid-2");
    f.insert("uid-3");
    EXPECT_EQ(f.size(), 3u);

    f.clear();
    EXPECT_EQ(f.size(), 0u);
    EXPECT_FALSE(f.contains("uid-1"));
    EXPECT_FALSE(f.contains("uid-2"));
    EXPECT_FALSE(f.contains("uid-3"));

    // 清空后可以重新插入
    EXPECT_TRUE(f.insert("uid-1"));
    EXPECT_EQ(f.size(), 1u);
}

// ============================================================================
// LRU 淘汰
// ============================================================================

TEST(DedupFilter, LruEvictionRemovesOldest) {
    DedupFilter f(5);  // 最多 5 个

    // 插入 5 个，按顺序 uid-0 .. uid-4
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(f.insert("uid-" + std::to_string(i)));
    }
    EXPECT_EQ(f.size(), 5u);

    // 插入第 6 个 → 淘汰最旧的 uid-0
    EXPECT_TRUE(f.insert("uid-5"));
    EXPECT_EQ(f.size(), 5u);
    EXPECT_FALSE(f.contains("uid-0"));  // uid-0 被淘汰
    EXPECT_TRUE(f.contains("uid-1"));   // uid-1 还在
    EXPECT_TRUE(f.contains("uid-5"));   // uid-5 刚插入
}

TEST(DedupFilter, LruTouchOnDuplicatePreventsEviction) {
    DedupFilter f(3);

    f.insert("uid-A");
    f.insert("uid-B");
    f.insert("uid-C");
    // 顺序：C(最新) → B → A(最旧)

    // touch uid-A（重复插入会提升 LRU 位置）
    EXPECT_FALSE(f.insert("uid-A"));
    // 顺序变为：A(最新) → C → B(最旧)

    // 插入新的 → 淘汰 B（最旧）
    EXPECT_TRUE(f.insert("uid-D"));
    EXPECT_EQ(f.size(), 3u);
    EXPECT_TRUE(f.contains("uid-A"));   // 被 touch 过，还在
    EXPECT_FALSE(f.contains("uid-B"));  // 被淘汰
    EXPECT_TRUE(f.contains("uid-C"));
    EXPECT_TRUE(f.contains("uid-D"));
}

TEST(DedupFilter, LruEvictionSequential) {
    DedupFilter f(3);

    f.insert("uid-1");
    f.insert("uid-2");
    f.insert("uid-3");

    // 连续插入新的，逐个淘汰旧的
    f.insert("uid-4");
    EXPECT_FALSE(f.contains("uid-1"));
    EXPECT_TRUE(f.contains("uid-2"));

    f.insert("uid-5");
    EXPECT_FALSE(f.contains("uid-2"));
    EXPECT_TRUE(f.contains("uid-3"));

    f.insert("uid-6");
    EXPECT_FALSE(f.contains("uid-3"));
    EXPECT_TRUE(f.contains("uid-4"));
    EXPECT_TRUE(f.contains("uid-5"));
    EXPECT_TRUE(f.contains("uid-6"));
    EXPECT_EQ(f.size(), 3u);
}

// ============================================================================
// Bloom filter 正确性
// ============================================================================

TEST(DedupFilter, BloomFilterNoFalseNegatives) {
    // Bloom filter 不能有 false negatives：如果 uid 在集合中，
    // bloom 必须返回 true
    DedupFilter f(10000);

    for (int i = 0; i < 1000; ++i) {
        f.insert("uid-" + std::to_string(i));
    }

    // 所有已插入的 uid 都必须被 contains 检测到
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(f.contains("uid-" + std::to_string(i)));
    }
}

TEST(DedupFilter, BloomFilterFalsePositiveRateBounded) {
    // 测试 bloom filter 的误判率不超过预期
    const size_t n = 10000;
    const double target_fpr = 0.01;
    DedupFilter f(n, target_fpr);

    // 插入 n 个元素
    for (size_t i = 0; i < n; ++i) {
        f.insert("present-" + std::to_string(i));
    }

    // 测试不在集合中的元素的误判率
    int false_positives = 0;
    const int test_count = 100000;
    for (int i = 0; i < test_count; ++i) {
        // 使用不同前缀确保不在集合中
        if (f.contains("absent-" + std::to_string(i))) {
            ++false_positives;
        }
    }

    double actual_fpr = static_cast<double>(false_positives) / test_count;
    // 允许 3x 余量（统计波动）
    EXPECT_LT(actual_fpr, target_fpr * 3.0);
    // 理论误判率应该接近目标
    EXPECT_LT(f.current_false_positive_rate(), target_fpr * 1.5);
}

TEST(DedupFilter, BloomRebuildAfterEviction) {
    // 淘汰后 bloom filter 重建，已淘汰的 uid 不应该在 bloom 中被命中
    DedupFilter f(100);

    // 插入 100 个
    for (int i = 0; i < 100; ++i) {
        f.insert("uid-" + std::to_string(i));
    }

    // 插入 100 个新的，淘汰所有旧的
    for (int i = 100; i < 200; ++i) {
        f.insert("uid-" + std::to_string(i));
    }

    // 旧的不应该在精确集合中
    for (int i = 0; i < 100; ++i) {
        EXPECT_FALSE(f.contains("uid-" + std::to_string(i)));
    }

    // 新的应该都在
    for (int i = 100; i < 200; ++i) {
        EXPECT_TRUE(f.contains("uid-" + std::to_string(i)));
    }
}

// ============================================================================
// 参数校验
// ============================================================================

TEST(DedupFilter, BloomParametersReasonable) {
    DedupFilter f(10000, 0.01);
    EXPECT_GT(f.bloom_bit_count(), 0u);
    EXPECT_GT(f.bloom_hash_count(), 0u);
    EXPECT_LE(f.bloom_hash_count(), 20u);  // k 不应过大
}

TEST(DedupFilter, TinyCapacity) {
    DedupFilter f(1);  // 极端情况：容量 = 1

    f.insert("uid-1");
    EXPECT_EQ(f.size(), 1u);
    EXPECT_TRUE(f.contains("uid-1"));

    f.insert("uid-2");
    EXPECT_EQ(f.size(), 1u);
    EXPECT_FALSE(f.contains("uid-1"));
    EXPECT_TRUE(f.contains("uid-2"));
}

// ============================================================================
// 边界情况
// ============================================================================

TEST(DedupFilter, EmptyUid) {
    DedupFilter f(100);
    EXPECT_TRUE(f.insert(""));
    EXPECT_TRUE(f.contains(""));
    EXPECT_FALSE(f.insert(""));
}

TEST(DedupFilter, LongUid) {
    DedupFilter f(100);
    std::string long_uid(1024, 'x');
    EXPECT_TRUE(f.insert(long_uid));
    EXPECT_TRUE(f.contains(long_uid));
    EXPECT_FALSE(f.insert(long_uid));
}

TEST(DedupFilter, SimilarUids) {
    DedupFilter f(100);

    EXPECT_TRUE(f.insert("uuid-abc-123"));
    EXPECT_TRUE(f.insert("uuid-abc-124"));
    EXPECT_TRUE(f.insert("uuid-abd-123"));

    EXPECT_TRUE(f.contains("uuid-abc-123"));
    EXPECT_TRUE(f.contains("uuid-abc-124"));
    EXPECT_TRUE(f.contains("uuid-abd-123"));

    EXPECT_FALSE(f.contains("uuid-abc-125"));
}
