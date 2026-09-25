// ============================================================================
// GuiMenu — 自前チートメニューのスレッド・入力・公開 API
// ============================================================================
//
// 模型は GuiMenuModel.cpp、描画は GuiMenuDraw.cpp、項目の木は GuiMenuItems.cpp。
// CTRPF / libctru を触るのはこのファイルだけ。
//
// CTRPF の既存メニューはゲームを止める。こちらはゲームの nw::lyt 経路へ
// 自前の Layout ノードを差し込む方式なので、**開いている間もゲームが進行する**。
//
// ホットキー
//   Select 単独        … 自前メニュー
//   Select + 十字上    … 従来の CTRPF メニュー

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <CTRPluginFramework/System/Touch.hpp>

#include <cstdio>
#include <cstring>

#include "GuiMenuInternal.hpp"
#include "GuiDialog.hpp"
#include "GuiKeyboard.hpp"
#include "ChatKanji.hpp"
#include "csvc.h"   // svcInvalidateEntireInstructionCache

namespace CTRPluginFramework
{
    namespace GuiMenu
    {
        using namespace detail;

        namespace
        {
            bool        g_run = false;
            Thread      g_thread = nullptr;

            // ★開閉の依頼は CTRPF のスレッドから来る。状態はメニュースレッドだけが触る。
            volatile bool g_reqOpen = false;
            volatile bool g_reqClose = false;

            // ★別スレッドからの通知依頼。中身を写し終えてから旗を立てる。
            char        g_pendTitle[32];
            char        g_pendMsg[96];
            bool        g_pendRed = false;
            volatile bool g_pendHave = false;

            // ★GuiDialog の依頼（どのスレッドからでも）。輪に写して、メニューのスレッドが 1 つずつ出す。
            struct PendingMessage { char title[64]; char body[512]; bool error; };
            PendingMessage  g_msgQueue[GuiDialog::kQueue];
            volatile u32    g_msgHead = 0, g_msgTail = 0;     // 書く側 = head、出す側 = tail
            LightLock       g_msgLock;
            bool            g_msgLockReady = false;

            // HotkeyBit の順（ui-model.js の HOTKEY_BUTTON_ORDER）
            const u32   kHotkeyKeys[HB_COUNT] = {
                (u32)Key::ZL, (u32)Key::L, (u32)Key::R, (u32)Key::ZR,
                (u32)Key::DPadUp, (u32)Key::DPadDown, (u32)Key::DPadLeft, (u32)Key::DPadRight,
                (u32)Key::A, (u32)Key::B, (u32)Key::X, (u32)Key::Y,
                (u32)Key::Select, (u32)Key::Start
            };

            u32     NowMs(void)
            {
                return (u32)(svcGetSystemTick() / (u64)(SYSCLOCK_ARM11 / 1000));
            }

            // ★入力は水準（GetKeysDown(true)）を 1 フレームに 1 回だけ読む。
            //   立ち上がりは模型が前フレームとの差で作る（F-321。IsKeyPressed は使わない）。
            Input   SampleInput(void)
            {
                const u32   keys = Controller::GetKeysDown(true);
                Input       in;

                in.held = 0;
                for (int i = 0; i < HB_COUNT; i++)
                    if ((keys & kHotkeyKeys[i]) != 0)
                        in.held = (u16)(in.held | (1u << i));
                in.touch = Touch::IsDown();

                const UIntVector pos = Touch::GetPosition();

                in.tx = (int)pos.x;
                in.ty = (int)pos.y;
                return in;
            }

            bool    g_touchLatch = false;
            bool    g_dpadBlockReq = false;     // このフレームだけゲームの十字キーを遮断（BlockGameDpad）
            bool    g_allBlockReq = false;      // このフレームだけスライドパッドも含めて全部遮断（BlockGameAll）

            // ★入力遮断は毎フレーム条件から決める（F-350）。描画の有無に縛らない。
            void    SyncInputLock(const Input &in)
            {
                // ボタン遮断: 操作可能な UI が出ている間（gameInputCaptureState）
                //   操作可能な UI が無い間は、関数側の要求（Step 中の OnTick）があれば十字キーだけ遮断する
                GuiRenderer::SetButtonBlock(ButtonBlock(), g_dpadBlockReq, g_allBlockReq);
                g_dpadBlockReq = false;
                g_allBlockReq = false;
                // タッチ遮断: 下画面 UI がある間（退場中も）。消えた後も指が離れるまで続ける。
                //   ホットキー入力待ちを「無効」「取消」のタッチで閉じた瞬間に遮断を外すと、
                //   触れたままの指がゲームへ新しいタッチとして届いていた。
                const bool present = BottomUiPresent();

                g_touchLatch = present || (g_touchLatch && in.touch);
                GuiRenderer::SetTouchBlock(g_touchLatch);
            }

