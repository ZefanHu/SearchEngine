#pragma once
// 倒排索引构建器（离线端）。
//
// 用法：按 doc_id 递增的顺序调用 addDocument()，最后调用 write() 落盘。
//
// 权重公式（与原项目保持一致）：
//   IDF(t)   = log2(N / (DF(t) + 1) + 1)
//   w(t, d)  = TF(t, d) * IDF(t)
//   w'(t, d) = w(t, d) / sqrt(Σ w(·, d)^2)          ← 按文档做 L2 归一化
// 归一化后，查询向量与文档向量的点积就是余弦相似度。
//
// 原实现的问题：
//   - 中间结果是 vector<vector<pair<string,double>>>：每个 (文档, 词) 对都存一份词的字符串拷贝；
//   - 倒排链用 std::set<pair<int,double>>：每条 posting 一个红黑树节点（约 48 字节 + malloc 开销）；
//   - storeIntoInvertIndexLib() 被误放在“逐文档循环”里，每处理一篇文档就把整个索引重写一遍；
//   - 落盘为文本，double 用默认 6 位精度输出，在线端再逐行解析回来。
// 现在每条 posting 在内存中固定 8 字节，词项字符串全局只存一份。

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "search/index/IndexFormat.h"
#include "search/offline/Document.h"

namespace search {

class IndexBuilder {
public:
    // 加入一篇文档的词频表。doc_id 必须严格递增——这保证了每条倒排链天然按 doc_id 有序，
    // 在线端做多路求交时可以直接二分/归并，不需要任何排序。
    void addDocument(DocId doc_id, const TermFrequencies& terms);

    std::uint32_t docCount() const noexcept { return doc_count_; }
    std::size_t termCount() const noexcept { return postings_.size(); }
    std::uint64_t postingCount() const noexcept { return posting_count_; }

    // 计算 TF-IDF 并写出二进制倒排索引（布局见 IndexFormat.h）
    void write(const std::filesystem::path& index_path) const;

    // 写出关键词联想词典：每行 "词 词频"，按字节序升序。只收录 DF >= min_df 的“像词的词”
    // 返回写出的词条数。
    std::size_t writeSuggestDict(const std::filesystem::path& dict_path, std::uint32_t min_df) const;

private:
    struct RawPosting {
        DocId doc_id;
        std::uint32_t term_frequency;
    };

    // 返回所有 term_id，按词项字节序升序排列
    std::vector<std::uint32_t> sortedTermIds() const;

    std::unordered_map<std::string, std::uint32_t> term_ids_;  // 词项 → term_id
    std::vector<const std::string*> terms_;                    // term_id → 词项（指向 term_ids_ 的 key）
    std::vector<std::vector<RawPosting>> postings_;            // term_id → 倒排链
    std::uint32_t doc_count_ = 0;
    std::uint64_t posting_count_ = 0;
};

}  // namespace search
