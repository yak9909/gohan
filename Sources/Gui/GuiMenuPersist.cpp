// ============================================================================
// GuiMenuPersist — 保持（issue-fixes.js の persistence / retained state）
// ============================================================================
//
// 置き場所は GohanCTRPFData.bin の gohan の欄（同梱 libctrpf の GohanData。GuiMenu.cpp が繋ぐ）。
// Simulator は localStorage に 4 つの鍵で置く（favorites / settings / enabledItems / enabledFavorites）。
// ここでは 1 本のバイト列にまとめる。**旧形式からの移し替えはしない**（利用者の指示）。
//
// 形式（リトルエンディアン）
//   'GMP1'  u16 版(=1)  u8 設定の旗(bit0 お気に入りを保持 / bit1 オンにした項目を保持 / bit2 オンにしたお気に入りを保持
//                                    / bit3 値の固定を保持)  u8 0
//   u16 お気に入りの数  { u16 長さ, 階層パス }...
//   u16 保持した値の数  { u16 長さ, 階層パス, u8 種類, u8 旗(bit0 固定), s32 適用値, s32 固定値 }...
//                        ★2026-09-26（Simulator 639b4e6）から固定は入れない（旗 0・連動型は入らない）。古い保存の固定は読む
//   u16 ホットキーの数  { u16 長さ, 階層パス, u16 適用済みのホットキー }...   ← 2026-09-26 追加（末尾。無ければ読まない）
//   u16 この項目を保持の数 { u16 長さ, 階層パス, u8 種類, s32 適用値 }...     ← Simulator 639b4e6（末尾。無ければ読まない）
//   u16 値の固定の数    { u16 長さ, 階層パス, u8 種類, s32 固定値 }...     ← 同上。「値の固定を保持」のときだけ中身がある
// 階層パスは項目のラベルを "/" で連結したもの（ui-model.js の assignFavoriteKeys と同じ鍵）。
// ホットキーの欄は末尾に足しただけなので、それより前の形は変わらない（古い保存も版 1 のまま読める）。
//
// ★Simulator との意図した差
//   1. ★固定していない連動型は保持しない（利用者の決定 2026-09-25）。Simulator は連動型の適用値も保持するので、
//      起動のたびにゲームの値（所持金など）が前回の値へ書き戻されてしまう。固定している連動型は固定値を保持する
//      （固定の駆動が、ゲームが読めるようになってから書く）。
//   2. チェック項目の効果は、適用のときと同じ規則で戻す（ON かつホットキーの束縛なしのときだけ効果 ON。
//      gohan-menu.md §4.3）。Simulator は保持した値をそのまま effectActive に入れる。
//   3. 戻したときは通知しない（登録しただけで通知しない、という PollEffects の方針と同じ）。
//   4. ★ホットキーを保持する（利用者の指摘 2026-09-26。Simulator の issue-fixes.js は値しか保存しない）。
//      CTRPF の通常メニューと同じく**保持の設定に関係なく常に**保存する。既定（BuildTree の値）と違うものだけ書く。

#include "GuiMenuInternal.hpp"

#include <cstring>
#include <string>

namespace CTRPluginFramework
{
    namespace GuiMenu
    {
        namespace detail
        {
            namespace
            {
                const u8    kMagic[4] = { 'G', 'M', 'P', '1' };
                const u16   kVersion = 1;

                bool                g_pendingRestore[kMaxItems];
                std::vector<u8>     g_lastSaved;
                u16                 g_defaultHotkey[kMaxItems];     // BuildTree が決めた既定（最初の保存・復元の前に控える）
                bool                g_defaultsTaken = false;

                void    TakeDefaultHotkeys(void)
                {
                    if (g_defaultsTaken)
                        return;
                    for (int i = 0; i < g_itemCount && i < kMaxItems; i++)
                        g_defaultHotkey[i] = g_items[i].appliedHotkey;
                    g_defaultsTaken = true;
                }

                // RETAINABLE_TYPES
                bool    Retainable(const Item &it)
                {
                    return it.type == ITEM_CHECKBOX || it.type == ITEM_VALUE || it.type == ITEM_SLIDER
                           || it.type == ITEM_LIST || it.type == ITEM_LINKED_VALUE || it.type == ITEM_LINKED_LIST;
                }

                // 深さ優先で (項目番号, 階層パス) を回す（walkItems と同じ順）
                template <typename F>
                void    WalkPaths(int first, int count, const std::string &parent, F &fn)
                {
                    for (int i = 0; i < count; i++)
                    {
                        const int           idx = first + i;
                        const Item         &it = g_items[idx];
                        const std::string   path = parent.empty() ? std::string(it.label) : parent + "/" + it.label;

                        fn(idx, path);
                        if (it.type == ITEM_FOLDER)
                            WalkPaths((int)it.childFirst, (int)it.childCount, path, fn);
                    }
                }