            void    Draw(u32 now)
            {
                BuildTop(now);
                BuildBottom(now);
                GuiRenderer::Commit();
            }

            // ---- 保持（GohanCTRPFData.bin の gohan の欄。同梱 libctrpf の GohanData）----
            //   書き出しは CTRPF のメニューを閉じたときの保存でも呼ばれる（CTRPF のスレッドから）。
            void    PersistWrite(std::vector<u8> &out)      { SerializePersist(out); }
            void    PersistRead(const u8 *data, u32 size)   { RestorePersist(data, size); }

            // 中身が前回の保存と違えば書く（メニューを閉じ切ったとき・終了時）
            void    SaveIfChanged(void)
            {
                if (!PersistChanged())
                    return;
                GohanData::Save();
                MarkPersistSaved();
            }

            void    ThreadMain(void *)
            {
                bool wasVisible = g_visible;

                while (g_run)
                {
                    if (GuiRenderer::IsReady())
                    {
                        const u32   now = NowMs();
                        const Input in = SampleInput();

                        // メニューを閉じ切った（見えなくなった）フレームで保存する。
                        // Simulator は操作のたびに localStorage へ書くが、SD へ毎回は書かない。
                        if (wasVisible && !g_visible)
                            SaveIfChanged();
                        wasVisible = g_visible;

                        if (g_reqOpen)
                        {
                            g_reqOpen = false;
                            OpenMenu(now);
                        }
                        if (g_reqClose)
                        {
                            g_reqClose = false;
                            CloseMenu(now);
                        }
                        if (g_pendHave)
                        {
                            AddNotice(g_pendTitle, g_pendMsg, now, g_pendRed);
                            g_pendHave = false;
                        }
                        if (!g_message.active && g_msgLockReady)
                        {
                            LightLock_Lock(&g_msgLock);
                            if (g_msgTail != g_msgHead)
                            {
                                const PendingMessage &m = g_msgQueue[g_msgTail % GuiDialog::kQueue];

                                OpenMessage(m.title, m.body, m.error, now);
                                g_msgTail = g_msgTail + 1;
                            }
                            LightLock_Unlock(&g_msgLock);
                        }
                        Step(now, in);
                        SyncInputLock(in);
                        if (g_visible || g_openTarget != 0.0f || Animating(now) || g_needFinal)
                        {
                            Draw(now);
                            g_needFinal = false;
                        }
                    }
                    svcSleepThread(16000000LL);     // 約 16ms
                }
            }

            // ★スレッドの有無では判定しない。各チートの登録（WireBehaviors）はスレッドを作る前に走るので、
            //   以前の「g_thread != nullptr」だと登録時の SetItemOptions / SetItemApplied が
            //   黙って何もしなかった（公共事業のリストが「読み込み中」のまま、利用者報告 2026-09-23）。
            //   項目表は BuildTree で登録より前に組まれているので、項目数だけ見れば足りる。
            bool    ItemOk(int index)
            {
                return index >= 0 && index < g_itemCount;
            }

            bool    SlotOk(int index)
            {
                return index >= 0 && index < kMaxItems;
            }

            void    QueueNotice(const char *title, const char *message, bool red)
            {
                if (g_thread == nullptr)
                    return;
                std::snprintf(g_pendTitle, sizeof(g_pendTitle), "%s", title);
                std::snprintf(g_pendMsg, sizeof(g_pendMsg), "%s", message);
                g_pendRed = red;
                g_pendHave = true;
            }
        }

        void    Redraw(void)
        {
            if (GuiRenderer::IsReady())
                Draw(NowMs());
        }

