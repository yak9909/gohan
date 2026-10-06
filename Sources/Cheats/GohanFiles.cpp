// GohanFiles — SD の gohan フォルダのリソース（GohanFiles.hpp に並びの説明）

#include "GohanFiles.hpp"

#include <CTRPluginFramework.hpp>

#include <cstdio>
#include <new>

namespace GohanFiles {

using CTRPluginFramework::File;

bool TitlePath(char *out, u32 size, const char *name) {
    const unsigned long long tid = CTRPluginFramework::Process::GetTitleID();
    const int n = std::snprintf(out, size, "/gohan/%016llX/%s", tid, name);
    return n > 0 && (u32)n < size;
}

bool CommonPath(char *out, u32 size, const char *name) {
    const int n = std::snprintf(out, size, "/gohan/common/%s", name);
    return n > 0 && (u32)n < size;
}

u8 *ReadAll(const char *path, u32 maxBytes, u32 &size) {
    size = 0;
    File f;
    if (File::Open(f, path, File::READ) != File::SUCCESS)
        return nullptr;
    const u64 total = f.GetSize();
    if (total == 0 || total > maxBytes) {
        f.Close();
        return nullptr;
    }
    u8 *buf = new (std::nothrow) u8[(u32)total + 1];
    if (buf == nullptr) {
        f.Close();
        return nullptr;
    }
    if (f.Read(buf, (u32)total) != File::SUCCESS) {
        f.Close();
        delete[] buf;
        return nullptr;
    }
    f.Close();
    buf[total] = 0;
    size = (u32)total;
    return buf;
}

}  // namespace GohanFiles
