#pragma once

#include <3ds.h>

// フリーズ調査用の記録（2026-09-25。利用者報告: カーソルを 2 分ほど連続で動かしていて GPU の完了待ちで止まった）。
// GPU に関わる操作をしたときに {システムの tick, 記号, 値} を 64 件の輪に書く。ゲームの動きは変えない。
// 読み方: 停止中のデバッガで g_frameTrace（nm で番地を引く）を読み、g_frameTraceNext の 1 つ手前が最新。
namespace FrameTrace
{
    const u32 kEntries = 64;

    struct Entry { u32 tick; u16 code; u16 arg16; u32 arg; };

    // 記号（上位の桁で部品を分ける）
    enum Code : u16
    {
        ListBuildDone = 0x0101, ListEnter = 0x0102, ListLeave = 0x0103, ListDestroy = 0x0104, ListMove = 0x0105,
        ListFieldCmd = 0x0106,
        LabelBuild = 0x0201, LabelText = 0x0202, LabelDestroy = 0x0203, LabelHolderDone = 0x0204,
        GridStage = 0x0301, GridTeardown = 0x0302,
        HighlightSelect = 0x0401, HighlightClear = 0x0402,
        PreviewStage = 0x0501,
        WorksExecute = 0x0601,
        FrameMark = 0x0F01,
    };

    void Mark(u16 code, u32 arg = 0, u16 arg16 = 0);
}
