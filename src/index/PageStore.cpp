#include "search/index/PageStore.h"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace search {

// ------------------------------ PageStoreWriter ------------------------------

PageStoreWriter::PageStoreWriter(const std::filesystem::path& data_path,
                                 const std::filesystem::path& offset_path)
    : data_(data_path), offsets_(offset_path) {
    data_.writePod(PageFileHeader{kPageMagic, kFormatVersion});
}

DocId PageStoreWriter::append(std::string_view url, std::string_view title, std::string_view content) {
    constexpr std::size_t kMaxField = std::numeric_limits<std::uint32_t>::max();
    if (url.size() > kMaxField || title.size() > kMaxField || content.size() > kMaxField) {
        throw std::length_error("page field exceeds 4GB");
    }
    offsets_.writePod<std::uint64_t>(data_.position());
    data_.writePod(PageRecordHeader{static_cast<std::uint32_t>(url.size()),
                                    static_cast<std::uint32_t>(title.size()),
                                    static_cast<std::uint32_t>(content.size())});
    data_.write(url);
    data_.write(title);
    data_.write(content);
    return count_++;
}

void PageStoreWriter::close() {
    data_.close();
    offsets_.close();
}

// --------------------------------- PageStore ---------------------------------

PageStore::PageStore(const std::filesystem::path& data_path, const std::filesystem::path& offset_path)
    : data_(data_path), offsets_(offset_path), count_(offsets_.size() / sizeof(std::uint64_t)) {
    // 偏移库是一个 uint64 数组，大小必须是 8 的整数倍；否则文件被截断或根本不是偏移库
    if (offsets_.size() % sizeof(std::uint64_t) != 0) {
        throw std::runtime_error("page offset file is truncated (size is not a multiple of 8): " + offset_path.string());
    }
    PageFileHeader header{};
    if (data_.size() < sizeof(header)) throw std::runtime_error("page store is truncated: " + data_path.string());
    std::memcpy(&header, data_.data(), sizeof(header));
    if (header.magic != kPageMagic || header.version != kFormatVersion) {
        throw std::runtime_error("not a page store file (bad magic/version): " + data_path.string());
    }
}

PageView PageStore::get(DocId doc_id) const {
    if (doc_id >= count_) throw std::out_of_range("doc_id out of range: " + std::to_string(doc_id));

    // 记录在文件里不保证对齐，所以用 memcpy 取出定长头（编译器会把它优化成一次普通读取）
    std::uint64_t offset = 0;
    std::memcpy(&offset, offsets_.data() + std::size_t{doc_id} * sizeof(offset), sizeof(offset));

    PageRecordHeader header{};
    const std::uint64_t file_size = data_.size();
    if (offset > file_size || file_size - offset < sizeof(header)) throw std::out_of_range("corrupt page offset");
    std::memcpy(&header, data_.data() + offset, sizeof(header));

    const std::uint64_t body_bytes =
        std::uint64_t{header.url_length} + header.title_length + header.content_length;
    if (file_size - offset - sizeof(header) < body_bytes) throw std::out_of_range("corrupt page record");

    const char* cursor = data_.data() + offset + sizeof(header);
    PageView page;
    page.url = {cursor, header.url_length};
    cursor += header.url_length;
    page.title = {cursor, header.title_length};
    cursor += header.title_length;
    page.content = {cursor, header.content_length};
    return page;
}

}  // namespace search
