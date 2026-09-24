#ifndef GRID_CURSOR_HPP
#define GRID_CURSOR_HPP

// グリッドカーソル（gohan.md「テスト」フォルダ）。
//
// 村の地面に、ゲーム自身の `Ftr/Chip/UnitCursor.bcres` でグリッドカーソルを描く。
// プレイヤーの足元を基点に、十字キーで 1 マスずつ動かし、大きさを変えられる。
//
// 大きさの作り方について。**ゲーム自身の模様替えカーソルは面積を変えていない**。
// 家具 1 つにつきカーソル 1 つで、毎フレーム更新が組む行列は対角 1.0 の単位尺。
// 4 コマの `UnitCursorRotate` は `TextureCoordinators[1].Rotate` を π/4 と 3π/4 に振って
// 斜線の向きを家具の向きへ合わせるだけで、フレームを決める `0x00690B68` の引数も
// アイテム ID ではなく家具の 16bit 角度（返す 0〜3 は向きの象限）。IDA-opus-5-F028。
// だから任意の大きさは **1 マスにつき 1 体**で作る。12 体を毎フレーム動かして描いても
// フレーム落ちは測れなかった（IDA-opus-5-F017、実機）。
//
// ゲームへ触るのは**描画スレッド上の 1 か所だけ**（`FrameCallback`）。メニュー側は
// 要求を立てて状態を読むだけで、確保・生成・解放も全部あちらで起きる。
// ゲームのアロケータとシーンは別スレッドから触ってよいものではない。
//
// フックは一度入れたらプロセス終了まで置いたままにする。`Hide` は状態を Off にするだけで、
// そのときスタブがやるのは数え上げと比較が数回だけ。実行中のスタブを別スレッドから
// 消しに行かないための取り決めで、取り外しは `Shutdown`（`OnProcessExit`）でやる。

#include <3ds/types.h>

namespace GridCursor
{
    // 3x3 が今の最大。16 あれば作り直さずにもう少し大きいものも試せる。
    static const u32 kMaxCursors = 64;
    static const u32 kMaxSide = 8;

    enum class Stage : u32
    {
        Off,            // 何も確保していない
        AllocHeaps,     // 資源用・instance 用のヒープを親ヒープから作る
        LoadResource,   // UnitCursor.bcres を頼む。届くまでフレームをまたいで繰り返す
        SetupResource,  // ゲームに再配置・準備をさせる
        FindModel,      // 名前でモデルを引く
        BuildCursors,   // 1 マスにつき 1 体を生成・姿勢付け・アニメ結線
        Ready,          // 毎フレーム描いている
        Teardown,       // 描画を止めてから順に返す
        Failed,         // 検査で止まった。failReason にどれか
    };

    enum class Request : u32 { None, Setup, Teardown, Reposition, Resize };

    struct Status
    {
        Stage   stage;
        u32     failReason;
        u32     frames;         // シーン所有者の検査を通ったフレーム
        u32     submits;        // 実際にシーンへ渡したノード
        u32     cursors;        // いま建っている体数
        u8      footprintW;
        u8      footprintH;
        s16     col;            // 出したときの位置からの相対（マス）。画面の右が +
        s16     row;            // 画面の下が +
        u32     resourceFree;   // 描画スレッドで拾った資源ヒープの空き
        u32     instanceFree;
    };