                int     FindByPath(const std::string &want)
                {
                    struct { const std::string *want; int found; void operator()(int idx, const std::string &p) { if (found < 0 && p == *want) found = idx; } } fn = { &want, -1 };

                    WalkPaths(g_rootFirst, g_rootCount, std::string(), fn);
                    return fn.found;
                }

                void    Put8(std::vector<u8> &o, u32 v)  { o.push_back((u8)v); }
                void    Put16(std::vector<u8> &o, u32 v) { Put8(o, v); Put8(o, v >> 8); }
                void    Put32(std::vector<u8> &o, u32 v) { Put16(o, v); Put16(o, v >> 16); }

                void    PutStr(std::vector<u8> &o, const std::string &s)
                {
                    const u32 n = s.size() > 0xFFFF ? 0xFFFF : (u32)s.size();

                    Put16(o, n);
                    o.insert(o.end(), s.begin(), s.begin() + n);
                }

                struct Reader
                {
                    const u8   *p;
                    u32         size, at;
                    bool        ok;

                    u32     Get(int bytes)
                    {
                        u32 v = 0;

                        if (!ok || at + (u32)bytes > size)
                        {
                            ok = false;
                            return 0;
                        }
                        for (int i = 0; i < bytes; i++)
                            v |= (u32)p[at + i] << (8 * i);
                        at += (u32)bytes;
                        return v;
                    }

                    std::string Str(void)
                    {
                        const u32 n = Get(2);

                        if (!ok || at + n > size)
                        {
                            ok = false;
                            return std::string();
                        }

                        const std::string s((const char *)p + at, n);

                        at += n;
                        return s;
                    }
                };

                // normalizeRetainedValue
                s32     Normalize(const Item &it, s32 v)
                {
                    if (it.type == ITEM_CHECKBOX)
                        return v != 0 ? 1 : 0;
                    if (it.type == ITEM_LIST || it.type == ITEM_LINKED_LIST)
                        return ClampI(v, 0, it.optionCount > 0 ? (int)it.optionCount - 1 : 0);
                    return ClampI(v, it.minimum, it.maximum);
                }

                // applyRetainedSnapshot の 1 件（連動型以外）。チェック項目の効果は適用と同じ規則（§4.3）
                void    ApplyRetainedValue(int idx, s32 value)
                {
                    Item       &it = g_items[idx];
                    const s32   v = Normalize(it, value);

                    it.value = v;
                    it.applied = v;
                    if (it.type == ITEM_CHECKBOX)
                    {
                        const Behavior &b = g_behavior[idx];

                        if (b.IsActive != nullptr && b.SetActive != nullptr)
                        {
                            const bool want = it.applied != 0 && it.appliedHotkey == 0;

                            if (b.IsActive(idx) != want)
                                b.SetActive(idx, want);
                        }
                        PrimeEffect(idx);
                    }
                    else if (g_behavior[idx].Apply != nullptr)
                        g_behavior[idx].Apply(idx, it.applied);
                }

                // applyValueLockSnapshot の 1 件
                void    ApplyValueLock(int idx, s32 fixedValue)
                {
                    Item &it = g_items[idx];

                    g_fixed[idx] = true;
                    g_fixedValue[idx] = Normalize(it, fixedValue);
                    it.value = g_fixedValue[idx];
                    it.applied = g_fixedValue[idx];
                }
            }

