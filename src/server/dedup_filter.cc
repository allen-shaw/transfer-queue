#include "transfer_queue/server/dedup_filter.h"

namespace transfer_queue {

DedupFilter::DedupFilter(size_t expected_size, double false_positive_rate) {
    // TODO: 初始化 bloom filter
    (void)expected_size;
    (void)false_positive_rate;
}

DedupFilter::~DedupFilter() = default;

bool DedupFilter::contains(const std::string& uid) const {
    // TODO: bloom filter 快速判定 + 精确集合确认
    return exact_set_.count(uid) > 0;
}

bool DedupFilter::insert(const std::string& uid) {
    // TODO: 插入 bloom filter + 精确集合
    auto [_, inserted] = exact_set_.insert(uid);
    return inserted;
}

void DedupFilter::clear() {
    exact_set_.clear();
}

size_t DedupFilter::size() const {
    return exact_set_.size();
}

} // namespace transfer_queue
