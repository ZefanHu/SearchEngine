#pragma once
// 带大缓冲区的顺序二进制写入器。
//
// 原实现用 ofstream << 文本，逐条 ostringstream 拼接后再写，还在循环里反复 flush 进度条。
// 这里统一成：1MB 用户态缓冲 + 顺序 write，百万级文档写盘时系统调用次数从“每条一次”降到“每 MB 一次”。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <type_traits>
#include <vector>

namespace search {

class BinaryWriter {
public:
    explicit BinaryWriter(const std::filesystem::path& path, std::size_t buffer_bytes = 1U << 20);
    ~BinaryWriter();

    BinaryWriter(const BinaryWriter&) = delete;
    BinaryWriter& operator=(const BinaryWriter&) = delete;

    void write(const void* data, std::size_t bytes);
    void write(std::string_view text) { write(text.data(), text.size()); }

    // 只允许写“可平凡拷贝”的类型（POD），编译期拦住误把 std::string 之类对象直接按字节写盘
    template <typename T>
    void writePod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "writePod requires a trivially copyable type");
        write(&value, sizeof(T));
    }

    std::uint64_t position() const noexcept { return position_; }

    // 刷盘并检查错误。析构函数不能抛异常，所以写完后必须显式 close() 才能发现“磁盘已满”之类的失败。
    void close();

private:
    std::filesystem::path path_;
    std::vector<char> buffer_;  // 必须先于 stream_ 声明：stream_ 借用这块内存做缓冲
    std::ofstream stream_;
    std::uint64_t position_ = 0;
};

}  // namespace search
