#include "RomfsIndex.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>
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
const u32 kArchiveContent = 0x2345678A;   // ARCHIVE_SAVEDATA_AND_CONTENT。他のタイトルを開く
// ★実機で数えたところ ARCHIVE_ROMFS は **8,629 本**、つまり base だけを返した
//   （update だけなら 1,907、重ねていれば 10,430。IDA-opus-5-F037）。
//   update にしか無いパスが 1,801 本あり、うち 741 本は描画できるモデルを持つ
//   （Item/Model 540、住民 128）ので、update 側も読む。
const u32 kUpdateTitleHigh = 0x0004000E;  // 同じ低位 ID の更新タイトル
// ★更新データの RomFS は **ARCHIVE_ROMFS をファイルパス {5, 0, 0} で開く**（ゲーム自身の開き方。
//   sub_11D184 → sub_129410 が {5,0,0}、base は sub_1363A0 が {0,0,0}。どちらも archive 3・パスは空。
//   IDA-opus-5.5-F022）。以前の「更新タイトルを ARCHIVE_SAVEDATA_AND_CONTENT で開く」は実機で 3 媒体とも失敗していた（T025）。
const u32 kSelfNcchUpdateRomfs = 5;

// ARCHIVE_ROMFS をファイルの種類 type で開く（0 = base の RomFS、5 = 更新データの RomFS）
Handle OpenSelfRomfs(u32 type) {
    static const char kEmptyText[] = "";
    u32 fileData[3] = { type, 0, 0 };
    const FS_Path archivePath = { PATH_EMPTY, 1, kEmptyText };
    const FS_Path filePath = { PATH_BINARY, sizeof(fileData), fileData };
    Handle file = 0;
    if (R_FAILED(FSUSER_OpenFileDirectly(&file, (FS_ArchiveID)kArchiveRomfs, archivePath, filePath, FS_OPEN_READ, 0)))
        return 0;
    return file;
}
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
u32 s_updateCount;                  // そのうち更新タイトルから来た分
u32 s_arenaUsed;
u32 s_writeArenaAt;                 // Walk が詰め始める位置。追記のため
u32 s_writeIndexAt;
u32 s_updateOpenResult;             // 更新タイトルを開けなかったときの Result
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
                        char *at = s_arena + s_writeArenaAt + bytes;
                        std::memcpy(at, path, pathLength);
                        if (sep)
                            at[pathLength] = '/';
                        std::memcpy(at + pathLength + sep, name, n + 1);
                        s_offsets[s_writeIndexAt + count] = s_writeArenaAt + bytes;
                        s_sizes[s_writeIndexAt + count] = Word(entry + 16);  // 64 bit の下位
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

