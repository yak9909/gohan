#pragma once
#include <cassert>
#include <cstddef>
#include <map>
#include <string>

namespace CTRPluginFramework {
class MenuEntry;
struct StatusFake {
    static inline std::map<std::string, std::string> files;
    static inline std::string title, message, created;
    static inline int existsError = 0, createError = 0, openError = 0;
    static inline int writeError = 0, flushError = 0, closeError = 0;
    static inline int collisions = 0, creates = 0, closes = 0;
    static inline bool shortWrite = false, race = false, overflow = false;
    static void Reset() {
        files.clear(); title.clear(); message.clear(); created.clear();
        existsError = createError = openError = writeError = flushError = closeError = 0;
        collisions = creates = closes = 0;
        shortWrite = race = overflow = false;
    }
};
struct MessageBox {
    std::string title, message;
    MessageBox(const std::string &t, const std::string &m) : title(t), message(m) {}
    void operator()() {
        StatusFake::title = title;
        StatusFake::message = message;
    }
};
class File {
    std::string path_;
    std::size_t offset_ = 0;
  public:
    enum { WRITE = 2 };
    static int Exists(const std::string &path) {
        if (StatusFake::existsError) return StatusFake::existsError;
        if (StatusFake::collisions > 0) {
            --StatusFake::collisions;
            StatusFake::files[path] = "既存";
        }
        return StatusFake::files.count(path) ? 1 : 0;
    }
    static int Create(const std::string &path) {
        ++StatusFake::creates;
        if (StatusFake::race) StatusFake::files[path] = "競合";
        if (StatusFake::createError) return StatusFake::createError;
        if (StatusFake::files.count(path)) return -1;
        StatusFake::files[path] = "";
        StatusFake::created = path;
        return 0;
    }
    static int Open(File &file, const std::string &path, int mode) {
        assert(mode == WRITE && StatusFake::files.count(path));
        file.path_ = path;
        return StatusFake::openError;
    }
    std::string GetFullName() const { return "/luma/plugins/gohan/" + path_; }
    int Write(const void *data, std::size_t size) {
        if (StatusFake::writeError) return StatusFake::writeError;
        offset_ = size - (StatusFake::shortWrite ? 1 : 0);
        StatusFake::files[path_].assign(static_cast<const char *>(data), offset_);
        return 0;
    }
    std::size_t Tell() const { return offset_; }
    int Flush() const { return StatusFake::flushError; }
    int Close() const { ++StatusFake::closes; return StatusFake::closeError; }
};
} // namespace CTRPluginFramework
