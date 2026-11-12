#pragma once
// 只读内存映射文件（RAII）。
//
// 这是整个在线服务“零拷贝”的基础：索引文件不再被逐行解析进堆内存，而是直接映射进
// 进程地址空间，由操作系统的 page cache 按需换入。好处：
//   1. 启动几乎不耗时（原实现要把 7MB 文本逐行 istringstream 解析成 unordered_map 嵌套）；
//   2. 内存占用由内核管理，百万级文档的索引可以大于物理内存；
//   3. 多个查询线程并发读同一块只读内存，天然线程安全、无需加锁。
//
// 代价是“启动快”把读盘推迟到了查询时：页缓存是冷的时候，第一批查询每访问一个新页都要等一次磁盘。
// 实测百万文档的索引上，冷启动后未命中查询的 p50 是 130 ms，而数据都在内存里时是 0.4 ms（见 docs/BENCHMARK.md 6.5）。
// preload() 用来把这笔开销从查询路径上挪走：它把整个文件顺序读进页缓存，由调用方放在后台线程里执行。
//
// 为什么不是“对整个文件调用一次 madvise(MADV_WILLNEED)”？因为实测它几乎不起作用：
// Linux 把单次预读的总量封顶在设备的 max(read_ahead_kb, max_sectors_kb)（测试机上是 4 MiB），
// 对 495 MiB 的 index.dat 调用一次只读进了 0.8%，超出的部分被悄悄忽略。
// 所以这里按 1 MiB 分块：每块先 madvise（让内核一次提交整块的读请求），再逐页读一个字节
// （缺页会等这一页真的读进来——这一步才是“保证”，不依赖任何设备相关的上限）。

#include <atomic>
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

    // 把整个文件顺序读进页缓存。会阻塞到读完为止（速度取决于磁盘），所以应当在后台线程里调用；
    // cancel 变为 true 后最多再读完当前这一块就返回。返回是否完整读完。
    bool preload(const std::atomic<bool>& cancel) const noexcept;

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