    // どれで止まったか。描画スレッドが文字列を組まなくて済むように番号で持つ。
    namespace Fail
    {
        static const u32 kNone = 0;
        static const u32 kNotInVillage = 1;
        static const u32 kNoParentHeap = 2;
        static const u32 kResourceHeap = 3;
        static const u32 kInstanceHeap = 4;
        static const u32 kLoadGaveUp = 5;
        static const u32 kNotCgfx = 6;
        static const u32 kModelMissing = 7;
        static const u32 kModelWrongType = 8;
        static const u32 kCreateFailed = 9;
        static const u32 kActivatorNull = 10;   // ヒープ不足。描くと落ちる
        static const u32 kNodeWrongVtable = 11;
        static const u32 kNoPlayer = 12;
        static const u32 kAnimBuildFailed = 13;
        static const u32 kSharedMeshArray = 14; // 破棄が資源側へ書いてしまう形
        static const u32 kCaveOccupied = 15;    // 置き場が空いていない。何も書いていない
        static const u32 kHookNotNop = 16;      // フック先が期待の NOP ではない
        static const u32 kHeapExhausted = 17;   // instance ヒープの残りが 1 体分に満たない
    }

    // ---- メニュースレッドから呼ぶ ----

    // フックを入れて組み立てを頼む。置き場が空いていなければ**何も書かずに** false。
    bool            Show(void);
    // 描画を止めて instance・アニメ・ヒープを返し、済んだらフックを外す。
    void            Hide(void);
    bool            IsShown(void);          // 出ている（または出そうとしている）
    // マス指定の形で出す（建物エディター）。足元の形で出ているときは false。
    // 置く場所は SetTiles で村のマス (x, y) の並びとして渡す。体数が足りなければ組み直す。
    bool            ShowTiles(void);
    // shared なら全部を「マスの四角 [l,r]x[t,b] の中の地面の最大」（PublicWorks::LandHeight）に揃える。
    // 建物は基点 1 マス（建てたときの高さと同じ）、橋は足元の四角（岸の高さ）を渡す。
    void            SetTiles(const u8* xs, const u8* ys, u32 count, bool shared, u8 l, u8 t, u8 r, u8 b);
    // マス指定の形の色（0x00BBGGRR と強さ 0〜255。0 で元の見た目）。
    void            SetTint(u32 color, u8 strength);
    // プロセス終了時に 1 回。フック語を元の NOP へ戻し、置き場を消す。
    void            Shutdown(void);

    // ---- フックの相乗り ----
    // スタブは `.text` の空き 1 か所にしか置けないので、毎フレーム走りたい
    // チートはここへ相乗る。フック自体はプロセス終了まで入れたまま。
    bool            InstallFrameHook(void);              // 入っていなければ入れる
    // 描画スレッドで毎フレーム呼ばれる。複数のチートが相乗りできる（最大 4）。
    bool            AddExtraFrameStep(void (*fn)(void));
    u32             LastFailReason(void);                // フック入れに失敗した理由
    // 画面基準。右が +dCol、下が +dRow（IDA-opus-5-F032 の実機観測に合わせてある）。
    void            Move(int dCol, int dRow);
    void            SetFootprint(u32 width, u32 height);
    // マスの間隔（＝1 回の移動量、＝カーソルを並べる間隔）。world 単位。
    void            SetSpacing(s32 worldUnits);
    s32             Spacing(void);
    // カーソル自身の拡大率（百分率）。間隔とは独立。両方を実機で合わせ込むための分離。
    void            SetScalePercent(s32 percent);
    s32             ScalePercent(void);
    // 基点をマスの格子へ丸めるか。切ればプレイヤーの足元そのままになる。
    void            SetSnap(bool on);
    bool            Snap(void);
    // 縞模様を 45 度傾ける。切り替えると組み直す（数フレーム消える）。
    void            SetDiagonalStripes(bool on);
    bool            DiagonalStripes(void);
    Status          Read(void);
    const char *    StageName(Stage stage);
    const char *    FailName(u32 reason);

    // ゲームの描画スレッドから、スタブ経由で毎フレーム呼ばれる。
    extern "C" void FrameCallback(void);
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        // メニューの木を組んだ直後に 1 回。Wire から呼ぶ。
        void    WireGridCursor(void);
        // 一本化した配り手（Cheats.cpp）から回してもらう。自分の項目なら真を返す。
        bool    GridCursorTick(int index, u16 held);
        bool    GridCursorDisable(int index);
    }
}

#endif
