#include "search/offline/IndexBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "search/common/BinaryWriter.h"
#include "search/common/Utf8.h"

namespace search {
namespace {

// 联想词典只收录“像词的词”：纯字母（>=2 个）或纯汉字，长度不超过 20 个字符。
// 数字串、带标点的 token（"32022130."）不适合作为搜索建议。
bool isSuggestable(std::string_view term) {
    std::size_t length = 0;
    bool all_ascii = true;
    for (std::size_t pos = 0; pos < term.size(); ++length) {
        const char32_t cp = utf8::decodeNext(term, pos);
        const bool letter = cp >= U'a' && cp <= U'z';
        const bool han = cp >= 0x4E00 && cp <= 0x9FFF;
        if (!letter && !han) return false;
        all_ascii = all_ascii && letter;
    }
    return length <= 20 && length >= (all_ascii ? 2U : 1U);
}

}  // namespace

void IndexBuilder::addDocument(DocId doc_id, const TermFrequencies& terms) {
    if (doc_id < doc_count_) throw std::logic_error("IndexBuilder::addDocument: doc_id must be increasing");
    doc_count_ = doc_id + 1;

    for (const auto& [term, frequency] : terms) {
        // try_emplace：一次哈希查找同时完成“查 + 不存在则插入”。
        // unordered_map 是基于节点的容器，rehash 不会移动 key，所以保存 key 的地址是安全的。
        const auto [it, inserted] = term_ids_.try_emplace(term, static_cast<std::uint32_t>(postings_.size()));
        if (inserted) {
            terms_.push_back(&it->first);
            postings_.emplace_back();
        }
        postings_[it->second].push_back({doc_id, frequency});
    }
    posting_count_ += terms.size();
}

std::vector<std::uint32_t> IndexBuilder::sortedTermIds() const {
    std::vector<std::uint32_t> order(terms_.size());
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(),
              [this](std::uint32_t lhs, std::uint32_t rhs) { return *terms_[lhs] < *terms_[rhs]; });
    return order;
}

void IndexBuilder::write(const std::filesystem::path& index_path) const {
    const std::size_t term_count = terms_.size();
    if (term_count > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("too many terms");

    // 1. 每个词项的 IDF
    std::vector<float> idf(term_count);
    for (std::size_t id = 0; id < term_count; ++id) {
        const double df = static_cast<double>(postings_[id].size());
        idf[id] = static_cast<float>(std::log2(static_cast<double>(doc_count_) / (df + 1.0) + 1.0));
    }

    // 2. 每篇文档的向量模长（L2 范数）。用 double 累加，避免长文档上 float 的累积误差
    std::vector<double> norms(doc_count_, 0.0);
    for (std::size_t id = 0; id < term_count; ++id) {
        for (const RawPosting& posting : postings_[id]) {
            const double weight = static_cast<double>(posting.term_frequency) * idf[id];
            norms[posting.doc_id] += weight * weight;
        }
    }
    for (double& norm : norms) norm = std::sqrt(norm);

    // 3. 按词项字节序依次写出：文件头 → 词典表 → 倒排链 → 字符串池
    const std::vector<std::uint32_t> order = sortedTermIds();
    BinaryWriter writer(index_path);

    std::uint64_t string_pool_bytes = 0;
    for (const std::string* term : terms_) string_pool_bytes += term->size();
    if (string_pool_bytes > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("string pool exceeds 4GB");

    writer.writePod(IndexHeader{kIndexMagic, kFormatVersion, doc_count_, static_cast<std::uint32_t>(term_count),
                                posting_count_, string_pool_bytes});

    std::uint64_t posting_begin = 0;
    std::uint32_t term_offset = 0;
    for (const std::uint32_t id : order) {
        const auto count = static_cast<std::uint32_t>(postings_[id].size());
        const auto length = static_cast<std::uint32_t>(terms_[id]->size());
        writer.writePod(TermEntry{posting_begin, count, term_offset, length, idf[id]});
        posting_begin += count;
        term_offset += length;
    }

    std::vector<Posting> block;  // 复用同一块缓冲，整条倒排链一次 write
    for (const std::uint32_t id : order) {
        block.clear();
        block.reserve(postings_[id].size());
        for (const RawPosting& posting : postings_[id]) {
            const double norm = norms[posting.doc_id];
            const double weight = norm > 0.0 ? posting.term_frequency * idf[id] / norm : 0.0;
            block.push_back({posting.doc_id, static_cast<float>(weight)});
        }
        writer.write(block.data(), block.size() * sizeof(Posting));
    }

    for (const std::uint32_t id : order) writer.write(*terms_[id]);
    writer.close();
}

std::size_t IndexBuilder::writeSuggestDict(const std::filesystem::path& dict_path, std::uint32_t min_df) const {
    BinaryWriter writer(dict_path);
    std::size_t written = 0;
    for (const std::uint32_t id : sortedTermIds()) {
        const auto& list = postings_[id];
        if (list.size() < min_df || !isSuggestable(*terms_[id])) continue;

        std::uint64_t total_frequency = 0;
        for (const RawPosting& posting : list) total_frequency += posting.term_frequency;

        writer.write(*terms_[id]);
        writer.write(" " + std::to_string(total_frequency) + "\n");
        ++written;
    }
    writer.close();
    return written;
}

}  // namespace search
