#pragma once
// 离线管线中流转的两种数据：原始文档、词频表。

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace search {

// 从语料中读出的一篇原始文档（尚未清洗）
struct RawDocument {
    std::string url;
    std::string title;
    std::string content;
};

// 一篇文档的词频表：{词项, 出现次数}，按词项升序且不重复。
// 用排好序的 vector 代替 std::map：内存连续、无逐节点 malloc，遍历速度快一个数量级。
using TermFrequencies = std::vector<std::pair<std::string, std::uint32_t>>;

// 把分词结果统计成词频表：排序后做一次“游程计数”，O(n log n)，全程只移动字符串、不拷贝。
inline TermFrequencies countTermFrequencies(std::vector<std::string>&& tokens) {
    std::sort(tokens.begin(), tokens.end());
    TermFrequencies result;
    for (auto& token : tokens) {
        if (!result.empty() && result.back().first == token) {
            ++result.back().second;
        } else {
            result.emplace_back(std::move(token), 1U);
        }
    }
    return result;
}

}  // namespace search
