// ★自動生成。手で編集しないこと。
// 生成元: PATCHES/export_gui_v2.py
//         （ケーブの実体は PATCHES/cave_node_record.py の builder）
//
// 基準仕様 v2（BASELINE/gui_spec_v2/README.md）に対応する。
//   描画はゲームの Layout_RecordPaneTree がやるので、
//   ケーブは **A1 / A2（ノード登録）と N（安全スタブ）の 3 本だけ**、
//   フックは **RenderTop / RenderBottom の 2 本だけ**。
//
// ★実機で検証済みのワード列をそのまま持ち込む。C++ へ書き直さない。
#pragma once

// ★3DS の u32 は unsigned long。unsigned int で書くと型が合わない。
struct GuiCave { unsigned long base; unsigned long count; const unsigned long *words; const char *name; };
struct GuiHook { unsigned long addr; unsigned long orig; unsigned long branch; const char *name; };

static const unsigned long kGuiCaveA1[] = {
    0xE92D500F, 0xE59F0138, 0xE5900000, 0xE3500000, 0x0A000048, 0xE59F112C,
    0xE5912100, 0xE3520000, 0x1A000016, 0xE59F0124, 0xE59F1124, 0xEBE3BD56,
    0xE3A00001, 0xE59F110C, 0xE2811C01, 0xEBE3A468, 0xE59F0100, 0xE5900100,
    0xEBE3A347, 0xE59F0104, 0xE3A01010, 0xEBE3A3F7, 0xE59F00F4, 0xE5900000,
    0xEBE3A341, 0xE59F10DC, 0xE3A02000, 0xE5812114, 0xE3A02010, 0xE5812118,
    0xE3A02001, 0xE5C1211C, 0xE59F00BC, 0xE5900000, 0xE59F10B8, 0xE5903004,
    0xE3530000, 0x1A000003, 0xE3A03000, 0xE5813004, 0xE5813008, 0xEA000008,
    0xE590300C, 0xE1530001, 0x0A000020, 0xE5913004, 0xE3530000, 0x1A00001D,
    0xE5913008, 0xE3530000, 0x1A00001A, 0xE59F0070, 0xE5900000, 0xE59F2070,
    0xE5812000, 0xE3A020FF, 0xE5C1200C, 0xE3A02000, 0xE58120FC, 0xE59F2068,
    0xE5812028, 0xE59F2064, 0xE581202C, 0xE3A02000, 0xE5C12121, 0xE1A00001,
    0xEBF4BC58, 0xE59F0030, 0xE5900000, 0xE59F102C, 0xE3A02001, 0xE58120F8,
    0xE59120F8, 0xE3520000, 0x13A02001, 0x15C1211D, 0xE3A02000, 0xEBF4C200,
    0xE8BD500F, 0xE92D4010, 0xEAF4B8D9, 0x0096FC38, 0x009B5100, 0x009B5300,
    0x00000207, 0x009B0DF0, 0x00008000, 0x43C80000, 0x43700000,
};
static const unsigned long kGuiCaveA2[] = {
    0xE92D500F, 0xE59F0138, 0xE5900000, 0xE3500000, 0x0A000048, 0xE59F112C,
    0xE5912100, 0xE3520000, 0x1A000016, 0xE59F0124, 0xE59F1124, 0xEBE3BC70,
    0xE3A00001, 0xE59F110C, 0xE2811C01, 0xEBE3A382, 0xE59F0100, 0xE5900100,
    0xEBE3A261, 0xE59F0104, 0xE3A01010, 0xEBE3A311, 0xE59F00F4, 0xE5900000,
    0xEBE3A25B, 0xE59F10DC, 0xE3A02000, 0xE5812114, 0xE3A02010, 0xE5812118,
    0xE3A02001, 0xE5C1211C, 0xE59F00BC, 0xE5900000, 0xE59F10B8, 0xE5903010,
    0xE3530000, 0x1A000003, 0xE3A03000, 0xE5813004, 0xE5813008, 0xEA000008,
    0xE5903018, 0xE1530001, 0x0A000020, 0xE5913004, 0xE3530000, 0x1A00001D,
    0xE5913008, 0xE3530000, 0x1A00001A, 0xE59F0070, 0xE5900000, 0xE59F2070,
    0xE5812000, 0xE3A020FF, 0xE5C1200C, 0xE3A02001, 0xE58120FC, 0xE59F2068,
    0xE5812028, 0xE59F2064, 0xE581202C, 0xE3A02000, 0xE5C12121, 0xE1A00001,
    0xEBF4BB72, 0xE59F0030, 0xE5900000, 0xE59F102C, 0xE3A02001, 0xE58120F8,
    0xE59120F8, 0xE3520000, 0x13A02001, 0x15C1211D, 0xE3A02001, 0xEBF4C11A,
    0xE8BD500F, 0xE92D4010, 0xEAF4C05B, 0x0096FC38, 0x009B5400, 0x009B5300,
    0x00000207, 0x009B0DF0, 0x00008000, 0x43A00000, 0x43700000,
};
static const unsigned long kGuiCaveN[] = {
    0xE92D500F, 0xE59F0014, 0xE59F1014, 0xE59F2014, 0xEF000054, 0xE8BD500F,
    0xE3A00000, 0xE12FFF1E, 0xFFFF8001, 0x329D8500, 0x00004000,
};

