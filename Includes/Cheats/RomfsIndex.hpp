#pragma once

#include <3ds.h>

// ゲーム自身の RomFS を実行時に歩いて、.bcres のパス一覧を作る。
//
// 名前を 3gx へ焼き込まない。パスとモデル名を素で載せると約 253 KB、フォルダを括っても
// 約 105 KB になるが、**RomFS の中にその表がそのまま入っている**ので読めばよい
// （利用者提案、2026-09-22）。gohan は漢字変換で既に RomFS のディレクトリ表を扱っており、
// 実機で動いている仕組みをそのまま使う。
//
// 索引は頼まれたときに 1 回だけ作る。歩き終わったらディレクトリ表とファイル表は返し、
// 残すのはパスの列だけ（実測: base で 336,906 B、歩行中のピークが 1,354,786 B）。
//
// ★実機で数えた結果、`ARCHIVE_ROMFS` は **base だけ**を返す（8,629 本。IDA-opus-5-F037）。
//   更新タイトルにしか無いパスが 1,801 本、うち 741 本は描画できるモデルを持つので
//   （Item/Model 540、住民 128）、**更新タイトルの RomFS も続けて読む**。
//   両方にある 106 本は一覧に 2 行出るが、ゲーム側がパスを解決するので害は無い。
namespace RomfsIndex
{
    enum class Fail : u32
    {
        None,
        OpenFailed,      // RomFS アーカイブが開けない
        BadHeader,       // level 3 の 40 バイトヘッダが読めない
        ReadFailed,      // 表の読み出しが途中で切れた
        OutOfMemory,     // 表やパス列を置く場所が取れない
        WalkFailed,      // 表の鎖が壊れている
    };

    bool            Build(void);            // 済んでいれば何もしないで true
    bool            Ready(void);
    Fail            Reason(void);
    u32             ErrorCode(void);        // 失敗した FS 呼び出しの Result など

    u32             Count(void);            // 見つかった .bcres の本数
    u32             UpdateCount(void);      // そのうち更新タイトルから来た分
    u32             UpdateOpenResult(void); // 更新タイトルを開いたときの Result
    const char *    PathAt(u32 index);      // 見つからなければ ""
    u32             SizeAt(u32 index);      // その .bcres のバイト数。
                                            // 資源ヒープの大きさをこれから決める

    // start 番目から cap 件を names[] へ。メニューのリスト項目は 255 件までしか
    // 持てないので（optionCount が u8）、窓をずらして見る。戻り値は入った件数。
    // names[] が指す文字列は次に Window を呼ぶまで有効。
    u32             Window(u32 start, const char **names, u32 cap);

    const char *    ReasonName(Fail reason);
}
