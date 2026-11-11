#pragma once
// 倒排索引（在线只读端）：mmap index.dat，在映射内存上直接二分查找词项。
//
// 原实现是 unordered_map<string, unordered_map<int, double>>：
//   - 启动时逐行文本解析，百万文档级别需要几分钟和数 GB 堆内存（每个节点一次 malloc）；
//   - 更严重的是查询代码里写了 `auto lib = getInvertIndexLib();`（少了 &），
//     每次查询都把整个倒排索引深拷贝两遍。
// 现在整个索引是一块连续的只读内存：零解析、零堆分配、可被所有线程无锁共享。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "search/common/MappedFile.h"
#include "search/index/IndexFormat.h"

namespace search {

// 一条倒排链的只读视图（C++20 之后可直接用 std::span<const Posting>）
class PostingList {
public:
    PostingList() = default;
    PostingList(const Posting* data, std::size_t size) : data_(data), size_(size) {}

    const Posting* begin() const noexcept { return data_; }
    const Posting* end() const noexcept { return data_ + size_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

private:
    const Posting* data_ = nullptr;
    std::size_t size_ = 0;
};

struct TermInfo {
    float idf = 0.0F;
    PostingList postings;
};

class InvertedIndex {
public:
    explicit InvertedIndex(const std::filesystem::path& path);  // 文件损坏时抛 std::runtime_error

    std::uint32_t docCount() const noexcept { return header_->doc_count; }
    std::uint32_t termCount() const noexcept { return header_->term_count; }
    std::uint64_t postingCount() const noexcept { return header_->posting_count; }

    // O(log T) 二分查找，T 为词项总数；找不到返回 std::nullopt
    std::optional<TermInfo> find(std::string_view term) const noexcept;

private:
    std::string_view termText(const TermEntry& entry) const noexcept {
        return {string_pool_ + entry.term_offset, entry.term_length};
    }

    MappedFile file_;
    // 以下指针都指向 file_ 的映射内存内部，不拥有资源
    const IndexHeader* header_ = nullptr;
    const TermEntry* terms_ = nullptr;
    const Posting* postings_ = nullptr;
    const char* string_pool_ = nullptr;
};

}  // namespace search
