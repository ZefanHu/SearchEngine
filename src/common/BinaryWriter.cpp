#include "search/common/BinaryWriter.h"

#include <stdexcept>

namespace search {

BinaryWriter::BinaryWriter(const std::filesystem::path& path, std::size_t buffer_bytes)
    : path_(path), buffer_(buffer_bytes) {
    if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path());
    // pubsetbuf 必须在 open 之前调用才生效
    stream_.rdbuf()->pubsetbuf(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
    stream_.open(path_, std::ios::binary | std::ios::trunc);
    if (!stream_) throw std::runtime_error("cannot open file for writing: " + path_.string());
}

BinaryWriter::~BinaryWriter() {
    if (stream_.is_open()) stream_.close();  // 兜底关闭；错误只能在显式 close() 里上报
}

void BinaryWriter::write(const void* data, std::size_t bytes) {
    stream_.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    position_ += bytes;
}

void BinaryWriter::close() {
    if (!stream_.is_open()) return;
    stream_.flush();
    const bool ok = stream_.good();
    stream_.close();
    if (!ok) throw std::runtime_error("write failed (disk full?): " + path_.string());
}

}  // namespace search
