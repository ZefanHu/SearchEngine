#pragma once
// 网页检索：分词 → 查倒排 → 求交集 → 余弦相似度打分 → Top-K。
//
// 原实现的问题（按严重程度）：
//   1. `auto invert_index_lib = Dictionary::getInstance().getInvertIndexLib();` —— 少写一个 &，
//      每次查询把整个倒排索引深拷贝两遍，`auto offset_lib = ...` 又把偏移库拷贝一遍；
//   2. 每个查询词都把整条倒排链倒进一个 std::set<int>，再用 set_intersection 输出到 vector、
//      再转回 set……大量红黑树节点的分配与释放；
//   3. 查询向量的 IDF 用写死的 N=1、DF=1 计算，所有词权重相同，等于没有 IDF；
//   4. “余弦相似度”只在查询词的维度上计算文档模长，结果偏向短文档，且单词查询时所有文档得分都是 1；
//   5. 特化 std::less<pair<int,double>> 且对相等元素返回 true，违反严格弱序 → priority_queue 行为未定义。
//
// 现在：倒排链是 mmap 上的有序数组，求交集 = 以最短链为驱动、在其它链上做带游标的二分查找，
// 全程零拷贝、零堆分配（除了结果集本身）。文档权重在离线阶段已按“整篇文档”做过 L2 归一化，
// 所以 Σ q_i·d_i 就是严格意义上的余弦相似度。

#include <cstddef>
#include <string>
#include <vector>

#include "search/common/Tokenizer.h"
#include "search/index/InvertedIndex.h"

namespace search {

struct SearchHit {
    DocId doc_id = 0;
    float score = 0.0F;
};

struct SearchResult {
    std::vector<SearchHit> hits;     // 按得分从高到低
    std::vector<std::string> terms;  // 查询被切成的词项（去重后），用于生成摘要
};

class WebSearcher {
public:
    // 只保存引用：tokenizer 与 index 的生命周期由上层（SearchService）保证长于本对象
    WebSearcher(const Tokenizer& tokenizer, const InvertedIndex& index) : tokenizer_(tokenizer), index_(index) {}

    // 线程安全（const 且不修改任何共享状态）。
    // 先按 AND 语义检索（文档必须包含全部查询词）；若没有任何文档同时包含所有词，退化为 OR 语义。
    SearchResult search(const std::string& query, std::size_t top_k) const;

private:
    const Tokenizer& tokenizer_;
    const InvertedIndex& index_;
};

}  // namespace search