static const GuiCave kGuiCaves[] = {
    { 0x00838950, 89, kGuiCaveA1, "A1" },
    { 0x00838CE8, 89, kGuiCaveA2, "A2" },
    { 0x00838E60, 11, kGuiCaveN, "N" },
};
static const unsigned long kGuiCaveCount = 3;

// ★入れるフックはこの 2 本だけ（基準仕様 v2 §1）。
//   どちらも「ノードをリストへ登録する」ためのもので、描画ではない。
static const GuiHook kGuiHooks[] = {
    { 0x00566DF8, 0xE92D4010, 0xEA0B46D4, "RenderTop" },
    { 0x00568F98, 0xE92D4010, 0xEA0B3F52, "RenderBottom" },
};
static const unsigned long kGuiHookCount = 2;

// ================================================================
// ★ゲーム側の入力を塞ぐケーブ（F-350）
//   sead::Controller::calc の中の BL sub_542E88 を差し替え、
//   生読みの直後・edge 計算の直前で hold とタッチ旗を削る。
//     SELECT(0x1000)      … 旗に関係なく**常に**落とす
//     スライドパッド(0xF00000) … **残す**（アナログ値は +0x11C/+0x120 で別に流れる）
//     それ以外のボタン     … 制御ブロック +0 が 1 のとき落とす
//     タッチ(+0x10 bit0)  … 制御ブロック +1 が 1 のとき落とす
//   ★CTRPF は HID を直接読むので自前メニューの操作とタッチは影響を受けない。
// ================================================================
static const unsigned long kGuiInputCave[] = {
    0xE59F305C, 0xE5D3C001, 0xE35C0000, 0x0A00000C, 0xE590C010, 0xE3CCC001,
    0xE580C010, 0xE590C108, 0xE35C0000, 0xBA000006, 0xE92D0010, 0xE3A04001,
    0xE1A04C14, 0xE590C110, 0xE1CCC004, 0xE580C110, 0xE8BD0010, 0xE590C110,
    0xE5D33000, 0xE3530000, 0x0A000001, 0xE59F300C, 0xE00CC003, 0xE580C110,
    0xEAF42B80, 0x009B7010, 0xFFF08764,
};
static const unsigned long kGuiInputCaveCount = 27;
static const unsigned long kGuiInputCaveBase  = 0x00838020;
static const unsigned long kGuiInputCtlIndex  = 25;   // CTL 番地のリテラル語位置
static const unsigned long kGuiInputCtl       = 0x009B7010;   // +0 ボタン遮断 / +1 タッチ遮断
static const unsigned long kGuiInputHookAddr  = 0x0053CDF4;
static const unsigned long kGuiInputHookOrig  = 0xEB001823;
static const unsigned long kGuiInputHookBl    = 0xEB0BEC89;

// ★ケーブ B — SELECT を「START への読み替え」の手前で消す（F-355）
//   ゲームは `sub_483020` で **SELECT(bit2) を START(bit3) に読み替えて
//   bit2 を消す**（SELECT は START の別名）。だから sead 層では区別できない。
//   読み替えの直前で bit2 だけ落とせば、SELECT だけが消えて START は残る。
static const unsigned long kGuiInputCaveB[] = {
    0xE59F3030, 0xE5D3C002, 0xE35C0000, 0x0A000008, 0xE590C000, 0xE3CCC004,
    0xE580C000, 0xE590C004, 0xE3CCC004, 0xE580C004, 0xE590C008, 0xE3CCC004,
    0xE580C008, 0xEAF12BD1, 0x009B7010,
};
static const unsigned long kGuiInputCaveBCount = 15;
static const unsigned long kGuiInputCaveBBase  = 0x008380A0;
static const unsigned long kGuiInputHookBAddr  = 0x003534E0;
static const unsigned long kGuiInputHookBOrig  = 0x0B04BECE;
static const unsigned long kGuiInputHookBBl    = 0x0B1392EE;   // ★条件は EQ のまま