            void    SerializePersist(std::vector<u8> &out)
            {
                out.clear();
                out.insert(out.end(), kMagic, kMagic + 4);
                Put16(out, kVersion);
                Put8(out, (g_persist.keepFavorites ? 1u : 0u) | (g_persist.keepEnabledItems ? 2u : 0u)
                          | (g_persist.keepEnabledFavorites ? 4u : 0u) | (g_persist.keepValueLocks ? 8u : 0u));
                Put8(out, 0);

                // お気に入り（keepFavorites が偽なら空。issue-fixes.js favoriteKeysArray）
                {
                    struct { std::vector<std::string> keys; void operator()(int idx, const std::string &p) { if (g_favorite[idx]) keys.push_back(p); } } fn;

                    if (g_persist.keepFavorites)
                        WalkPaths(g_rootFirst, g_rootCount, std::string(), fn);
                    Put16(out, (u32)fn.keys.size());
                    for (size_t i = 0; i < fn.keys.size(); i++)
                        PutStr(out, fn.keys[i]);
                }

                // 保持した値（適用値だけ。編集中の値は入れない）。
                // 「オンにした項目を保持」なら全部、「オンにしたお気に入りを保持」だけならお気に入りだけ。
                {
                    struct Fn
                    {
                        bool                        favoritesOnly;
                        std::vector<u8>            *out;
                        u32                         count;
                        std::vector<u8>             body;
                        void operator()(int idx, const std::string &p)
                        {
                            const Item &it = g_items[idx];

                            if (!Retainable(it) || (favoritesOnly && !g_favorite[idx]))
                                return;
                            // 連動型は入れない: 固定していないものは保持しない（利用者の決定）、固定は「値の固定を保持」の欄へ
                            //   （Simulator 639b4e6: オンにした項目の保持は値の固定を含めない）
                            if (IsLinked(it))
                                return;
                            PutStr(body, p);
                            Put8(body, it.type);
                            Put8(body, 0);
                            Put32(body, (u32)it.applied);
                            Put32(body, 0);
                            count++;
                        }
                    } fn;

                    fn.favoritesOnly = !g_persist.keepEnabledItems;
                    fn.out = &out;
                    fn.count = 0;
                    if (g_persist.keepEnabledItems || g_persist.keepEnabledFavorites)
                        WalkPaths(g_rootFirst, g_rootCount, std::string(), fn);
                    Put16(out, fn.count);
                    out.insert(out.end(), fn.body.begin(), fn.body.end());
                }

                // ホットキー（常に。既定と違うものだけ。適用済みの値。編集中は入れない）
                {
                    struct Fn
                    {
                        u32                 count;
                        std::vector<u8>     body;
                        void operator()(int idx, const std::string &p)
                        {
                            const Item &it = g_items[idx];

                            if (it.type == ITEM_FOLDER || it.appliedHotkey == g_defaultHotkey[idx])
                                return;
                            PutStr(body, p);
                            Put16(body, it.appliedHotkey);
                            count++;
                        }
                    } fn;

                    TakeDefaultHotkeys();
                    fn.count = 0;
                    WalkPaths(g_rootFirst, g_rootCount, std::string(), fn);
                    Put16(out, fn.count);
                    out.insert(out.end(), fn.body.begin(), fn.body.end());
                }

                // この項目を保持（全体の設定に関係なく、印を付けた項目の適用値。makeSpecificRetainedSnapshot）
                {
                    struct Fn
                    {
                        u32                 count;
                        std::vector<u8>     body;
                        void operator()(int idx, const std::string &p)
                        {
                            if (!g_retained[idx] || !IsItemRetainable(idx))
                                return;
                            PutStr(body, p);
                            Put8(body, g_items[idx].type);
                            Put32(body, (u32)g_items[idx].applied);
                            count++;
                        }
                    } fn;

                    fn.count = 0;
                    WalkPaths(g_rootFirst, g_rootCount, std::string(), fn);
                    Put16(out, fn.count);
                    out.insert(out.end(), fn.body.begin(), fn.body.end());
                }

                // 値の固定（「値の固定を保持」のときだけ。makeValueLockSnapshot）
                {
                    struct Fn
                    {
                        u32                 count;
                        std::vector<u8>     body;
                        void operator()(int idx, const std::string &p)
                        {
                            if (!IsFixed(idx))
                                return;
                            PutStr(body, p);
                            Put8(body, g_items[idx].type);
                            Put32(body, (u32)g_fixedValue[idx]);
                            count++;
                        }
                    } fn;

                    fn.count = 0;
                    if (g_persist.keepValueLocks)
                        WalkPaths(g_rootFirst, g_rootCount, std::string(), fn);
                    Put16(out, fn.count);
                    out.insert(out.end(), fn.body.begin(), fn.body.end());
                }
            }

