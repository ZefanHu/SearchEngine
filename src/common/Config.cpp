#include "search/common/Config.h"

#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include "search/common/Log.h"

namespace search {
namespace {

namespace fs = std::filesystem;

std::string_view trim(std::string_view text) {
    constexpr std::string_view kBlank = " \t\r\n";
    const auto first = text.find_first_not_of(kBlank);
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(kBlank);
    return text.substr(first, last - first + 1);
}

// 把 "k = v" 文本解析成 map，并提供带类型的取值函数
class KeyValueFile {
public:
    explicit KeyValueFile(const fs::path& file) : base_dir_(fs::absolute(file).parent_path()) {
        std::ifstream input(file);
        if (!input) throw std::runtime_error("cannot open config file: " + file.string());

        std::string line;
        while (std::getline(input, line)) {
            const std::string_view text = trim(line);
            if (text.empty() || text.front() == '#') continue;
            const auto equal = text.find('=');
            if (equal == std::string_view::npos) continue;
            const auto key = trim(text.substr(0, equal));
            const auto value = trim(text.substr(equal + 1));
            if (!key.empty() && !value.empty()) values_.insert_or_assign(std::string(key), std::string(value));
        }
    }

    const std::string* find(const std::string& key) const {
        const auto it = values_.find(key);
        return it == values_.end() ? nullptr : &it->second;
    }

    const std::string& require(const std::string& key) const {
        if (const auto* value = find(key)) return *value;
        throw std::runtime_error("missing required config key: " + key);
    }

    // 相对路径以配置文件所在目录为基准
    fs::path resolve(std::string_view value) const {
        const fs::path path{std::string(value)};
        return (path.is_absolute() ? path : base_dir_ / path).lexically_normal();
    }

    fs::path path(const std::string& key) const { return resolve(require(key)); }

    std::vector<fs::path> pathList(const std::string& key) const {
        std::vector<fs::path> result;
        std::string_view rest = require(key);
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            const auto item = trim(rest.substr(0, comma));
            if (!item.empty()) result.push_back(resolve(item));
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
        return result;
    }

    // 可选的整数项：不存在则保留默认值；存在但非法/越界则报错
    template <typename Int>
    void number(const std::string& key, Int& out, Int min_value, Int max_value) const {
        const auto* value = find(key);
        if (value == nullptr) return;
        long long parsed = 0;
        const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), parsed);
        if (error != std::errc{} || end != value->data() + value->size() ||
            parsed < static_cast<long long>(min_value) || parsed > static_cast<long long>(max_value)) {
            throw std::runtime_error("invalid value for config key '" + key + "': " + *value);
        }
        out = static_cast<Int>(parsed);
    }

private:
    fs::path base_dir_;
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace

Config Config::load(const fs::path& file) {
    const KeyValueFile kv(file);
    Config config;

    config.jieba_dict = kv.path("jieba_dict");
    config.jieba_hmm = kv.path("jieba_hmm");
    config.jieba_user_dict = kv.path("jieba_user_dict");
    config.jieba_idf = kv.path("jieba_idf");
    config.jieba_stop_words = kv.path("jieba_stop_words");
    config.stop_word_files = kv.pathList("stop_word_files");

    config.corpus_dir = kv.path("corpus_dir");
    config.index_dir = kv.path("index_dir");
    config.web_index = kv.path("web_index");

    kv.number<unsigned>("build_threads", config.build_threads, 0, 1024);
    kv.number<std::size_t>("build_batch_size", config.build_batch_size, 1, 1'000'000);
    kv.number<int>("simhash_max_distance", config.simhash_max_distance, 0, 3);
    kv.number<std::uint32_t>("suggest_min_df", config.suggest_min_df, 1, std::numeric_limits<std::int32_t>::max());

    kv.number<std::uint16_t>("port", config.port, 1, 65535);
    kv.number<std::size_t>("search_top_k", config.search_top_k, 1, 100);
    kv.number<std::size_t>("suggest_top_k", config.suggest_top_k, 1, 100);
    kv.number<std::size_t>("snippet_bytes", config.snippet_bytes, 30, 4096);
    kv.number<std::size_t>("cache_capacity", config.cache_capacity, 0, 100'000'000);
    kv.number<std::size_t>("cache_shards", config.cache_shards, 1, 1024);

    log::info("config loaded: ", fs::absolute(file).lexically_normal().string());
    return config;
}

}  // namespace search