// CAVE_N が吐き出すキャッシュの範囲。借りたヒープの番地に差し替える。
static const unsigned long kGuiCaveNTexVaIndex  = 9;
static const unsigned long kGuiCaveNTexLenIndex = 10;

// ---- 自前ノードの vtable（16 スロット）----
//   +0x08 = ゲームの Layout_ReplayRecordedList
//   +0x14 = ★ゲームの Layout_RecordPaneTree（v1 の CAVE_Q はもう無い）
//   +0x18 = ゲームの SetupProjectionMatrix
//   ほかは安全スタブ（CAVE_N）
static const unsigned long kGuiVtblReplay = 0x005682DC;
static const unsigned long kGuiVtblRecord = 0x005686D4;
static const unsigned long kGuiVtblSetPrj = 0x00567BC0;
static const unsigned long kGuiVtblStub   = 0x00838E60;

// ---- .bss の配置 ----
static const unsigned long kGuiLog        = 0x009B0D00;
static const unsigned long kGuiNodeTop    = 0x009B5100;
static const unsigned long kGuiVtbl       = 0x009B5300;
static const unsigned long kGuiNodeBot    = 0x009B5400;
static const unsigned long kGuiRootTop    = 0x009B6500;
static const unsigned long kGuiRootBot    = 0x009B6600;
static const unsigned long kGuiHeapSlot   = 0x009B7000;
static const unsigned long kGuiRootLen  = 0x100;
static const unsigned long kGuiNodeLen  = 0x140;
static const unsigned long kGuiLogWords = 64;

// ---- ノードの契約フィールド（sub_5685A4 と同じ値）----
static const unsigned long kGuiListSize   = 0x8000;   // ケーブに焼いてある既定
static const unsigned long kGuiListQCount = 16;
// ★実行時に画面ごとの値へ差し替える語の位置（A1 / A2 で同じ索引）
static const unsigned long kGuiListSizeIndex = 86;
static const unsigned long kGuiListSizeTop   = 0x28000;   // 上画面。F-343 で通知を 7 件にして記録の最悪値が 107,200 B に なったため 0x20000（131,072 B・81.8%）から引き上げた。 追加ぶんは nngx ヒープから 32,768 B（空きは 1,220,172 B。F-318）。
static const unsigned long kGuiListSizeBot   = 0x2D000;    // 下画面は表示物が少ない

// ---- ヒープの借用（基準仕様 v2 §4.1 / §4.7）----
static const unsigned long kLytAlloc     = 0x004B8CF0;   // AllocMemory(size, align)
static const unsigned long kLytFree      = 0x00569B44;   // Allocator::Free(this, ptr)
static const unsigned long kLytAllocator = 0x0096F150;
static const unsigned long kGuiBorrow    = 0x20000;

// ---- sead::ExpHeap の欄（F-309。vtable の取得子から確定）----
static const unsigned long kExpHeapVtable     = 0x8FFEF8;
static const unsigned long kAllocatorHeapOff  = 0x04;
static const unsigned long kExpStart          = 0x1C;
static const unsigned long kExpSize           = 0x20;
static const unsigned long kExpFreeHead       = 0x78;
static const unsigned long kExpFreeNext       = 0x7C;
static const unsigned long kExpFreeCount      = 0x80;
static const unsigned long kExpFreeBias       = 0x84;
static const unsigned long kExpUseCount       = 0x90;
static const unsigned long kExpBlkHdr         = 0x10;
static const unsigned long kExpBlkNext        = 0x04;
static const unsigned long kExpBlkSize        = 0x0C;
// 最悪時に自前 GUI へ配れる量（F-310。場面転換をまたいだ実測）
static const unsigned long kGuiWorstFree = 738876;