        bool    Initialize(void)
        {
            const u32 now = NowMs();

            if (g_thread != nullptr)
                return true;
            if (!g_msgLockReady)
            {
                LightLock_Init(&g_msgLock);
                g_msgLockReady = true;
            }
            BuildTree();
            GuiKeyboard::Reset();
            ResetState();
            // ★ResetState が ToggleHandlers を消すので、振る舞いの登録はその後
            WireBehaviors();
            // ★保持の復元は各チートの登録（Cheats::Wire）の後。効果・適用の関数が要る
            GohanData::SetHandlers(PersistWrite, PersistRead);
            GohanData::Load();
            // ★開いた瞬間に押されているボタン（Select など）を立ち上がりとして拾わない
            PrimeInput(SampleInput().held);
            g_reqOpen = false;
            g_reqClose = false;
            g_pendHave = false;
            OpenMenu(now);
            g_run = true;
            Redraw();
            g_thread = threadCreate(ThreadMain, nullptr, 0x2000, 0x30, -2, true);
            return g_thread != nullptr;
        }

        void    Shutdown(void)
        {
            if (g_thread == nullptr)
                return;
            g_run = false;
            svcSleepThread(50000000LL);
            g_thread = nullptr;
            SaveIfChanged();
            ChatKanji::Dismiss();
            g_visible = false;
            g_openTarget = 0.0f;
        }

        bool    OnOpening(void)
        {
            // ★十字上を一緒に押していれば従来の CTRPF メニュー
            if (Controller::IsKeyDown(Key::DPadUp))
                return true;
            if (!GuiRenderer::IsReady() || g_thread == nullptr)
                return true;
            // ★入力待ちの最中は Select を記録用に残す
            if (g_capture.active)
                return false;
            if (IsOpen())
                Close();
            else
                Open();
            return false;
        }

        // openTarget（チートメニューを開く／閉じるボタンの判定と同じ）
        bool    IsOpen(void)    { return g_openTarget != 0.0f; }
        void    Open(void)      { g_reqOpen = true; }
        void    Close(void)     { g_reqClose = true; }

        void    Notify(const char *title, const char *message)      { QueueNotice(title, message, false); }
        void    NotifyRed(const char *title, const char *message)   { QueueNotice(title, message, true); }

        // ---- 関数側連携 ----
        int     FindItem(const char *label)
        {
            if (label == nullptr)
                return -1;
            for (int i = 0; i < g_itemCount; i++)
                if (std::strcmp(g_items[i].label, label) == 0)
                    return i;
            return -1;
        }

        bool    RegisterToggleEffect(int index, const ToggleEffectFuncs *funcs)
        {
            if (!SlotOk(index) || funcs == nullptr || funcs->IsActive == nullptr || funcs->SetActive == nullptr)
                return false;
            g_behavior[index].IsActive = funcs->IsActive;
            g_behavior[index].SetActive = funcs->SetActive;
            PrimeEffect(index);
            return true;
        }

        void    UnregisterToggleEffect(int index)
        {
            if (!SlotOk(index))
                return;
            g_behavior[index].IsActive = nullptr;
            g_behavior[index].SetActive = nullptr;
            PrimeEffect(index);
        }

        bool    ToggleEffectActive(int index)
        {
            return SlotOk(index) && EffectActive(index);
        }

        bool    RegisterLinked(int index, LinkedRead read, LinkedWrite write)
        {
            if (!SlotOk(index) || read == nullptr || write == nullptr)
                return false;
            g_behavior[index].LinkedRead = read;
            g_behavior[index].LinkedWrite = write;
            return true;
        }

        bool    RegisterExecute(int index, ExecuteFunc execute)
        {
            if (!SlotOk(index))
                return false;
            g_behavior[index].Execute = execute;
            return true;
        }

        bool    RegisterApply(int index, ApplyFunc apply)
        {
            if (!SlotOk(index))
                return false;
            g_behavior[index].Apply = apply;
            return true;
        }

        bool    RegisterDisabled(int index, DisabledFunc isDisabled)
        {
            if (!SlotOk(index))
                return false;
            g_behavior[index].IsDisabled = isDisabled;
            return true;
        }

        void    FlushMemory(u32 address, u32 size)
        {
            if (address == 0 || size == 0)
                return;
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE, address, size);
            svcInvalidateEntireInstructionCache();
        }

        static u32 PatchMask(int size)
        {
            return size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
        }

        static u32 PatchRead(u32 address, int size)
        {
            if (size == 1)
                return *(volatile u8 *)address;
            if (size == 2)
                return *(volatile u16 *)address;
            return *(volatile u32 *)address;
        }

        static void PatchWrite(u32 address, int size, u32 value)
        {
            if (size == 1)
                *(volatile u8 *)address = (u8)value;
            else if (size == 2)
                *(volatile u16 *)address = (u16)value;
            else
                *(volatile u32 *)address = value;
        }

