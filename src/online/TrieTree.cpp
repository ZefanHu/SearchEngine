#include "search/online/TrieTree.h"

#include <algorithm>
#include <stdexcept>

namespace search {

TrieTree::TrieTree() : nodes_(1) {}

void TrieTree::insert(std::u32string_view word, std::uint32_t word_id) {
    // 注意：下面会对 nodes_ 做 emplace_back，可能触发扩容使所有 Node& 引用失效，
    // 所以全程只保存下标 current，每次都通过 nodes_[current] 重新取引用。
    std::uint32_t current = 0;
    nodes_[0].range.end = word_id + 1;

    for (const char32_t label : word) {
        const auto& edges = nodes_[current].edges;
        std::uint32_t child = 0;
        if (!edges.empty() && edges.back().label == label) {
            child = edges.back().child;  // 有序插入时，可复用的子节点只可能是最后一个
        } else {
            if (!edges.empty() && edges.back().label > label) {
                throw std::logic_error("TrieTree::insert: words must be inserted in sorted order");
            }
            child = static_cast<std::uint32_t>(nodes_.size());
            nodes_.emplace_back();
            nodes_[child].range.begin = word_id;
            nodes_[current].edges.push_back({label, child});
        }
        nodes_[child].range.end = word_id + 1;
        current = child;
    }
}

TrieTree::Range TrieTree::findPrefix(std::u32string_view prefix) const noexcept {
    std::uint32_t current = 0;
    for (const char32_t label : prefix) {
        const auto& edges = nodes_[current].edges;
        const auto it = std::lower_bound(edges.begin(), edges.end(), label,
                                         [](const Edge& edge, char32_t target) { return edge.label < target; });
        if (it == edges.end() || it->label != label) return {};
        current = it->child;
    }
    return nodes_[current].range;
}

}  // namespace search
