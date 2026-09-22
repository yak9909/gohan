#include "RomfsIndex.hpp"

#include <3ds.h>
#include <cstdlib>
#include <cstring>

namespace RomfsIndex {

namespace {

// libctru の romfs_header / romfs_dir / romfs_file と同じ並び。
// ディレクトリ: parent, sibling, child, file, hash, nameLen, name[]          (24 B + 名前)
// ファイル    : parent, sibling, offset(64), size(64), hash, nameLen, name[] (32 B + 名前)
const u32 kDirEntry = 24;
const u32 kFileEntry = 32;
const u32 kEnd = 0xFFFFFFFFu;
const u32 kArchiveRomfs = 3;        // ARCHIVE_ROMFS。自分自身の RomFS なので ID も種別も要らない
const u32 kMaxPath = 192;           // 実測の最長は 75
const u32 kMaxDepth = 32;
const u32 kTableLimit = 0x400000;   // 実測は dir 42,648 B / file 940,716 B

u8 *s_dirs;
u8 *s_files;
char *s_arena;                      // パスを詰めたもの
u32 *s_offsets;                     // s_arena の中の位置
u32 *s_sizes;                       // .bcres のバイト数（ファイル表に入っている）
u32 s_dirsSize;
u32 s_filesSize;
u32 s_count;
u32 s_arenaUsed;
bool s_ready;
Fail s_reason = Fail::None;
u32 s_errorCode;

// Match が返す文字列の置き場。次に Match を呼ぶまで有効、という約束。
char s_matchText[256 * (kMaxPath / 2)];
u32 s_matchUsed;

u32 Word(const u8 *p) {
    u32 v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

bool Fits(u32 offset, u32 size, u32 capacity) {
    return offset <= capacity && size <= capacity - offset;
}

// romfs の名前は UTF-16LE。ゲームのパスはすべて ASCII なので、そうでない字が混ざって
// いたら飛ばす（あとでゲームへ渡せないパスを一覧に載せないため）。
bool AsciiName(const u8 *entry, u32 header, u32 bytes, char *out, u32 cap) {
    if ((bytes & 1) != 0 || bytes / 2 >= cap)
        return false;
    const u32 length = bytes / 2;
    for (u32 i = 0; i < length; ++i) {
        if (entry[header + i * 2 + 1] != 0)
            return false;
        const u8 c = entry[header + i * 2];
        if (c < 0x20 || c > 0x7E)
            return false;
        out[i] = (char)c;
    }
    out[length] = '\0';
    return true;
}

bool EndsWithBcres(const char *name, u32 length) {
    return length > 6 && std::strcmp(name + length - 6, ".bcres") == 0;
}

struct Level { u32 dir; u16 pathLength; };

u32 DirWord(u32 dir, u32 field) { return Word(s_dirs + dir + field); }

// 名前が ASCII でないものは飛ばす。兄弟の鎖の中で最初に使えるものを返す。
u32 UsableDir(u32 dir, char *name, u32 cap) {
    while (dir != kEnd) {
        if (!Fits(dir, kDirEntry, s_dirsSize))
            return kEnd;
        const u32 bytes = DirWord(dir, 20);
        if (!Fits(dir + kDirEntry, bytes, s_dirsSize))
            return kEnd;
        if (AsciiName(s_dirs + dir, kDirEntry, bytes, name, cap))
            return dir;
        dir = DirWord(dir, 4);
    }
    return kEnd;
}

// 1 周目は数えるだけ、2 周目で詰める。表から正確な大きさが出るので伸ばす器が要らない。
//
// ★子と兄弟の鎖をそのまま辿る。スタックに積むのは**先祖だけ**なので、
//   その深さは木の深さ（実測 4）になる。兄弟をまとめて積む書き方だと
//   スタックの深さが**幅**になり、ルート直下で溢れてフォルダごと黙って落ちる
//   （tools/models3d/verify_romfs_index.py がこれを捕まえた）。
bool Walk(bool measure, u32 *countOut, u32 *bytesOut) {
    char path[kMaxPath];
    char name[kMaxPath];
    Level stack[kMaxDepth];
    u32 depth = 0;
    u32 count = 0;
    u32 bytes = 0;
    u32 visited = 0;
    u32 cur = 0;
    u32 pathLength = 0;

    path[0] = '\0';
    for (;;) {
        if (++visited > s_dirsSize / kDirEntry + 1)
            return false;
        path[pathLength] = '\0';

        // このフォルダのファイル。先頭はファイル表ではなくディレクトリ項に入っている。
        u32 fileAt = DirWord(cur, 12);
        u32 steps = 0;
        while (fileAt != kEnd) {
            if (++steps > s_filesSize / kFileEntry + 1 || !Fits(fileAt, kFileEntry, s_filesSize))
                return false;
            const u8 *entry = s_files + fileAt;
            const u32 nameBytes = Word(entry + 28);
            if (!Fits(fileAt + kFileEntry, nameBytes, s_filesSize))
                return false;
            if (AsciiName(entry, kFileEntry, nameBytes, name, sizeof(name))) {
                const u32 n = nameBytes / 2;
                const u32 sep = pathLength ? 1u : 0u;
                if (EndsWithBcres(name, n) && pathLength + sep + n + 1 <= kMaxPath) {
                    if (!measure) {
                        char *at = s_arena + bytes;
                        std::memcpy(at, path, pathLength);
                        if (sep)
                            at[pathLength] = '/';
                        std::memcpy(at + pathLength + sep, name, n + 1);
                        s_offsets[count] = bytes;
                        s_sizes[count] = Word(entry + 16);   // 64 bit の下位。.bcres に 4 GB は無い
                    }
                    bytes += pathLength + sep + n + 1;
                    ++count;
                }
            }
            fileAt = Word(entry + 4);
        }

        const u32 child = UsableDir(DirWord(cur, 8), name, sizeof(name));
        if (child != kEnd && depth < kMaxDepth) {
            const u32 add = (u32)std::strlen(name);
            const u32 sep = pathLength ? 1u : 0u;
            if (pathLength + sep + add + 1 < kMaxPath) {
                stack[depth].dir = cur;
                stack[depth].pathLength = (u16)pathLength;
                ++depth;
                if (sep)
                    path[pathLength] = '/';
                std::memcpy(path + pathLength + sep, name, add + 1);
                pathLength += sep + add;
                cur = child;
                continue;
            }
        }

        for (;;) {
            const u32 sibling = UsableDir(DirWord(cur, 4), name, sizeof(name));
            if (sibling != kEnd) {
                const u32 parentLength = depth ? stack[depth - 1].pathLength : 0;
                const u32 add = (u32)std::strlen(name);
                const u32 sep = parentLength ? 1u : 0u;
                if (parentLength + sep + add + 1 >= kMaxPath)
                    return false;
                if (sep)
                    path[parentLength] = '/';
                std::memcpy(path + parentLength + sep, name, add + 1);
                pathLength = parentLength + sep + add;
                cur = sibling;
                break;
            }
            if (depth == 0) {
                *countOut = count;
                *bytesOut = bytes;
                return true;
            }
            --depth;
            cur = stack[depth].dir;
            pathLength = stack[depth].pathLength;
        }
    }
}

void ReleaseTables(void) {
    std::free(s_dirs);
    std::free(s_files);
    s_dirs = nullptr;
    s_files = nullptr;
}

bool Stop(Fail reason, u32 code) {
    s_reason = reason;
    s_errorCode = code;
    ReleaseTables();
    std::free(s_arena);
    std::free(s_offsets);
    std::free(s_sizes);
    s_arena = nullptr;
    s_offsets = nullptr;
    s_sizes = nullptr;
    s_count = 0;
    return false;
}

}  // namespace

bool Ready(void) { return s_ready; }
Fail Reason(void) { return s_reason; }
u32 ErrorCode(void) { return s_errorCode; }
u32 Count(void) { return s_count; }

const char *PathAt(u32 index) {
    return (s_ready && index < s_count) ? s_arena + s_offsets[index] : "";
}

u32 SizeAt(u32 index) {
    return (s_ready && index < s_count) ? s_sizes[index] : 0;
}

bool Build(void) {
    if (s_ready)
        return true;
    s_reason = Fail::None;
    s_errorCode = 0;

    // libctru の romfsMountFromCurrentProcess と**完全に同じ引数**で開く
    // （`libctru/source/romfs_dev.c`）。ここを自分で考えて `{PATH_EMPTY, 0, nullptr}` を
    // 両方へ渡したところ、FS が 0xE0E046BE（module 17 = FS / summary 7 = invalid argument）
    // を返した（IDA-opus-5-F036）。
    //   * アーカイブ側は PATH_EMPTY だが **長さ 1 の空文字列**。長さ 0 や null ではない
    //   * ファイル側は PATH_EMPTY ではなく **PATH_BINARY の 12 バイトのゼロ**
    Handle file = 0;
    static const char kEmptyText[] = "";
    static const u8 kZeros[0xC] = { 0 };
    const FS_Path archivePath = { PATH_EMPTY, 1, kEmptyText };
    const FS_Path filePath = { PATH_BINARY, sizeof(kZeros), kZeros };
    const Result opened = FSUSER_OpenFileDirectly(&file, (FS_ArchiveID)kArchiveRomfs,
                                                  archivePath, filePath, FS_OPEN_READ, 0);
    if (R_FAILED(opened) || file == 0)
        return Stop(Fail::OpenFailed, (u32)opened);

    u32 got = 0;
    u8 front[0x60];
    u32 header[10];
    bool bad = false;
    u32 badCode = 0;
    Fail badWhy = Fail::None;

    if (R_FAILED(FSFILE_Read(file, &got, 0, front, sizeof(front))) || got != sizeof(front)) {
        bad = true; badWhy = Fail::ReadFailed; badCode = got;
    }
    // ★この開き方だと FS は**既に level 3 を剥いて渡してくる**ので、
    //   40 バイトのヘッダがそのまま先頭にある（libctru も offset 0 から読んでいる）。
    //   IVFC が付いた生のイメージを渡されたときのために、そちらも受ける。
    u64 level3 = 0;
    if (!bad && std::memcmp(front, "IVFC", 4) == 0) {
        const u32 shift = Word(front + 0x4C);
        if (shift > 24) {
            bad = true; badWhy = Fail::BadHeader; badCode = shift;
        } else {
            const u64 block = (u64)1 << shift;
            level3 = ((u64)0x60 + block - 1) & ~(block - 1);
        }
    }
    if (!bad) {
        if (level3 == 0) {
            std::memcpy(header, front, sizeof(header));   // さっき読んだ先頭に入っている
        } else if (R_FAILED(FSFILE_Read(file, &got, level3, header, sizeof(header))) ||
                   got != sizeof(header)) {
            bad = true; badWhy = Fail::ReadFailed; badCode = got;
        }
        if (!bad && header[0] != 0x28) {
            bad = true; badWhy = Fail::BadHeader; badCode = header[0];
        }
    }
    if (!bad) {
        s_dirsSize = header[4];
        s_filesSize = header[8];
        if (s_dirsSize < kDirEntry || s_filesSize < kFileEntry ||
            s_dirsSize > kTableLimit || s_filesSize > kTableLimit) {
            bad = true; badWhy = Fail::BadHeader; badCode = s_filesSize;
        }
    }
    if (!bad) {
        s_dirs = (u8 *)std::malloc(s_dirsSize);
        s_files = (u8 *)std::malloc(s_filesSize);
        if (s_dirs == nullptr || s_files == nullptr) {
            bad = true; badWhy = Fail::OutOfMemory; badCode = s_dirsSize + s_filesSize;
        }
    }
    if (!bad) {
        if (R_FAILED(FSFILE_Read(file, &got, level3 + header[3], s_dirs, s_dirsSize)) ||
            got != s_dirsSize ||
            R_FAILED(FSFILE_Read(file, &got, level3 + header[7], s_files, s_filesSize)) ||
            got != s_filesSize) {
            bad = true; badWhy = Fail::ReadFailed; badCode = got;
        }
    }
    FSFILE_Close(file);
    svcCloseHandle(file);
    if (bad)
        return Stop(badWhy, badCode);

    u32 count = 0, bytes = 0;
    if (!Walk(true, &count, &bytes))
        return Stop(Fail::WalkFailed, 0);
    if (count == 0)
        return Stop(Fail::WalkFailed, 1);

    s_arena = (char *)std::malloc(bytes);
    s_offsets = (u32 *)std::malloc(count * sizeof(u32));
    s_sizes = (u32 *)std::malloc(count * sizeof(u32));
    if (s_arena == nullptr || s_offsets == nullptr || s_sizes == nullptr)
        return Stop(Fail::OutOfMemory, bytes);

    u32 again = 0, usedBytes = 0;
    if (!Walk(false, &again, &usedBytes) || again != count || usedBytes != bytes)
        return Stop(Fail::WalkFailed, 2);

    // 表はもう要らない。残すのはパスの列だけ。
    ReleaseTables();
    s_count = count;
    s_arenaUsed = bytes;
    s_ready = true;
    return true;
}

u32 Window(u32 start, const char **names, u32 cap) {
    s_matchUsed = 0;
    if (!s_ready || start >= s_count)
        return 0;
    u32 filled = 0;
    while (filled < cap && start + filled < s_count) {
        const char *path = s_arena + s_offsets[start + filled];
        const u32 length = (u32)std::strlen(path) + 1;
        if (s_matchUsed + length > sizeof(s_matchText))
            break;
        std::memcpy(s_matchText + s_matchUsed, path, length);
        names[filled] = s_matchText + s_matchUsed;
        s_matchUsed += length;
        ++filled;
    }
    return filled;
}

const char *ReasonName(Fail reason) {
    switch (reason) {
    case Fail::None: return u8"-";
    case Fail::OpenFailed: return u8"RomFS が開けません";
    case Fail::BadHeader: return u8"RomFS のヘッダが読めません";
    case Fail::ReadFailed: return u8"RomFS の読み出しが切れました";
    case Fail::OutOfMemory: return u8"索引を置く場所が取れません";
    case Fail::WalkFailed: return u8"RomFS の表をたどれません";
    }
    return u8"?";
}

}  // namespace RomfsIndex