            void    RestorePersist(const u8 *data, u32 size)
            {
                Reader  r = { data, size, 0, data != nullptr };

                TakeDefaultHotkeys();
                std::memset(g_pendingRestore, 0, sizeof(g_pendingRestore));
                if (data == nullptr || size < 8 || std::memcmp(data, kMagic, 4) != 0)
                {
                    g_lastSaved.clear();                // 保存が無い: 既定のまま。次に閉じたとき書く
                    return;
                }
                r.at = 4;
                if (r.Get(2) != kVersion)
                    return;

                const u32 flags = r.Get(1);

                r.Get(1);
                g_persist.keepFavorites = (flags & 1u) != 0;
                g_persist.keepEnabledItems = (flags & 2u) != 0;
                g_persist.keepEnabledFavorites = (flags & 4u) != 0;
                g_persist.keepValueLocks = (flags & 8u) != 0;

                const u32 favorites = r.Get(2);

                for (u32 i = 0; i < favorites && r.ok; i++)
                {
                    const int idx = FindByPath(r.Str());

                    if (idx >= 0 && g_persist.keepFavorites)
                        g_favorite[idx] = true;
                }

                const u32 retained = r.Get(2);

                for (u32 i = 0; i < retained && r.ok; i++)
                {
                    const std::string   path = r.Str();
                    const u32           type = r.Get(1);
                    const u32           rflags = r.Get(1);
                    const s32           value = (s32)r.Get(4);
                    const s32           fixedValue = (s32)r.Get(4);
                    const int           idx = FindByPath(path);

                    if (!r.ok || idx < 0 || !(g_persist.keepEnabledItems || g_persist.keepEnabledFavorites))
                        continue;

                    Item &it = g_items[idx];

                    if (it.type != type || !Retainable(it))
                        continue;

                    // 固定していない連動型は保持しない（古い保存に入っていても戻さない。利用者の決定）
                    if (IsLinked(it) && (rflags & 1u) == 0)
                        continue;
                    if (IsLinked(it))
                        ApplyValueLock(idx, fixedValue);    // 2026-09-26 より前の保存（固定をこの欄に入れていた）
                    else
                        ApplyRetainedValue(idx, value);
                }

                // ホットキー（末尾の欄。古い保存には無い）
                if (r.ok && r.at < size)
                {
                    const u32 hotkeys = r.Get(2);

                    for (u32 i = 0; i < hotkeys && r.ok; i++)
                    {
                        const std::string   path = r.Str();
                        const u32           hk = r.Get(2);
                        const int           idx = FindByPath(path);

                        if (!r.ok || idx < 0 || g_items[idx].type == ITEM_FOLDER)
                            continue;

                        Item &it = g_items[idx];

                        it.hotkey = (u16)hk;
                        it.appliedHotkey = (u16)hk;
                        // 効果の規則（ON かつ束縛なしのときだけ効果 ON。§4.3）を束縛に合わせて取り直す
                        if (it.type == ITEM_CHECKBOX)
                        {
                            const Behavior &b = g_behavior[idx];

                            if (b.IsActive != nullptr && b.SetActive != nullptr)
                            {
                                const bool want = it.applied != 0 && it.appliedHotkey == 0;

                                if (b.IsActive(idx) != want)
                                    b.SetActive(idx, want);
                                PrimeEffect(idx);
                            }
                        }
                    }
                }
                // この項目を保持（末尾の欄。全体の設定とは独立で、最後に適用する）
                if (r.ok && r.at < size)
                {
                    const u32 n = r.Get(2);

                    for (u32 i = 0; i < n && r.ok; i++)
                    {
                        const std::string   path = r.Str();
                        const u32           type = r.Get(1);
                        const s32           value = (s32)r.Get(4);
                        const int           idx = FindByPath(path);

                        if (!r.ok || idx < 0 || g_items[idx].type != type || !IsItemRetainable(idx))
                            continue;
                        g_retained[idx] = true;
                        ApplyRetainedValue(idx, value);
                    }
                }
                // 値の固定（「値の固定を保持」のときだけ戻す）
                if (r.ok && r.at < size)
                {
                    const u32 n = r.Get(2);

                    for (u32 i = 0; i < n && r.ok; i++)
                    {
                        const std::string   path = r.Str();
                        const u32           type = r.Get(1);
                        const s32           fixedValue = (s32)r.Get(4);
                        const int           idx = FindByPath(path);

                        if (!r.ok || idx < 0 || !g_persist.keepValueLocks || g_items[idx].type != type
                            || !IsLinked(g_items[idx]))
                            continue;
                        ApplyValueLock(idx, fixedValue);
                    }
                }
                SerializePersist(g_lastSaved);
            }

            void    DrivePendingRestores(void)
            {
                for (int i = 0; i < g_itemCount; i++)
                {
                    if (!g_pendingRestore[i])
                        continue;

                    const Behavior &b = g_behavior[i];
                    s32             v = 0;

                    if (!IsLinked(g_items[i]) || IsFixed(i) || b.LinkedRead == nullptr || b.LinkedWrite == nullptr)
                    {
                        g_pendingRestore[i] = false;
                        continue;
                    }
                    if (!b.LinkedRead(i, &v))
                        continue;
                    g_pendingRestore[i] = false;
                    if (v != g_items[i].applied)
                        b.LinkedWrite(i, g_items[i].applied);
                }
            }

            bool    PersistChanged(void)
            {
                std::vector<u8> now;

                SerializePersist(now);
                return now != g_lastSaved;
            }

            void    MarkPersistSaved(void)
            {
                SerializePersist(g_lastSaved);
            }
        }
    }
}