        static bool PatchValid(const TogglePatch *p)
        {
            return p != nullptr && p->address != 0 && (p->size == 1 || p->size == 2 || p->size == 4);
        }

        bool    PatchListIsActive(const TogglePatch *patches, int count)
        {
            if (patches == nullptr || count <= 0)
                return false;
            for (int i = 0; i < count; i++)
            {
                const TogglePatch *p = &patches[i];

                if (!PatchValid(p))
                    return false;
                if ((PatchRead(p->address, (int)p->size) & PatchMask((int)p->size))
                    != (p->onValue & PatchMask((int)p->size)))
                    return false;
            }
            return true;
        }

        void    PatchListSetActive(const TogglePatch *patches, int count, bool active)
        {
            if (patches == nullptr || count <= 0)
                return;
            for (int i = 0; i < count; i++)
            {
                const TogglePatch *p = &patches[i];

                if (!PatchValid(p))
                    continue;
                PatchWrite(p->address, (int)p->size, active ? p->onValue : p->offValue);
                FlushMemory(p->address, (u32)p->size);
            }
        }

        void    BlockGameDpad(void)
        {
            g_dpadBlockReq = true;
        }

        void    BlockGameAll(void)
        {
            g_allBlockReq = true;
        }

        bool    IsVisible(void)
        {
            return g_visible || OverlayActive() || g_dialog.type != DLG_NONE || g_message.active;
        }

        void    QueueMessage(const char *title, const char *body, bool error)
        {
            if (!g_msgLockReady)
            {
                LightLock_Init(&g_msgLock);
                g_msgLockReady = true;
            }
            LightLock_Lock(&g_msgLock);
            if (g_msgHead - g_msgTail >= GuiDialog::kQueue)
                g_msgTail = g_msgTail + 1;          // あふれたら古い物を捨てる
            PendingMessage &m = g_msgQueue[g_msgHead % GuiDialog::kQueue];
            std::snprintf(m.title, sizeof(m.title), "%s", title != nullptr ? title : "");
            std::snprintf(m.body, sizeof(m.body), "%s", body != nullptr ? body : "");
            m.error = error;
            g_msgHead = g_msgHead + 1;
            LightLock_Unlock(&g_msgLock);
        }

        bool    MessageOpen(void)
        {
            return g_message.active || g_msgHead != g_msgTail;
        }

        void    SetToggleHandlers(const ToggleHandlers *handlers)
        {
            if (handlers == nullptr)
                std::memset(&g_toggleHdl, 0, sizeof(g_toggleHdl));
            else
                g_toggleHdl = *handlers;
        }

        int     ItemCount(void)                 { return g_itemCount; }
        const char *ItemLabel(int index)        { return ItemOk(index) ? g_items[index].label : ""; }
        int     ItemValue(int index)            { return ItemOk(index) ? (int)g_items[index].value : 0; }
        int     ItemApplied(int index)          { return ItemOk(index) ? (int)g_items[index].applied : 0; }
        u16     ItemHotkey(int index)           { return ItemOk(index) ? g_items[index].hotkey : 0; }
        u16     ItemAppliedHotkey(int index)    { return ItemOk(index) ? g_items[index].appliedHotkey : 0; }

        void    SetItemApplied(int index, int value)
        {
            if (!ItemOk(index))
                return;
            g_items[index].value = value;
            g_items[index].applied = value;
        }

        void    SetItemOptions(int index, const char *const *options, int count)
        {
            if (!ItemOk(index) || options == nullptr || count <= 0)
                return;
            if (count > 255)
                count = 255;
            g_items[index].options = options;
            g_items[index].optionCount = (u8)count;
            if (g_items[index].value >= count)
                g_items[index].value = 0;
            if (g_items[index].applied >= count)
                g_items[index].applied = 0;
        }

        const char *FormatItemHotkey(int index, char *buf, unsigned int cap)
        {
            if (buf == nullptr || cap == 0)
                return "";
            return FormatHotkey(ItemOk(index) ? g_items[index].hotkey : 0, buf, cap);
        }
    }
}

// ---- GuiDialog（Includes/Gui/GuiDialog.hpp）----
namespace GuiDialog
{
    void    ShowMessage(const char *title, const char *message, bool error)
    {
        CTRPluginFramework::GuiMenu::QueueMessage(title, message, error);
    }

    bool    IsOpen(void)
    {
        return CTRPluginFramework::GuiMenu::MessageOpen();
    }
}
