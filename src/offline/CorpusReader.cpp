#include "search/offline/CorpusReader.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"
#include "search/common/Log.h"

namespace search {
namespace {

namespace fs = std::filesystem;

// ------------------------------ NDJSON (BigQuery) ------------------------------
//
// 每行一个 JSON 对象，至少包含三个字符串字段：
//   {"url": "...", "title": "...", "content": "..."}
// 其它字段（id / source / category / publish_time ...）会被忽略。
class JsonlCorpusReader final : public CorpusReader {
public:
    explicit JsonlCorpusReader(const fs::path& file) : path_(file), buffer_(1U << 20) {
        input_.rdbuf()->pubsetbuf(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
        input_.open(file, std::ios::binary);
        if (!input_) throw std::runtime_error("cannot open corpus file: " + file.string());
    }

    ~JsonlCorpusReader() override {
        if (bad_lines_ > 0) log::warn(path_.string(), ": skipped ", bad_lines_, " malformed line(s)");
    }

    bool next(RawDocument& out) override {
        while (std::getline(input_, line_)) {
            if (line_.empty() || line_ == "\r") continue;

            // 第三个参数 allow_exceptions=false：脏数据返回 discarded 而不是抛异常，
            // 一行坏数据不应该让跑了几个小时的建库任务整体失败
            auto json = nlohmann::json::parse(line_, nullptr, false);
            if (!json.is_object()) {
                ++bad_lines_;
                continue;
            }
            out.url = takeString(json, "url");
            out.title = takeString(json, "title");
            out.content = takeString(json, "content");
            return true;
        }
        return false;
    }

private:
    // 把 JSON 里的字符串“搬”出来而不是拷贝（正文可能有几十 KB）
    static std::string takeString(nlohmann::json& json, const char* key) {
        const auto it = json.find(key);
        if (it == json.end() || !it->is_string()) return {};
        return std::move(it->get_ref<std::string&>());
    }

    fs::path path_;
    std::vector<char> buffer_;  // 先于 input_ 声明
    std::ifstream input_;
    std::string line_;
    std::size_t bad_lines_ = 0;
};

// ----------------------------------- RSS XML -----------------------------------
class RssCorpusReader final : public CorpusReader {
public:
    explicit RssCorpusReader(const fs::path& file) {
        if (document_.LoadFile(file.string().c_str()) != tinyxml2::XML_SUCCESS) {
            log::warn("skip unparsable XML: ", file.string());
            return;
        }
        if (const auto* rss = document_.FirstChildElement("rss")) {
            if (const auto* channel = rss->FirstChildElement("channel")) {
                item_ = channel->FirstChildElement("item");
            }
        }
    }

    bool next(RawDocument& out) override {
        while (item_ != nullptr) {
            const tinyxml2::XMLElement* current = item_;
            item_ = item_->NextSiblingElement("item");

            out.url = childText(current, "link");
            if (out.url.compare(0, 4, "http") != 0) continue;  // 没有合法链接的条目没有检索价值
            out.title = childText(current, "title");
            out.content = childText(current, "description");
            if (out.content.empty()) out.content = childText(current, "content");
            return true;
        }
        return false;
    }

private:
    // GetText() 在元素为空（<title/>）时返回 nullptr。
    // 原实现直接把它赋给 std::string —— 用空指针构造 string 是未定义行为，遇到空标签就崩溃。
    static std::string childText(const tinyxml2::XMLElement* parent, const char* name) {
        const auto* child = parent->FirstChildElement(name);
        const char* text = child != nullptr ? child->GetText() : nullptr;
        return text != nullptr ? std::string(text) : std::string();
    }

    tinyxml2::XMLDocument document_;
    const tinyxml2::XMLElement* item_ = nullptr;
};

// ---------------------------------- 目录扫描 ----------------------------------
enum class CorpusFormat { Unknown, Jsonl, Rss };

CorpusFormat detectFormat(const fs::path& file) {
    std::string extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".jsonl" || extension == ".ndjson" || extension == ".json") return CorpusFormat::Jsonl;
    if (extension == ".xml") return CorpusFormat::Rss;
    return CorpusFormat::Unknown;
}

class DirectoryCorpusReader final : public CorpusReader {
public:
    explicit DirectoryCorpusReader(const fs::path& directory) {
        if (!fs::is_directory(directory)) throw std::runtime_error("corpus_dir is not a directory: " + directory.string());
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() && detectFormat(entry.path()) != CorpusFormat::Unknown) {
                files_.push_back(entry.path());
            }
        }
        if (files_.empty()) throw std::runtime_error("no corpus files (*.jsonl/*.json/*.xml) under: " + directory.string());
        std::sort(files_.begin(), files_.end());  // 固定处理顺序 → 每次建库得到完全相同的 doc_id
        log::info("corpus: ", files_.size(), " file(s) under ", directory.string());
    }

    bool next(RawDocument& out) override {
        while (true) {
            if (current_ && current_->next(out)) return true;
            if (next_file_ == files_.size()) return false;
            current_ = openCorpusFile(files_[next_file_++]);  // 上一个文件的 reader 在此被释放
        }
    }

private:
    std::vector<fs::path> files_;
    std::size_t next_file_ = 0;
    std::unique_ptr<CorpusReader> current_;
};

}  // namespace

std::unique_ptr<CorpusReader> openCorpusFile(const fs::path& file) {
    switch (detectFormat(file)) {
        case CorpusFormat::Jsonl: return std::make_unique<JsonlCorpusReader>(file);
        case CorpusFormat::Rss: return std::make_unique<RssCorpusReader>(file);
        case CorpusFormat::Unknown: break;
    }
    throw std::runtime_error("unsupported corpus file type: " + file.string());
}

std::unique_ptr<CorpusReader> openCorpusDirectory(const fs::path& directory) {
    return std::make_unique<DirectoryCorpusReader>(directory);
}

}  // namespace search