// 開いている 1 本の RomFS を歩いて、見つかった分を末尾へ足す。
// 表はこの中で取ってこの中で返すので、ピークは「一番大きい表 + パスの列」。
// 重複は取り除かない。両方にあるパスは 106 本だけで、一覧に 2 行出る以上の害は無い。
bool Ingest(Handle file, Fail *why, u32 *code) {
    u32 got = 0;
    u8 front[0x60];
    u32 header[10];

    if (R_FAILED(FSFILE_Read(file, &got, 0, front, sizeof(front))) || got != sizeof(front)) {
        *why = Fail::ReadFailed; *code = got;
        return false;
    }
    u64 level3 = 0;
    if (std::memcmp(front, "IVFC", 4) == 0) {
        const u32 shift = Word(front + 0x4C);
        if (shift > 24) {
            *why = Fail::BadHeader; *code = shift;
            return false;
        }
        const u64 block = (u64)1 << shift;
        level3 = ((u64)0x60 + block - 1) & ~(block - 1);
    }
    if (level3 == 0) {
        std::memcpy(header, front, sizeof(header));
    } else if (R_FAILED(FSFILE_Read(file, &got, level3, header, sizeof(header))) ||
               got != sizeof(header)) {
        *why = Fail::ReadFailed; *code = got;
        return false;
    }
    if (header[0] != 0x28) {
        *why = Fail::BadHeader; *code = header[0];
        return false;
    }
    s_dirsSize = header[4];
    s_filesSize = header[8];
    if (s_dirsSize < kDirEntry || s_filesSize < kFileEntry ||
        s_dirsSize > kTableLimit || s_filesSize > kTableLimit) {
        *why = Fail::BadHeader; *code = s_filesSize;
        return false;
    }

    s_dirs = (u8 *)std::malloc(s_dirsSize);
    s_files = (u8 *)std::malloc(s_filesSize);
    if (s_dirs == nullptr || s_files == nullptr) {
        ReleaseTables();
        *why = Fail::OutOfMemory; *code = s_dirsSize + s_filesSize;
        return false;
    }
    if (R_FAILED(FSFILE_Read(file, &got, level3 + header[3], s_dirs, s_dirsSize)) ||
        got != s_dirsSize ||
        R_FAILED(FSFILE_Read(file, &got, level3 + header[7], s_files, s_filesSize)) ||
        got != s_filesSize) {
        ReleaseTables();
        *why = Fail::ReadFailed; *code = got;
        return false;
    }

    u32 count = 0, bytes = 0;
    if (!Walk(true, &count, &bytes) || count == 0) {
        ReleaseTables();
        *why = Fail::WalkFailed; *code = count;
        return false;
    }

    char *arena = (char *)std::realloc(s_arena, s_arenaUsed + bytes);
    u32 *offsets = (u32 *)std::realloc(s_offsets, (s_count + count) * sizeof(u32));
    u32 *sizes = (u32 *)std::realloc(s_sizes, (s_count + count) * sizeof(u32));
    if (arena != nullptr) s_arena = arena;
    if (offsets != nullptr) s_offsets = offsets;
    if (sizes != nullptr) s_sizes = sizes;
    if (arena == nullptr || offsets == nullptr || sizes == nullptr) {
        ReleaseTables();
        *why = Fail::OutOfMemory; *code = bytes;
        return false;
    }

    // 第 2 周目は末尾から。Walk は 0 起点で書くので、ずらした位置を渡す。
    s_writeArenaAt = s_arenaUsed;
    s_writeIndexAt = s_count;
    u32 again = 0, usedBytes = 0;
    const bool filled = Walk(false, &again, &usedBytes) && again == count && usedBytes == bytes;
    s_writeArenaAt = 0;
    s_writeIndexAt = 0;
    ReleaseTables();
    if (!filled) {
        *why = Fail::WalkFailed; *code = 2;
        return false;
    }
    s_arenaUsed += bytes;
    s_count += count;
    return true;
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
u32 UpdateCount(void) { return s_updateCount; }
u32 UpdateOpenResult(void) { return s_updateOpenResult; }

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
    s_updateOpenResult = 0;
    s_count = 0;
    s_updateCount = 0;
    s_arenaUsed = 0;

    // libctru の romfsMountFromCurrentProcess と**完全に同じ引数**で開く
    // （`libctru/source/romfs_dev.c`）。自分で考えた `{PATH_EMPTY, 0, nullptr}` だと
    // FS が 0xE0E046BE（module 17 = FS / summary 7 = invalid argument）を返す（F036）。
    static const char kEmptyText[] = "";
    static const u8 kZeros[0xC] = { 0 };
    const FS_Path archivePath = { PATH_EMPTY, 1, kEmptyText };
    const FS_Path filePath = { PATH_BINARY, sizeof(kZeros), kZeros };

    Handle file = 0;
    const Result opened = FSUSER_OpenFileDirectly(&file, (FS_ArchiveID)kArchiveRomfs,
                                                  archivePath, filePath, FS_OPEN_READ, 0);
    if (R_FAILED(opened) || file == 0)
        return Stop(Fail::OpenFailed, (u32)opened);

    Fail why = Fail::None;
    u32 code = 0;
    const bool got = Ingest(file, &why, &code);
    FSFILE_Close(file);
    svcCloseHandle(file);
    if (!got)
        return Stop(why, code);

    // 更新タイトルも読む。こちらは **失敗しても致命ではない**：
    // base だけで 8,629 本あり、更新側にしか無いのは 1,801 本。
    // タイトル ID は走っているプロセスから取るので、地域を焦き込まない。
    const u64 running = CTRPluginFramework::Process::GetTitleID();
    const u64 updateTid = ((u64)kUpdateTitleHigh << 32) | (u32)running;
    // まずゲームと同じ開き方（ARCHIVE_ROMFS + {5,0,0}）。だめなら以前の方法（SD → カード → NAND）。
    static const u8 kMediaTypes[] = { 0xFF, 1, 2, 0 };
    for (u32 i = 0; i < sizeof(kMediaTypes); ++i) {
        Handle update = 0;
        Result r = 0;
        if (kMediaTypes[i] == 0xFF) {
            update = OpenSelfRomfs(kSelfNcchUpdateRomfs);
            r = update != 0 ? 0 : (Result)0xFFFFFFFF;
        } else {
            u32 archiveData[4] = { (u32)updateTid, (u32)(updateTid >> 32), kMediaTypes[i], 0 };
            u32 fileData[5] = { 0, 0, 0, 0, 0 };
            const FS_Path titlePath = { PATH_BINARY, sizeof(archiveData), archiveData };
            const FS_Path titleFile = { PATH_BINARY, sizeof(fileData), fileData };
            r = FSUSER_OpenFileDirectly(&update, (FS_ArchiveID)kArchiveContent, titlePath, titleFile, FS_OPEN_READ, 0);
        }
        s_updateOpenResult = (u32)r;
        if (R_FAILED(r) || update == 0)
            continue;
        const u32 before = s_count;
        Fail ignoredWhy = Fail::None;
        u32 ignoredCode = 0;
        if (Ingest(update, &ignoredWhy, &ignoredCode))
            s_updateCount = s_count - before;
        FSFILE_Close(update);
        svcCloseHandle(update);
        break;
    }

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

// ---------------------------------------------------------------------------------------
// ReadFile: ハッシュ表で 1 本だけ引く
// ---------------------------------------------------------------------------------------

namespace {

bool ReadAt(Handle file, u64 at, void *out, u32 size) {
    u32 got = 0;
    return R_SUCCEEDED(FSFILE_Read(file, &got, at, out, size)) && got == size;
}

// RomFS の名前ハッシュ（3dbrew / libctru romfs_dev.c の calc_hash と同じ式）
u32 NameHash(u32 parent, const char *name, u32 length) {
    u32 hash = parent ^ 123456789u;
    for (u32 i = 0; i < length; ++i) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= (u8)name[i];
    }
    return hash;
}

// エントリの名前（UTF-16LE）が ASCII の name[0..length) と同じか
bool SameName(Handle file, u64 at, u32 nameLength, const char *name, u32 length) {
    if (nameLength != length * 2 || nameLength > 2 * kMaxPath)
        return false;
    u16 text[kMaxPath];
    if (!ReadAt(file, at, text, nameLength))
        return false;
    for (u32 i = 0; i < length; ++i)
        if (text[i] != (u8)name[i])
            return false;
    return true;
}

u32 ReadFrom(Handle file, const char *path, void *buf, u32 cap) {
    u8 front[0x60];
    if (!ReadAt(file, 0, front, sizeof(front)))
        return 0;
    u64 level3 = 0;
    if (std::memcmp(front, "IVFC", 4) == 0) {
        const u32 shift = Word(front + 0x4C);
        if (shift > 24)
            return 0;
        const u64 block = (u64)1 << shift;
        level3 = ((u64)0x60 + block - 1) & ~(block - 1);
    }
    u32 header[10];
    if (!ReadAt(file, level3, header, sizeof(header)) || header[0] != 0x28)
        return 0;
    const u32 dirBuckets = header[2] / 4;
    const u32 fileBuckets = header[6] / 4;
    if (dirBuckets == 0 || fileBuckets == 0)
        return 0;

    u32 dir = 0;                                        // 根
    const char *part = path;
    for (u32 depth = 0; depth < kMaxDepth; ++depth) {
        const char *slash = std::strchr(part, '/');
        const u32 length = slash != nullptr ? (u32)(slash - part) : (u32)std::strlen(part);
        if (length == 0 || length > kMaxPath)
            return 0;
        if (slash != nullptr) {                         // ディレクトリ
            u32 entry = kEnd;
            if (!ReadAt(file, level3 + header[1] + 4 * (NameHash(dir, part, length) % dirBuckets), &entry, 4))
                return 0;
            for (u32 guard = 0; entry != kEnd && guard < 4096; ++guard) {
                u32 e[6];                               // parent, sibling, child, file, hashNext, nameLen
                if (entry > header[4] || !ReadAt(file, level3 + header[3] + entry, e, sizeof(e)))
                    return 0;
                if (e[0] == dir && SameName(file, level3 + header[3] + entry + kDirEntry, e[5], part, length))
                    break;
                entry = e[4];
            }
            if (entry == kEnd)
                return 0;
            dir = entry;
            part = slash + 1;
            continue;
        }
        u32 entry = kEnd;                               // ファイル
        if (!ReadAt(file, level3 + header[5] + 4 * (NameHash(dir, part, length) % fileBuckets), &entry, 4))
            return 0;
        for (u32 guard = 0; entry != kEnd && guard < 4096; ++guard) {
            u32 e[8];                                   // parent, sibling, offset(64), size(64), hashNext, nameLen
            if (entry > header[8] || !ReadAt(file, level3 + header[7] + entry, e, sizeof(e)))
                return 0;
            if (e[0] == dir && SameName(file, level3 + header[7] + entry + kFileEntry, e[7], part, length)) {
                const u64 offset = ((u64)e[3] << 32) | e[2];
                const u64 size = ((u64)e[5] << 32) | e[4];
                if (size == 0 || size > cap)
                    return 0;
                if (buf == nullptr)                     // 大きさだけ（FileSize）
                    return (u32)size;
                return ReadAt(file, level3 + header[9] + offset, buf, (u32)size) ? (u32)size : 0;
            }
            entry = e[6];
        }
        return 0;
    }
    return 0;
}

}  // namespace

