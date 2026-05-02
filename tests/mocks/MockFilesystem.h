#pragma once
#include "interfaces/IFileSystem.h"
#include <vector>
#include <tuple>

namespace Haisos::Mocks {

class MockFilesystem : public IFileSystem {
public:
    MockFilesystem() = default;

    // Tracking
    struct OpenCall { std::string pathname; int flags; int mode; };
    std::vector<OpenCall> m_openCalls;

    struct ReadCall { int fd; size_t count; };
    std::vector<ReadCall> m_readCalls;

    struct WriteCall { int fd; size_t count; };
    std::vector<WriteCall> m_writeCalls;

    struct DirCall { std::string path; int mode; };
    std::vector<DirCall> m_mkdirCalls;
    std::vector<DirCall> m_rmdirCalls;
    std::vector<DirCall> m_chdirCalls;

    struct GetcwdCall { size_t size; };
    std::vector<GetcwdCall> m_getcwdCalls;

    struct ReadDirCall { std::string path; };
    std::vector<ReadDirCall> m_readdirCalls;

    // Return values
    int m_openReturn = 0;
    int m_closeReturn = 0;
    ssize_t m_readReturn = 0;
    ssize_t m_writeReturn = 0;
    int m_mkdirReturn = 0;
    int m_rmdirReturn = 0;
    int m_chdirReturn = 0;
    char* m_getcwdReturn = nullptr;
    std::vector<DirectoryEntry> m_readdirReturn;

    // IFileSystem overrides
    int OpenFile(const std::string& pathname, int flags) override {
        m_openCalls.push_back({pathname, flags, 0});
        return m_openReturn;
    }

    int OpenFile(const std::string& pathname, int flags, int mode) override {
        m_openCalls.push_back({pathname, flags, mode});
        return m_openReturn;
    }

    int CloseFile(int fd) override {
        m_lastCloseFd = fd;
        return m_closeReturn;
    }

    int ReadFile(int fd, void* buf, size_t count) override {
        m_readCalls.push_back({fd, count});
        if (m_readBuffer.empty()) return m_readReturn;
        size_t toCopy = std::min(count, m_readBuffer.size());
        std::memcpy(buf, m_readBuffer.data(), toCopy);
        return static_cast<int>(toCopy);
    }

    int WriteFile(int fd, const void* buf, size_t count) override {
        m_writeCalls.push_back({fd, count});
        m_writeBuffer.insert(m_writeBuffer.end(),
            static_cast<const char*>(buf),
            static_cast<const char*>(buf) + count);
        return m_writeReturn;
    }

    int CreateDirectory(const std::string& pathname, int mode) override {
        m_mkdirCalls.push_back({pathname, mode});
        return m_mkdirReturn;
    }

    int RemoveDirectory(const std::string& pathname) override {
        m_rmdirCalls.push_back({pathname, 0});
        return m_rmdirReturn;
    }

    int ChangeDirectory(const std::string& path) override {
        m_chdirCalls.push_back({path, 0});
        return m_chdirReturn;
    }

    char* GetCurrentDirectory(std::string& buf, size_t size) override {
        m_getcwdCalls.push_back({size});
        if (m_getcwdReturn) {
            buf = m_getcwdReturn;
        }
        return m_getcwdReturn;
    }

    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override {
        m_readdirCalls.push_back({path});
        return m_readdirReturn;
    }

    int GetLastCloseFd() const { return m_lastCloseFd; }
    const std::vector<char>& GetWriteBuffer() const { return m_writeBuffer; }
    void SetReadBuffer(const std::string& data) { m_readBuffer.assign(data.begin(), data.end()); }

private:
    int m_lastCloseFd = 0;
    std::vector<char> m_readBuffer;
    std::vector<char> m_writeBuffer;
};

} // namespace Haisos::Mocks
