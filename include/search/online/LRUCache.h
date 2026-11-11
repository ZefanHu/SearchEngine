#pragma once
// 线程安全的 LRU 缓存，以及在其上做哈希分片的 ShardedLruCache。
//
// LruCache：  std::list（表头 = 最近使用）+ unordered_map<key, list 迭代器>，get/put 均为 O(1)。
// 分片的意义：单把 mutex 在高并发下会成为所有请求的串行点。把 key 按哈希分到 N 个互相独立的
//            LruCache，不同分片上的请求完全不竞争，锁冲突概率降为 1/N。
//
// 相比原实现的改动：
//   - get() 返回 std::optional<V>，不再需要调用方先默认构造一个 V 再传引用进来；
//   - put() 按值接收再 move 进容器：调用方传右值时全程零拷贝；
//   - 缓存的 value 建议用 shared_ptr<const T>：拷贝出锁只是一次引用计数自增，
//     不会在持锁期间复制几十 KB 的 JSON 字符串；
//   - 容量为 0 时退化为“不缓存”，而不是每次 put 后立刻淘汰自己。

#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace search {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LruCache {
public:
    explicit LruCache(std::size_t capacity) : capacity_(capacity) {}

    LruCache(const LruCache&) = delete;
    LruCache& operator=(const LruCache&) = delete;

    std::optional<Value> get(const Key& key) {
        const std::lock_guard lock(mutex_);
        const auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;
        // splice 只改几个指针，把节点挪到表头；不分配内存，也不会使任何迭代器失效
        items_.splice(items_.begin(), items_, it->second);
        return it->second->second;
    }

    void put(Key key, Value value) {
        if (capacity_ == 0) return;
        const std::lock_guard lock(mutex_);

        if (const auto it = index_.find(key); it != index_.end()) {
            it->second->second = std::move(value);
            items_.splice(items_.begin(), items_, it->second);
            return;
        }

        if (items_.size() >= capacity_) {  // 先淘汰表尾（最久未使用）再插入，size 永不超过 capacity
            index_.erase(items_.back().first);
            items_.pop_back();
        }
        items_.emplace_front(std::move(key), std::move(value));
        index_.emplace(items_.front().first, items_.begin());
    }

    std::size_t size() const {
        const std::lock_guard lock(mutex_);
        return items_.size();
    }

private:
    using Item = std::pair<Key, Value>;

    const std::size_t capacity_;
    mutable std::mutex mutex_;  // mutable：size() 是逻辑上的 const 操作，但仍需加锁
    std::list<Item> items_;
    std::unordered_map<Key, typename std::list<Item>::iterator, Hash> index_;
};

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class ShardedLruCache {
public:
    // total_capacity 平均分给各分片（向上取整）
    ShardedLruCache(std::size_t total_capacity, std::size_t shard_count) {
        if (shard_count == 0) shard_count = 1;
        const std::size_t per_shard = (total_capacity + shard_count - 1) / shard_count;
        shards_.reserve(shard_count);
        // mutex 不可移动，所以 LruCache 也不可移动，只能通过 unique_ptr 间接放进 vector
        for (std::size_t i = 0; i < shard_count; ++i) shards_.push_back(std::make_unique<Shard>(per_shard));
    }

    std::optional<Value> get(const Key& key) { return shardOf(key).get(key); }

    void put(Key key, Value value) {
        Shard& shard = shardOf(key);
        shard.put(std::move(key), std::move(value));
    }

    std::size_t size() const {
        std::size_t total = 0;
        for (const auto& shard : shards_) total += shard->size();
        return total;
    }

private:
    using Shard = LruCache<Key, Value, Hash>;

    Shard& shardOf(const Key& key) { return *shards_[Hash{}(key) % shards_.size()]; }

    std::vector<std::unique_ptr<Shard>> shards_;
};

}  // namespace search
