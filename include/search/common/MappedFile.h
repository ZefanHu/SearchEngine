#pragma once
// 只读内存映射文件（RAII）。
//
// 这是整个在线服务“零拷贝”的基础：索引文件不再被逐行解析进堆内存，而是直接映射进
// 进程地址空间，由操作系统的 page cache 按需换入。好处：
//   1. 启动几乎不耗时（原实现要把 7MB 文本逐行 istringstream 解析成 unordered_map 嵌套）；
//   2. 内存占用由内核管理，百万级文档的索引可以大于物理内存；
//   3. 多个查询线程并发读同一块只读内存，天然线程安全、无需加锁。

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace search {

class MappedFile {
public:
    MappedFile() = default;
    explicit MappedFile(const std::filesystem::path& path);  // 失败抛 std::runtime_error
    ~MappedFile();

    // 资源独占：禁止拷贝，允许移动（Rule of Five）
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    const char* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    std::string_view view() const noexcept { return {data_, size_}; }

private:
    void reset() noexcept;

    const char* data_ = nullptr;
    std::size_t size_ = 0;
#ifdef _WIN32
    void* file_handle_ = nullptr;
    void* mapping_handle_ = nullptr;
#endif
};

}  // namespace search
