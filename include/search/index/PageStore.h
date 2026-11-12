#pragma once
// 网页库：按 doc_id 存取 (url, title, content)。
//
//   PageStoreWriter —— 离线端，顺序追加写
//   PageStore       —— 在线端，mmap 只读，get() 返回指向映射内存的 string_view（零拷贝）
//
// 原实现的问题：
//   - 用伪 XML 文本（<docid>..</docid><title>..）存储，读取时靠 string::find 找标签，
//     正文里只要出现 "</content>" 就会解析错位，且每次都要重新扫描整条记录；
//   - 偏移库写入的是 {起始, 结束} 而读取端按 {起始, 长度} 解释，两端语义不一致；
//   - 偏移用 int 存，文件超过 2GB 即溢出；
//   - 每次查询都重新 open 文件 + seek + read + 构造 string + 再拷贝成 WebPage。
// 现在用“长度前缀”的二进制记录：不需要转义、不需要扫描、O(1) 定位。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "search/common/BinaryWriter.h"
#include "search/common/MappedFile.h"
#include "search/index/IndexFormat.h"

namespace search {

struct PageView {
    std::string_view url;
    std::string_view title;
    std::string_view content;
};

class PageStoreWriter {
public:
    PageStoreWriter(const std::filesystem::path& data_path, const std::filesystem::path& offset_path);

    // 追加一篇文档，返回分配给它的 doc_id（从 0 递增）
    DocId append(std::string_view url, std::string_view title, std::string_view content);

    std::uint32_t size() const noexcept { return count_; }
    void close();

private:
    BinaryWriter data_;
    BinaryWriter offsets_;
    std::uint32_t count_ = 0;
};

class PageStore {
public:
    PageStore(const std::filesystem::path& data_path, const std::filesystem::path& offset_path);

    // 把两个文件顺序读进页缓存（阻塞，供后台线程调用），见 MappedFile::preload
    bool preload(const std::atomic<bool>& cancel) const noexcept { return offsets_.preload(cancel) && data_.preload(cancel); }

    std::size_t size() const noexcept { return count_; }

    // 返回的 string_view 指向 mmap 内存，生命周期与 PageStore 对象相同。
    // doc_id 越界或文件损坏时抛 std::out_of_range，而不是越界读内存。
    PageView get(DocId doc_id) const;

private:
    MappedFile data_;
    MappedFile offsets_;
    std::size_t count_ = 0;
};

}  // namespace search
