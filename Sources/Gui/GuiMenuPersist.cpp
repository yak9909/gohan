// ============================================================================
// GuiMenuPersist — 保持（issue-fixes.js の persistence / retained state）
// ============================================================================
//
// 置き場所は GohanCTRPFData.bin の gohan の欄（同梱 libctrpf の GohanData。GuiMenu.cpp が繋ぐ）。
// Simulator は localStorage に 4 つの鍵で置く（favorites / settings / enabledItems / enabledFavorites）。
// ここでは 1 本のバイト列にまとめる。**旧形式からの移し替えはしない**（利用者の指示）。
//
// 形式（リトルエンディアン）。★版 2（2026-09-27、Simulator e5f8f01 の保持の組み替え）
//   'GMP1'  u16 版(=2)
//   u8 設定の旗: bit0 お気に入りを保持 / bit1 値の固定・保持された項目 / bit2 値の固定・お気に入り / bit3 値の固定・全項目 /
//                bit4 トグル状態・保持された項目 / bit5 トグル状態・お気に入り / bit6 トグル状態・全項目
//   u8 押し切るまでボタンの遮断（A/B/X/Y/START = bit0..4）
//   u16 お気に入りの数      { u16 長さ, 階層パス }...
//   u16 この項目を保持の数  { u16 長さ, 階層パス }...                          （印。issue-fixes.js retainedItems）
//   u16 印の項目の値の数    { u16 長さ, 階層パス, u8 種類, s32 適用値 }...       （checkbox と連動型以外。retainedState）
//   u16 トグル状態の数      { u16 長さ, 階層パス, u8 ON }...                     （checkbox。トグル状態の範囲。toggleStates）
//   u16 値の固定の数        { u16 長さ, 階層パス, u8 種類, s32 固定値 }...       （連動型。値の固定の範囲。valueLocks）
//   u16 ホットキーの数      { u16 長さ, 階層パス, u16 適用済みのホットキー }...  （常に。既定と違うものだけ）
// 版 1（2026-09-25〜27 の保存）は読むときに移し替える（Simulator と同じ規則: オンにした項目を保持 → トグル状態・全項目、
//   オンにしたお気に入りを保持 → トグル状態・お気に入り、値の固定を保持 → 値の固定・全項目。ほかは既定）。
// 階層パスは項目のラベルを "/" で連結したもの（ui-model.js の assignFavoriteKeys と同じ鍵）。
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
                const u16   kVersion = 2;

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

            namespace
            {
                // stateScopeMatches（全項目 / 印の項目 / お気に入り）
                bool    ScopeMatches(int idx, bool retainedScope, bool favoriteScope, bool allScope)
                {
                    return allScope || (retainedScope && g_retained[idx]) || (favoriteScope && g_favorite[idx]);
                }

                template <typename F>
                void    WriteList(std::vector<u8> &out, F &fn)
                {
                    fn.count = 0;
                    WalkPaths(g_rootFirst, g_rootCount, std::string(), fn);
                    Put16(out, fn.count);
                    out.insert(out.end(), fn.body.begin(), fn.body.end());
                }

                // 効果の規則（ON かつホットキーの束縛なしのときだけ効果 ON。§4.3）を取り直す
                void    RefreshEffect(int idx)
                {
                    const Item     &it = g_items[idx];
                    const Behavior &b = g_behavior[idx];

                    if (it.type != ITEM_CHECKBOX || b.IsActive == nullptr || b.SetActive == nullptr)
                        return;

                    const bool want = it.applied != 0 && it.appliedHotkey == 0;

                    if (b.IsActive(idx) != want)
                        b.SetActive(idx, want);
                    PrimeEffect(idx);
                }
            }

            void    SerializePersist(std::vector<u8> &out)
            {
                const Persistence &st = g_persist;

                out.clear();
                out.insert(out.end(), kMagic, kMagic + 4);
                Put16(out, kVersion);
                Put8(out, (st.keepFavorites ? 1u : 0u) | (st.keepRetainedValueLocks ? 2u : 0u) | (st.keepFavoriteValueLocks ? 4u : 0u)
                          | (st.keepAllValueLocks ? 8u : 0u) | (st.keepRetainedToggleStates ? 16u : 0u)
                          | (st.keepFavoriteToggleStates ? 32u : 0u) | (st.keepAllToggleStates ? 64u : 0u));
                Put8(out, st.blockButtonsUntilReleaseMask & 0x1Fu);

                // お気に入り（keepFavorites が偽なら空。issue-fixes.js favoriteKeysArray）
                {
                    struct { u32 count; std::vector<u8> body; void operator()(int idx, const std::string &p) { if (g_persist.keepFavorites && g_favorite[idx]) { PutStr(body, p); count++; } } } fn;

                    WriteList(out, fn);
                }
                // この項目を保持（印。makeRetainedItemSelection）
                {
                    struct { u32 count; std::vector<u8> body; void operator()(int idx, const std::string &p) { if (g_retained[idx] && IsItemRetainable(idx)) { PutStr(body, p); count++; } } } fn;

                    WriteList(out, fn);
                }
                // 印の項目の値（makeSpecificRetainedSnapshot: checkbox は除く）。
                // ★gohan の意図した差: 連動型も除く（固定していない連動型は保持しない。固定は値の固定の欄）
                {
                    struct
                    {
                        u32 count;
                        std::vector<u8> body;
                        void operator()(int idx, const std::string &p)
                        {
                            const Item &it = g_items[idx];

                            if (!g_retained[idx] || !Retainable(it) || it.type == ITEM_CHECKBOX || IsLinked(it))
                                return;
                            PutStr(body, p);
                            Put8(body, it.type);
                            Put32(body, (u32)it.applied);
                            count++;
                        }
                    } fn;

                    WriteList(out, fn);
                }
                // トグル状態（makeToggleStateSnapshot: checkbox の適用済み ON/OFF、範囲で絞る）
                {
                    struct
                    {
                        u32 count;
                        std::vector<u8> body;
                        void operator()(int idx, const std::string &p)
                        {
                            const Item &it = g_items[idx];

                            if (it.type != ITEM_CHECKBOX
                                || !ScopeMatches(idx, g_persist.keepRetainedToggleStates, g_persist.keepFavoriteToggleStates,
                                                 g_persist.keepAllToggleStates))
                                return;
                            PutStr(body, p);
                            Put8(body, it.applied != 0 ? 1u : 0u);
                            count++;
                        }
                    } fn;

                    WriteList(out, fn);
                }
                // 値の固定（makeValueLockSnapshot: 固定中の連動型、範囲で絞る）
                {
                    struct
                    {
                        u32 count;
                        std::vector<u8> body;
                        void operator()(int idx, const std::string &p)
                        {
                            if (!IsFixed(idx)
                                || !ScopeMatches(idx, g_persist.keepRetainedValueLocks, g_persist.keepFavoriteValueLocks,
                                                 g_persist.keepAllValueLocks))
                                return;
                            PutStr(body, p);
                            Put8(body, g_items[idx].type);
                            Put32(body, (u32)g_fixedValue[idx]);
                            count++;
                        }
                    } fn;

                    WriteList(out, fn);
                }
                // ホットキー（常に。既定と違うものだけ。適用済みの値。編集中は入れない）
                {
                    struct
                    {
                        u32 count;
                        std::vector<u8> body;
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
                    WriteList(out, fn);
                }
            }

            namespace
            {
                void    RestoreHotkeys(Reader &r, u32 size)
                {
                    if (!r.ok || r.at >= size)
                        return;

                    const u32 hotkeys = r.Get(2);

                    for (u32 i = 0; i < hotkeys && r.ok; i++)
                    {
                        const std::string   path = r.Str();
                        const u32           hk = r.Get(2);
                        const int           idx = FindByPath(path);

                        if (!r.ok || idx < 0 || g_items[idx].type == ITEM_FOLDER)
                            continue;
                        g_items[idx].hotkey = (u16)hk;
                        g_items[idx].appliedHotkey = (u16)hk;
                        RefreshEffect(idx);
                    }
                }

                void    RestoreFavorites(Reader &r)
                {
                    const u32 favorites = r.Get(2);

                    for (u32 i = 0; i < favorites && r.ok; i++)
                    {
                        const int idx = FindByPath(r.Str());

                        if (idx >= 0 && g_persist.keepFavorites)
                            g_favorite[idx] = true;
                    }
                }

                // 版 2
                void    RestoreV2(Reader &r, u32 size)
                {
                    const u32 flags = r.Get(1);
                    const u32 mask = r.Get(1);

                    g_persist.keepFavorites = (flags & 1u) != 0;
                    g_persist.keepRetainedValueLocks = (flags & 2u) != 0;
                    g_persist.keepFavoriteValueLocks = (flags & 4u) != 0;
                    g_persist.keepAllValueLocks = (flags & 8u) != 0;
                    g_persist.keepRetainedToggleStates = (flags & 16u) != 0;
                    g_persist.keepFavoriteToggleStates = (flags & 32u) != 0;
                    g_persist.keepAllToggleStates = (flags & 64u) != 0;
                    g_persist.blockButtonsUntilReleaseMask = (u8)(mask & 0x1Fu);
                    RestoreFavorites(r);

                    const u32 marks = r.Get(2);

                    for (u32 i = 0; i < marks && r.ok; i++)
                    {
                        const int idx = FindByPath(r.Str());

                        if (r.ok && idx >= 0 && IsItemRetainable(idx))
                            g_retained[idx] = true;
                    }

                    const u32 values = r.Get(2);

                    for (u32 i = 0; i < values && r.ok; i++)
                    {
                        const std::string   path = r.Str();
                        const u32           type = r.Get(1);
                        const s32           value = (s32)r.Get(4);
                        const int           idx = FindByPath(path);

                        if (!r.ok || idx < 0 || g_items[idx].type != type || !Retainable(g_items[idx])
                            || g_items[idx].type == ITEM_CHECKBOX || IsLinked(g_items[idx]))
                            continue;
                        ApplyRetainedValue(idx, value);
                    }

                    const u32 toggles = r.Get(2);

                    for (u32 i = 0; i < toggles && r.ok; i++)
                    {
                        const std::string   path = r.Str();
                        const u32           on = r.Get(1);
                        const int           idx = FindByPath(path);

                        if (!r.ok || idx < 0 || g_items[idx].type != ITEM_CHECKBOX)
                            continue;
                        ApplyRetainedValue(idx, on != 0 ? 1 : 0);
                    }

                    const u32 locks = r.Get(2);

                    for (u32 i = 0; i < locks && r.ok; i++)
                    {
                        const std::string   path = r.Str();
                        const u32           type = r.Get(1);
                        const s32           fixedValue = (s32)r.Get(4);
                        const int           idx = FindByPath(path);

                        if (!r.ok || idx < 0 || g_items[idx].type != type || !IsLinked(g_items[idx]))
                            continue;
                        ApplyValueLock(idx, fixedValue);
                    }
                    RestoreHotkeys(r, size);
                }

                // 版 1（移し替え）
                void    RestoreV1(Reader &r, u32 size)
                {
                    const u32 flags = r.Get(1);

                    r.Get(1);
                    g_persist.keepFavorites = (flags & 1u) != 0;
                    g_persist.keepAllToggleStates = (flags & 2u) != 0;          // オンにした項目を保持
                    g_persist.keepFavoriteToggleStates = (flags & 4u) != 0;     // オンにしたお気に入りを保持
                    g_persist.keepAllValueLocks = (flags & 8u) != 0;            // 値の固定を保持
                    RestoreFavorites(r);

                    // 保持した値（オンにした項目を保持 / オンにしたお気に入りを保持。固定は 2026-09-26 より前の形）
                    const bool  general = (flags & 6u) != 0;
                    const u32   retained = r.Get(2);

                    for (u32 i = 0; i < retained && r.ok; i++)
                    {
                        const std::string   path = r.Str();
                        const u32           type = r.Get(1);
                        const u32           rflags = r.Get(1);
                        const s32           value = (s32)r.Get(4);
                        const s32           fixedValue = (s32)r.Get(4);
                        const int           idx = FindByPath(path);

                        if (!r.ok || idx < 0 || !general || g_items[idx].type != type || !Retainable(g_items[idx]))
                            continue;
                        if (IsLinked(g_items[idx]))
                        {
                            if ((rflags & 1u) != 0)
                                ApplyValueLock(idx, fixedValue);
                        }
                        else
                            ApplyRetainedValue(idx, value);
                    }
                    RestoreHotkeys(r, size);

                    // この項目を保持（印 + 値）
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
                            if (!IsLinked(g_items[idx]))
                                ApplyRetainedValue(idx, value);
                        }
                    }
                    // 値の固定
                    if (r.ok && r.at < size)
                    {
                        const u32 n = r.Get(2);

                        for (u32 i = 0; i < n && r.ok; i++)
                        {
                            const std::string   path = r.Str();
                            const u32           type = r.Get(1);
                            const s32           fixedValue = (s32)r.Get(4);
                            const int           idx = FindByPath(path);

                            if (!r.ok || idx < 0 || !g_persist.keepAllValueLocks || g_items[idx].type != type
                                || !IsLinked(g_items[idx]))
                                continue;
                            ApplyValueLock(idx, fixedValue);
                        }
                    }
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

                const u32 version = r.Get(2);

                if (version == 2)
                    RestoreV2(r, size);
                else if (version == 1)
                    RestoreV1(r, size);
                else
                    return;
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
