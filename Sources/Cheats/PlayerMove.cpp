// ============================================================================
// PlayerMove — 座標移動（gohan.md §17.1）とタッチワープ（§17.2）
// ============================================================================
//
// どちらもチェック項目の ToggleHandlers::OnTick（メニュースレッド・毎 16ms）で動く。
// プレイヤーの X（+0x14）と Z（+0x1C）だけを書く。高さ（+0x18）は触らない。
// オフラインで成立させる設計（オンラインの同期は対象外。gohan.md §17.0）。

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Cheats.hpp"
#include "GuiMenu.hpp"

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            // ---- ゲーム側（gohan.md §17.0）----
            const u32   kGetPlayer       = 0x005C27D8;  // GetPlayer(pIndex, 1) -> player*
            const u32   kGetOnlineIndex  = 0x00305F6C;  // 自分のプレイヤー番号
            const u32   kGetWorldCoords  = 0x005BFCE4;  // GetWorldCoords(&x, &y, pIndex, 1) -> bool
            const u32   kMapOpen         = 0x00949C30;  // 地図が開いている（bool）
            const u32   kCurrentRoom     = 0x0095133A;  // g_CurrentRoomId
            const u32   kOffX            = 0x14;
            const u32   kOffZ            = 0x1C;
            const u32   kOffState        = 0x1A9;
            const u8    kStateDig1       = 0x4F;        // スコップ動作中
            const u8    kStateDig2       = 0x52;

            typedef u32  (*GetPlayerFn)(u32 index, u32 one);
            typedef u8   (*GetIndexFn)(void);
            typedef bool (*WorldCoordsFn)(u32 *x, u32 *y, u32 index, u32 one);

            enum MoveKey  { KEY_DPAD = 0, KEY_CIRCLE, KEY_CSTICK };
            enum MoveMode { MODE_FREE = 0, MODE_GRID };
            enum MoveDpad { DPAD_KEEP = 0, DPAD_BLOCK_WHILE_MOVING };

            // ★スライドパッド／C スティックの読み値の最大（正規化用）と不感帯
            const float kCircleMax  = 156.0f;
            const float kCstickMax  = 146.0f;   // Vapecord cStickCoordinate と同じ
            const float kDeadZone   = 0.15f;    // 触っていない時の揺らぎで動かないように
            const float kGridTilt   = 0.5f;     // グリッド単位で 1 マス進める傾き
            const u32   kRepeatWait = 200;      // CONTROL_REPEAT と同じ
            const u32   kRepeatStep = 60;

            int     g_moveIndex = -1;
            int     g_keyIndex = -1;
            int     g_speedIndex = -1;
            int     g_modeIndex = -1;
            int     g_dpadIndex = -1;
            int     g_warpIndex = -1;

            int     g_gridDx = 0;
            int     g_gridDz = 0;
            u32     g_gridNext = 0;

            u32     NowMs(void)
            {
                return (u32)(svcGetSystemTick() / (u64)(SYSCLOCK_ARM11 / 1000));
            }

            u8      OwnIndex(void)
            {
                return ((GetIndexFn)kGetOnlineIndex)();
            }

            u32     OwnPlayer(u8 *index)
            {
                *index = OwnIndex();
                return ((GetPlayerFn)kGetPlayer)(*index, 1);
            }

            bool    Digging(u32 player)
            {
                const u8 state = *(u8 *)(player + kOffState);

                return state == kStateDig1 || state == kStateDig2;
            }

            float   Speed(void)
            {
                // ★float 項目は 1/10 単位の整数（2.0 -> 20）
                return (float)GuiMenu::ItemApplied(g_speedIndex) / 10.0f;
            }

            // 入力方向（x = 右が正、z = 下が正）。大きさは 0..1。
            void    ReadDirection(u16 held, u16 hotkey, float *dx, float *dz)
            {
                const int key = GuiMenu::ItemApplied(g_keyIndex);

                *dx = 0.0f;
                *dz = 0.0f;
                if (key == KEY_DPAD)
                {
                    // ホットキーに含まれる十字キーは移動に使わない
                    const u16 dir = held & (u16)~hotkey;

                    if (dir & (1u << GuiMenu::HB_RIGHT)) *dx += 1.0f;
                    if (dir & (1u << GuiMenu::HB_LEFT))  *dx -= 1.0f;
                    if (dir & (1u << GuiMenu::HB_DOWN))  *dz += 1.0f;
                    if (dir & (1u << GuiMenu::HB_UP))    *dz -= 1.0f;
                    return;
                }

                circlePosition pos = { 0, 0 };
                float          max = kCircleMax;

                if (key == KEY_CIRCLE)
                    hidCircleRead(&pos);
                else
                {
                    hidCstickRead(&pos);
                    max = kCstickMax;
                }
                float x = (float)pos.dx / max;
                float y = (float)pos.dy / max;

                if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
                if (y > 1.0f) y = 1.0f; else if (y < -1.0f) y = -1.0f;
                if (x * x + y * y < kDeadZone * kDeadZone)
                    return;
                *dx = x;
                *dz = -y;                       // パッドの上が +Y、ワールドの奥が -Z
            }

            void    ResetGrid(void)
            {
                g_gridDx = 0;
                g_gridDz = 0;
                g_gridNext = 0;
            }

            int     Round8(float v)
            {
                return v >= kGridTilt ? 1 : v <= -kGridTilt ? -1 : 0;
            }

            void    MoveTick(int index, u16 held)
            {
                const u16 hotkey = GuiMenu::ItemAppliedHotkey(index);

                // held はメニュー表示中 0。パッドを直接読むので表示中は自前でも止める
                if (GuiMenu::IsVisible() || hotkey == 0 || (held & hotkey) != hotkey)
                {
                    ResetGrid();
                    return;
                }

                // 十字キーの無効化: 移動キーが十字キーで「座標移動中」なら、ホットキーを押している間
                //   ゲーム側の十字キーを止める（入力遮断ケーブの値 2。CTRPF の held は影響を受けない）
                if (g_dpadIndex >= 0 && GuiMenu::ItemApplied(g_dpadIndex) == DPAD_BLOCK_WHILE_MOVING
                    && GuiMenu::ItemApplied(g_keyIndex) == KEY_DPAD)
                    GuiMenu::BlockGameDpad();

                u8        pIndex;
                const u32 player = OwnPlayer(&pIndex);

                if (player == 0 || Digging(player))
                {
                    ResetGrid();
                    return;
                }

                float dx, dz;

                ReadDirection(held, hotkey, &dx, &dz);
                if (GuiMenu::ItemApplied(g_modeIndex) != MODE_GRID)
                {
                    const float speed = Speed();

                    *(float *)(player + kOffX) += dx * speed;
                    *(float *)(player + kOffZ) += dz * speed;
                    return;
                }

                // グリッド単位: 8 方向に丸め、押し始めに 1 マス、200ms 後から 60ms ごとに 1 マス
                const int gx = Round8(dx);
                const int gz = Round8(dz);
                const u32 now = NowMs();

                if (gx == 0 && gz == 0)
                {
                    ResetGrid();
                    return;
                }
                if (gx == g_gridDx && gz == g_gridDz && now < g_gridNext)
                    return;

                u32 wx = 0, wy = 0;

                if (!((WorldCoordsFn)kGetWorldCoords)(&wx, &wy, pIndex, 1))
                    return;
                *(float *)(player + kOffX) = (float)(32 * (s32)(wx + gx) + 16);
                *(float *)(player + kOffZ) = (float)(32 * (s32)(wy + gz) + 16);
                g_gridNext = now + ((gx == g_gridDx && gz == g_gridDz) ? kRepeatStep : kRepeatWait);
                g_gridDx = gx;
                g_gridDz = gz;
            }

            // ---- タッチワープ（Vapecord PlayerClass::CalculateCoordinates の表）----
            struct WarpRect
            {
                u32   x, y, w, h;
                float offX, offY, scale;
            };

            bool    Inside(const WarpRect &r, u32 tx, u32 ty)
            {
                return tx >= r.x && tx < r.x + r.w && ty >= r.y && ty < r.y + r.h;
            }

            // 移動方法がグリッド単位の間は移動量を使わないので無効にする（編集中の値で判定）
            bool    SpeedDisabled(int index)
            {
                (void)index;
                return g_modeIndex >= 0 && GuiMenu::ItemValue(g_modeIndex) == MODE_GRID;
            }

            void    WarpTick(int index, u16 held)
            {
                static const WarpRect kTownClosed = {  70, 32, 180, 175,  30.0f,   5.0f, 14.2f };
                static const WarpRect kTownOpened = {   7, 32, 180, 175, -33.0f,   6.0f, 14.2f };
                static const WarpRect kIsland     = {  76, 36, 168, 168,  72.0f,  23.0f, 12.1f };
                static const WarpRect kMainStreet = {   4, 43, 312, 159, -16.0f,  55.0f,  6.2f };
                static const WarpRect kTour       = {  65, 34, 190, 170,  24.0f,  -7.0f, 13.5f };

                const u16 hotkey = GuiMenu::ItemAppliedHotkey(index);

                // ホットキーを設定していれば、押している間だけワープする
                if (hotkey != 0 && (held & hotkey) != hotkey)
                    return;
                if (GuiMenu::IsVisible() || *(u8 *)kMapOpen == 0 || !Touch::IsDown())
                    return;

                u8        pIndex;
                const u32 player = OwnPlayer(&pIndex);

                if (player == 0 || Digging(player))
                    return;

                const u8        room = *(u8 *)kCurrentRoom;
                const WarpRect *r = nullptr;

                if (room == 0x00)
                {
                    const u32 info = *(u32 *)(kMapOpen + 0x1C);

                    r = (info != 0 && *(u8 *)(info + 0x5D8) != 0) ? &kTownOpened : &kTownClosed;
                }
                else if (room == 0x68)
                    r = &kIsland;
                else if (room == 0x01)
                    r = &kMainStreet;
                else if (room >= 0x69 && room < 0x80)
                    r = &kTour;
                if (r == nullptr)
                    return;

                const UIntVector pos = Touch::GetPosition();

                if (!Inside(*r, pos.x, pos.y))
                    return;
                *(float *)(player + kOffX) = ((float)pos.x - r->offX) * r->scale;
                *(float *)(player + kOffZ) = ((float)pos.y - r->offY) * r->scale;
            }

            void    OnTick(int index, u16 held)
            {
                if (index == g_moveIndex)
                    MoveTick(index, held);
                else if (index == g_warpIndex)
                    WarpTick(index, held);
            }

            void    OnDisable(int index)
            {
                if (index == g_moveIndex)
                    ResetGrid();
            }

            const GuiMenu::ToggleHandlers kHandlers = { nullptr, OnTick, OnDisable };
        }

        void    WirePlayerMove(void)
        {
            g_moveIndex = GuiMenu::FindItem(kCoordMove);
            g_keyIndex = GuiMenu::FindItem(kCoordMoveKey);
            g_speedIndex = GuiMenu::FindItem(kCoordMoveSpeed);
            g_modeIndex = GuiMenu::FindItem(kCoordMoveMode);
            g_dpadIndex = GuiMenu::FindItem(kCoordMoveDpad);
            g_warpIndex = GuiMenu::FindItem(kTouchWarp);
            // 子設定が欠けていたら座標移動は動かさない
            if (g_keyIndex < 0 || g_speedIndex < 0 || g_modeIndex < 0)
                g_moveIndex = -1;
            ResetGrid();
            if (g_speedIndex >= 0)
                GuiMenu::RegisterDisabled(g_speedIndex, SpeedDisabled);
            GuiMenu::SetToggleHandlers(&kHandlers);
        }
    }
}