// ---- nw::lyt のオブジェクト（F-291）----
static const unsigned long kVtPane          = 0x008FBE7C;
static const unsigned long kVtPicture       = 0x008FBFFC;
static const unsigned long kVtMaterial      = 0x008FC18C;
static const unsigned long kVtTextBox       = 0x008FC07C;
static const unsigned long kVtResFont       = 0x008FCAAC;
static const unsigned long kVpDrawInfo      = 0x008FC17C;
static const unsigned long kGLayoutMgr      = 0x0096FC38;
static const unsigned long kTagProcSlot     = 0x0097C0B4;
static const unsigned long kTagProcDefault  = 0x008FCA24;
static const unsigned long kTagProcGuard    = 0x0097C0B0;
static const unsigned long kWorkObjOff = 0x1C;   // DrawInfo+0x80 = mgr+0x1C
static const unsigned long kPicStep    = 0x160;
static const unsigned long kShared     = 0x100;
static const unsigned long kBasePosCenter = 0x14;
static const unsigned long kFmtLA4     = 2;

// ---- 確保 / 解放ケーブ（PATCHES/alloc_lyt_heap.py の builder）----
//   ★アロケータは **ゲームのスレッド（フック内）から呼ぶ**こと。
//     プラグインのスレッドから直接呼んではいけない（DOCS/ctrpf_plugin.md 3 節）。
//   使い方: ケーブを書く -> Render_FrameEnd をフック -> スロットが埋まるまで待つ
//           -> フックを戻す -> ケーブをゼロ埋め
//   ★借りる大きさは実行時に kAllocSizeIndex の語を差し替えて決める。
static const unsigned long kAllocCaveBase = 0x00838100;
static const unsigned long kAllocCaveLimit = 0x00838200;
static const unsigned long kAllocHook = 0x004F1CE4;
static const unsigned long kAllocHookOrig = 0xE92D4070;
static const unsigned long kAllocCave[] = {
    0xE92D500F, 0xE59FC030, 0xE59C0000, 0xE3500000, 0x1A000006, 0xE59F0024,
    0xE3A01004, 0xEBF202F3, 0xE59FC014, 0xE58C0000, 0xE59F0010, 0xE58C0004,
    0xE8BD500F, 0xE92D4070, 0xEAF2E6EA, 0x009B7000, 0x00050000,
};
static const unsigned long kAllocCaveCount = 17;
static const unsigned long kAllocSizeIndex = 16;   // ここへ借りる大きさを書く
static const unsigned long kFreeCave[] = {
    0xE92D500F, 0xE59FC030, 0xE59C1000, 0xE3510000, 0x0A000006, 0xE59F0024,
    0xE5900000, 0xEBF4C688, 0xE59FC014, 0xE3A01000, 0xE58C1000, 0xE58C1004,
    0xE8BD500F, 0xE92D4070, 0xEAF2E6EA, 0x009B7000, 0x0096F150,
};
static const unsigned long kFreeCaveCount = 17;

// ---- コマンドリストの削除ケーブ（F-317。PATCHES/free_cmdlists.py）----
//   ★A1/A2 のフックを**外してから**使うこと。
//     外す前に削除すると、次のフレームで A1/A2 がまた作り直す。
//   ★束縛中のリストは消さない守りが入っている
//     （消すと g_GpuCmdPtr / End / Mgr が 0 にされる）。
//   完了印: *(unsigned long *)kDelDoneFlag が 1 になる。
static const unsigned long kDelCave[] = {
    0xE92D500F, 0xE59F008C, 0xE59F108C, 0xEBE3BF72, 0xE59FC088, 0xE59C0000,
    0xE3500000, 0x0A000009, 0xE59F1074, 0xE5911000, 0xE1500001, 0x0A000005,
    0xE3A00001, 0xE1A0100C, 0xEBE3BF16, 0xE59FC05C, 0xE3A00000, 0xE58C0000,
    0xE59FC054, 0xE59C0000, 0xE3500000, 0x0A000009, 0xE59F103C, 0xE5911000,
    0xE1500001, 0x0A000005, 0xE3A00001, 0xE1A0100C, 0xEBE3BF08, 0xE59FC028,
    0xE3A00000, 0xE58C0000, 0xE59FC020, 0xE3A00001, 0xE58C0000, 0xE8BD500F,
    0xE92D4070, 0xEAF2E6D3, 0x00000207, 0x009B0DF4, 0x009B5200, 0x009B5500,
    0x009B0DF8,
};
static const unsigned long kDelCaveCount = 43;
static const unsigned long kDelDoneFlag  = 0x009B0DF8;
static const unsigned long kDelScratch   = 0x009B0DF4;

// ---- 画面の実寸（1 単位 = 1px、原点は画面中央）----
static const unsigned long kGuiTopW = 400, kGuiTopH = 240;
static const unsigned long kGuiBotW = 320, kGuiBotH = 240;
