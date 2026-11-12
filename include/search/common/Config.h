#pragma once
// 强类型配置。
//
// 原项目的 Configuration 是一个“全局单例 + 字符串 map”：
//   - 任何类都能在任何地方 getInstance()，依赖关系完全隐藏，无法单测；
//   - key 拼错只会在运行时得到空串（原代码里就出现过 key 拼写错误）；
//   - 路径相对于“当前工作目录”，必须 cd 到 bin/ 下才能运行。
// 现在改为：启动时一次性解析成普通结构体，通过构造函数显式传给需要它的对象（依赖注入）。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace search {

struct Config {
    // ---- 分词器 ----
    std::filesystem::path jieba_dict;
    std::filesystem::path jieba_hmm;
    std::filesystem::path jieba_user_dict;
    std::filesystem::path jieba_idf;
    std::filesystem::path jieba_stop_words;
    std::vector<std::filesystem::path> stop_word_files;

    // ---- 离线建库 ----
    std::filesystem::path corpus_dir;
    std::filesystem::path index_dir;
    unsigned build_threads = 0;          // 0 = hardware_concurrency
    std::size_t build_batch_size = 2048;
    int simhash_max_distance = 3;
    std::uint32_t suggest_min_df = 2;
    // 标题加权：给标题里的每个词额外加上“正文向量模长 × 该百分比”的权重（0 = 标题与正文同等对待），见 IndexBuilder.h
    std::uint32_t title_weight_percent = 100;

    // ---- 在线服务 ----
    // 默认只监听本机：对外访问应经由 Caddy/Nginx 等反向代理（负责 HTTPS、限流），而不是直接暴露服务端口
    std::string listen_host = "127.0.0.1";
    std::uint16_t port = 9000;
    // 同时保持的最大连接数。Workflow 默认只有 2000，压测 C10K 时必须调大，
    // 同时还要调大进程的文件描述符上限（ulimit -n / systemd 的 LimitNOFILE）
    std::size_t max_connections = 2000;
    std::size_t search_top_k = 10;
    std::size_t suggest_top_k = 10;
    std::size_t snippet_bytes = 300;
    std::size_t cache_capacity = 20000;
    std::size_t cache_shards = 16;
    // 启动后用后台线程把索引文件顺序读进页缓存。默认关闭：索引比空闲内存大时，预读会挤掉别的进程的页缓存
    bool preload_index = false;
    std::filesystem::path web_index;

    // ---- 由 index_dir 派生出的四个索引文件 ----
    std::filesystem::path pageDataPath() const { return index_dir / "pages.dat"; }
    std::filesystem::path pageOffsetPath() const { return index_dir / "pages.idx"; }
    std::filesystem::path invertedIndexPath() const { return index_dir / "index.dat"; }
    std::filesystem::path suggestDictPath() const { return index_dir / "dict.dat"; }

    // 解析配置文件；缺少必填项、数值非法时抛 std::runtime_error（fail fast，而不是带着空串继续跑）
    static Config load(const std::filesystem::path& file);
};

}  // namespace search
