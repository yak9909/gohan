#pragma once

#include <3ds.h>

// romfs の .bcres を選んでプレイヤーの足元へ 1 体出す。
//
// 描く手順は「テスト」フォルダのグリッドカーソルと同じ（資源ヒープ → 読み込み →
// Setup → モデル引き → instance 生成 → 姿勢 → 毎フレーム submit）。違うのは
//   * パスを実行時に決める（RomfsIndex）
//   * モデルは名前ではなく**ファイルの中の番号**で選ぶ。名前を持たずに済む
//   * SkeletalModel でなくてもよい。型は見るが弾かない
// という 3 点だけ。
namespace ModelViewer
{
    enum class Stage : u32
    {
        Off,
        AllocHeaps,
        LoadResource,
        SetupResource,
        FindModel,
        BuildInstance,
        Ready,
        Teardown,
        Failed,
    };

    namespace Fail
    {
        static const u32 kNone = 0;
        static const u32 kNoIndex = 1;          // RomfsIndex が作れていない
        static const u32 kNoPath = 2;           // 選ばれていない
        static const u32 kNoParentHeap = 3;
        static const u32 kResourceHeap = 4;
        static const u32 kInstanceHeap = 5;
        static const u32 kLoadGaveUp = 6;
        static const u32 kNotCgfx = 7;
        static const u32 kNoModels = 8;         // 中に CMDL が無い
        static const u32 kModelMissing = 9;
        static const u32 kCreateFailed = 10;
        static const u32 kActivatorNull = 11;
        static const u32 kSharedMeshArray = 12;
        static const u32 kNoPlayer = 13;
        static const u32 kNoScene = 14;
        static const u32 kHookFailed = 15;
        static const u32 kHeapExhausted = 16;
    }

    struct Status
    {
        Stage   stage;
        u32     failReason;
        u32     modelCount;     // 読めたファイルの中の CMDL の数
        u32     modelIndex;     // いま出しているもの
        u32     frames;         // 描画スレッドで数えたフレーム
        u32     submits;
        u32     resourceFree;
        u32     instanceFree;
    };

    // ---- メニュースレッドから ----
    void            SetPath(const char *romfsPath);   // 出す前に決める
    const char *    Path(void);
    void            SetModelIndex(s32 index);
    // 資源ヒープの大きさは .bcres のバイト数から決める。SetPath と一緒に呼ぶ。
    void            SetResourceHeapFromFileSize(u32 fileBytes);
    void            SetScalePercent(s32 percent);
    void            SetOffset(s32 x, s32 y, s32 z);   // プレイヤーからの相対。world 単位
    bool            Show(void);
    void            Hide(void);
    bool            IsShown(void);
    void            Shutdown(void);
    Status          Read(void);
    const char *    StageName(Stage stage);
    const char *    FailName(u32 reason);

    // GridCursor のスタブから毎フレーム回してもらう。
    void            FrameStep(void);
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WireModelViewer(void);
    }
}
