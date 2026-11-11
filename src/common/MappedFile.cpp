#include "search/common/MappedFile.h"

#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace search {
namespace {

[[noreturn]] void fail(const std::filesystem::path& path, const char* what) {
    throw std::runtime_error(std::string(what) + ": " + path.string());
}

}  // namespace

#ifdef _WIN32

MappedFile::MappedFile(const std::filesystem::path& path) {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) fail(path, "cannot open file");

    LARGE_INTEGER file_size;
    if (!::GetFileSizeEx(file, &file_size)) {
        ::CloseHandle(file);
        fail(path, "cannot stat file");
    }
    file_handle_ = file;
    size_ = static_cast<std::size_t>(file_size.QuadPart);
    if (size_ == 0) return;  // 空文件无法映射，保持 data_ == nullptr

    HANDLE mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        reset();
        fail(path, "cannot create file mapping");
    }
    mapping_handle_ = mapping;

    data_ = static_cast<const char*>(::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (data_ == nullptr) {
        reset();
        fail(path, "cannot map file");
    }
}

void MappedFile::reset() noexcept {
    if (data_ != nullptr) ::UnmapViewOfFile(data_);
    if (mapping_handle_ != nullptr) ::CloseHandle(mapping_handle_);
    if (file_handle_ != nullptr) ::CloseHandle(file_handle_);
    data_ = nullptr;
    size_ = 0;
    mapping_handle_ = nullptr;
    file_handle_ = nullptr;
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      file_handle_(std::exchange(other.file_handle_, nullptr)),
      mapping_handle_(std::exchange(other.mapping_handle_, nullptr)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        reset();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        file_handle_ = std::exchange(other.file_handle_, nullptr);
        mapping_handle_ = std::exchange(other.mapping_handle_, nullptr);
    }
    return *this;
}

#else  // POSIX

MappedFile::MappedFile(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) fail(path, "cannot open file");

    struct stat info {};
    if (::fstat(fd, &info) != 0) {
        ::close(fd);
        fail(path, "cannot stat file");
    }
    size_ = static_cast<std::size_t>(info.st_size);

    if (size_ > 0) {
        void* address = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (address == MAP_FAILED) {
            ::close(fd);
            size_ = 0;
            fail(path, "cannot mmap file");
        }
        data_ = static_cast<const char*>(address);
    }
    ::close(fd);  // 映射建立后 fd 即可关闭，映射本身仍然有效
}

void MappedFile::reset() noexcept {
    if (data_ != nullptr) ::munmap(const_cast<char*>(data_), size_);
    data_ = nullptr;
    size_ = 0;
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        reset();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

#endif

MappedFile::~MappedFile() { reset(); }

}  // namespace search
