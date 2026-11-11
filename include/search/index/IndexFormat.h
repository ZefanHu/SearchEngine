#pragma once
// 磁盘文件的二进制布局定义。离线写入端与在线读取端共用这一份定义，是两者之间唯一的“契约”。
//
// 设计原则：所有结构体都是定长、可平凡拷贝、按 8 字节对齐，文件内容可以被 mmap 后“原地当数组用”，
// 不需要任何反序列化步骤。字节序为小端（x86-64 / ARM64 均满足）。
//
// ┌──────────────────────── index.dat（倒排索引）────────────────────────┐
// │ IndexHeader            32 字节                                        │
// │ TermEntry[term_count]  每项 24 字节，按词项字节序升序 → 可二分查找     │
// │ Posting[posting_count] 每项  8 字节，同一词项内按 doc_id 升序          │
// │ 字符串池               所有词项的字节首尾相接（无分隔符）               │
// └──────────────────────────────────────────────────────────────────────┘
//
// ┌──────────── pages.dat（网页库）────────────┐  ┌──── pages.idx（偏移库）────┐
// │ PageFileHeader   8 字节                    │  │ uint64 offset[doc_count]   │
// │ 记录 0: PageRecordHeader + url+title+正文  │  │ 第 i 项 = 文档 i 的记录在   │
// │ 记录 1: ...                                │  │ pages.dat 中的起始字节偏移  │
// └────────────────────────────────────────────┘  └────────────────────────────┘

#include <cstdint>
#include <type_traits>

namespace search {

using DocId = std::uint32_t;  // 文档编号从 0 开始连续分配；uint32 可容纳 42 亿篇

inline constexpr std::uint32_t kIndexMagic = 0x58444953;  // 小端下即 "SIDX"
inline constexpr std::uint32_t kPageMagic = 0x47415053;   // 小端下即 "SPAG"
inline constexpr std::uint32_t kFormatVersion = 1;

struct IndexHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t doc_count;
    std::uint32_t term_count;
    std::uint64_t posting_count;
    std::uint64_t string_pool_bytes;
};

struct TermEntry {
    std::uint64_t posting_begin;  // 该词项的倒排链在 Posting 数组中的起始下标
    std::uint32_t posting_count;  // 倒排链长度，即文档频率 DF
    std::uint32_t term_offset;    // 词项文本在字符串池中的偏移
    std::uint32_t term_length;
    float idf;
};

struct Posting {
    DocId doc_id;
    float weight;  // 已做 L2 归一化的 TF-IDF 权重
};

struct PageFileHeader {
    std::uint32_t magic;
    std::uint32_t version;
};

struct PageRecordHeader {
    std::uint32_t url_length;
    std::uint32_t title_length;
    std::uint32_t content_length;
};

// 编译期锁死布局：任何人改动结构体导致大小变化，都会在这里编译失败，而不是悄悄写出不兼容的文件
static_assert(sizeof(IndexHeader) == 32 && std::is_trivially_copyable_v<IndexHeader>);
static_assert(sizeof(TermEntry) == 24 && std::is_trivially_copyable_v<TermEntry>);
static_assert(sizeof(Posting) == 8 && std::is_trivially_copyable_v<Posting>);
static_assert(sizeof(PageFileHeader) == 8 && sizeof(PageRecordHeader) == 12);

}  // namespace search
