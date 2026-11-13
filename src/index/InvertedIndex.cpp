#include "search/index/InvertedIndex.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace search {

InvertedIndex::InvertedIndex(const std::filesystem::path& path) : file_(path) {
    const auto corrupt = [&path](const char* reason) {
        return std::runtime_error(std::string("invalid index file (") + reason + "): " + path.string());
    };

    if (file_.size() < sizeof(IndexHeader)) throw corrupt("truncated header");

    // mmap 返回的地址按页对齐，文件各区段又都按 8 字节对齐，所以这里的指针转换满足对齐要求
    header_ = reinterpret_cast<const IndexHeader*>(file_.data());
    if (header_->magic != kIndexMagic) throw corrupt("bad magic");
    if (header_->version != kFormatVersion) throw corrupt("unsupported version");

    // 先整体校验区段大小，再逐项校验词典表，保证之后的查询绝不会读到映射区之外
    const std::uint64_t terms_bytes = std::uint64_t{header_->term_count} * sizeof(TermEntry);
    const std::uint64_t remaining = file_.size() - sizeof(IndexHeader);
    if (terms_bytes > remaining) throw corrupt("truncated term table");
    if (header_->posting_count > (remaining - terms_bytes) / sizeof(Posting)) throw corrupt("truncated postings");
    const std::uint64_t postings_bytes = header_->posting_count * sizeof(Posting);
    if (header_->string_pool_bytes != remaining - terms_bytes - postings_bytes) throw corrupt("size mismatch");

    terms_ = reinterpret_cast<const TermEntry*>(file_.data() + sizeof(IndexHeader));
    postings_ = reinterpret_cast<const Posting*>(file_.data() + sizeof(IndexHeader) + terms_bytes);
    string_pool_ = file_.data() + sizeof(IndexHeader) + terms_bytes + postings_bytes;

    for (std::uint32_t i = 0; i < header_->term_count; ++i) {
        const TermEntry& entry = terms_[i];
        if (entry.posting_begin > header_->posting_count ||
            entry.posting_count > header_->posting_count - entry.posting_begin ||
            entry.term_offset > header_->string_pool_bytes ||
            entry.term_length > header_->string_pool_bytes - entry.term_offset) {
            throw corrupt("term entry out of range");
        }
        // find() 在词典表上做二分查找，前提是词项严格升序。乱序的表不会崩溃，但会“查不到明明存在的词”，
        // 这种错误比崩溃更难发现，所以在加载时就拒绝。上面已经确认两项都不越界，这里可以放心读字符串池
        if (i > 0 && !(termText(terms_[i - 1]) < termText(entry))) throw corrupt("term table is not sorted");
    }
}

std::optional<TermInfo> InvertedIndex::find(std::string_view term) const noexcept {
    const TermEntry* const end = terms_ + header_->term_count;
    const TermEntry* const it = std::lower_bound(
        terms_, end, term,
        [this](const TermEntry& entry, std::string_view target) { return termText(entry) < target; });
    if (it == end || termText(*it) != term) return std::nullopt;
    return TermInfo{it->idf, PostingList{postings_ + it->posting_begin, it->posting_count}};
}

}  // namespace search
