#pragma once
#include "interfaces/IFileSystemService.h"
#include <cstring>
#include <map>
#include <optional>
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
    std::vector<DirCall> m_unlinkCalls;


    struct ReadDirCall { std::string path; };
    std::vector<ReadDirCall> m_readdirCalls;

    // Return values
    int m_openReturn = 0;
    int m_closeReturn = 0;
    ssize_t m_readReturn = 0;
    ssize_t m_writeReturn = 0;
    int m_mkdirReturn = 0;
    int m_rmdirReturn = 0;
    int m_unlinkReturn = 0;
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

    ssize_t ReadFile(int fd, void* buf, size_t count) override {
        m_readCalls.push_back({fd, count});
        if (m_readBuffer.empty()) return m_readReturn;
        size_t toCopy = std::min(count, m_readBuffer.size());
        std::memcpy(buf, m_readBuffer.data(), toCopy);
        return static_cast<ssize_t>(toCopy);
    }

    ssize_t WriteFile(int fd, const void* buf, size_t count) override {
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

    int RemoveFile(const std::string& pathname) override {
        m_unlinkCalls.push_back({pathname, 0});
        return m_unlinkReturn;
    }

    // The mock records calls rather than routing, so mounting is a no-op here.
    void Mount(const std::string&, std::shared_ptr<IFileSystem>) override {}
    void Unmount(const std::string&) override {}

    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override {
        m_readdirCalls.push_back({path});
        return m_readdirReturn;
    }

    int Stat(const std::string& path, FileStatus& out) override {
        m_statCalls.push_back({path});
        if (m_statReturn == 0) {
            out = m_statResult;
        }
        return m_statReturn;
    }
    std::vector<ReadDirCall> m_statCalls;
    int m_statReturn = -1;
    FileStatus m_statResult;

    // Builtins are recorded like any other state here, not enforced.
    int AddBuiltinCommand(const std::string& path, const std::string& builtinName) override {
        return m_builtins.emplace(path, builtinName).second ? 0 : -1;
    }
    int RemoveBuiltinCommand(const std::string& path) override {
        return m_builtins.erase(path) > 0 ? 0 : -1;
    }
    std::optional<std::string> IsBuiltinCommand(const std::string& path) override {
        auto it = m_builtins.find(path);
        return it == m_builtins.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    std::map<std::string, std::string> m_builtins;

    int GetLastCloseFd() const { return m_lastCloseFd; }
    const std::vector<char>& GetWriteBuffer() const { return m_writeBuffer; }
    void SetReadBuffer(const std::string& data) { m_readBuffer.assign(data.begin(), data.end()); }

private:
    int m_lastCloseFd = 0;
    std::vector<char> m_readBuffer;
    std::vector<char> m_writeBuffer;
};

} // namespace Haisos::Mocks
