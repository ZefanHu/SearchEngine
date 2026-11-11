#pragma once
// 前缀树（Trie），以 Unicode 码点为边。
//
// 关键设计：词典按字节序排好序后依次插入（UTF-8 的字节序 == 码点序）。于是
//   “以某个前缀开头的所有词” 在有序词典中一定是一段连续区间 [begin, end)。
// 每个节点只需要记录这个区间的两个端点（8 字节），就等价于记录了整棵子树里的全部词。
//
// 原实现在每个节点里放一个 std::set<int>，保存“经过该节点的所有词的下标”：
//   - 一个长度为 L 的词会被存进 L 个节点的 set，内存是 O(总字符数) 个红黑树节点；
//   - searchPrefix() 按值返回 std::set<int>，查一个单字前缀就要深拷贝上万个节点；
//   - 节点用裸 new/delete 管理，子节点用 unordered_map<string, TrieNode*>（每条边一个堆上的 string）。
// 现在：所有节点放在一个 vector 里用下标互相引用（无裸指针、析构零成本、缓存友好），
// 子节点是有序的小数组（顺序插入时永远追加在末尾，查找用二分）。

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace search {

class TrieTree {
public:
    // 词典下标的左闭右开区间
    struct Range {
        std::uint32_t begin = 0;
        std::uint32_t end = 0;

        std::uint32_t size() const noexcept { return end - begin; }
        bool empty() const noexcept { return begin == end; }
    };

    TrieTree();

    // 插入第 word_id 个词。要求：word_id 从 0 开始逐个递增，且词按码点序升序、不重复。
    void insert(std::u32string_view word, std::uint32_t word_id);

    // 返回以 prefix 开头的词的下标区间；没有则返回空区间。复杂度 O(m·log σ)，
    // m 为前缀字符数，σ 为单个节点的子节点数——与词典大小无关。
    Range findPrefix(std::u32string_view prefix) const noexcept;

    std::size_t nodeCount() const noexcept { return nodes_.size(); }

private:
    struct Edge {
        char32_t label;
        std::uint32_t child;  // 子节点在 nodes_ 中的下标
    };
    struct Node {
        std::vector<Edge> edges;  // 按 label 升序
        Range range;
    };

    std::vector<Node> nodes_;  // nodes_[0] 是根
};

}  // namespace search