namespace {

// ★開いたものを使い回す。以前は 1 回読むたびに更新タイトル（媒体を 3 通り試す）と base を開き直していて、
//   建物エディターで種類を切り替えるたびに重かった（利用者報告、2026-09-24）。
Handle s_updateHandle;
Handle s_baseHandle;
bool s_updateTried;

Handle UpdateHandle(void) {
    if (s_updateTried)
        return s_updateHandle;
    s_updateTried = true;
    s_updateHandle = OpenSelfRomfs(kSelfNcchUpdateRomfs);     // ゲームと同じ開き方
    if (s_updateHandle != 0)
        return s_updateHandle;
    const u64 running = CTRPluginFramework::Process::GetTitleID();
    const u64 updateTid = ((u64)kUpdateTitleHigh << 32) | (u32)running;
    static const u8 kMediaTypes[] = { 1, 2, 0 };
    for (u32 i = 0; i < sizeof(kMediaTypes); ++i) {
        u32 archiveData[4] = { (u32)updateTid, (u32)(updateTid >> 32), kMediaTypes[i], 0 };
        u32 fileData[5] = { 0, 0, 0, 0, 0 };
        const FS_Path titlePath = { PATH_BINARY, sizeof(archiveData), archiveData };
        const FS_Path titleFile = { PATH_BINARY, sizeof(fileData), fileData };
        Handle update = 0;
        if (R_SUCCEEDED(FSUSER_OpenFileDirectly(&update, (FS_ArchiveID)kArchiveContent, titlePath, titleFile,
                                                FS_OPEN_READ, 0)) && update != 0) {
            s_updateHandle = update;
            break;
        }
    }
    return s_updateHandle;
}

Handle BaseHandle(void) {
    if (s_baseHandle != 0)
        return s_baseHandle;
    static const char kEmptyText[] = "";
    static const u8 kZeros[0xC] = { 0 };
    const FS_Path archivePath = { PATH_EMPTY, 1, kEmptyText };
    const FS_Path filePath = { PATH_BINARY, sizeof(kZeros), kZeros };
    Handle base = 0;
    if (R_SUCCEEDED(FSUSER_OpenFileDirectly(&base, (FS_ArchiveID)kArchiveRomfs, archivePath, filePath,
                                            FS_OPEN_READ, 0)) && base != 0)
        s_baseHandle = base;
    return s_baseHandle;
}

}  // namespace

u32 ReadFile(const char *path, void *buf, u32 cap) {
    if (path == nullptr || cap == 0)
        return 0;
    // 更新タイトルが先（同じパスがあればゲームもこちらを使う）
    const Handle update = UpdateHandle();
    u32 got = update != 0 ? ReadFrom(update, path, buf, cap) : 0;
    if (got != 0)
        return got;
    const Handle base = BaseHandle();
    return base != 0 ? ReadFrom(base, path, buf, cap) : 0;
}

u32 FileSize(const char *path) {
    return ReadFile(path, nullptr, 0xFFFFFFFFu);
}

}  // namespace RomfsIndex
