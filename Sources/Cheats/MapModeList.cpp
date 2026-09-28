#include "MapModeList.hpp"

#include <cstring>

// マップエディターのモード一覧 = ゲームの建物一覧 BsMenuMapList（IDA-opus-5.5-F062。解析は
// work/evidence/map_editor/mode_list/analysis.md）。番地は JPN 無印の更新版。
//
// 組み立ても選択の見た目もゲームの関数を呼ぶ。ゲームの地図（BsMenuMapVillage）がしていることのうち、
// 地図アイコン・開閉ボタン・ページ送りに関わる所だけを呼ばない。
//   組み立て   = 地図の生成段 4〜6（sub_2247B4 / sub_2245AC / sub_223D7C）と ListIn の項目登録（sub_224700 / sub_224334 / sub_224B88）
//   開いたまま = 部屋 164 でゲーム自身が使う sub_224E4C（in_list を最後のフレームで当て、矢印 P_list_arrow_00 を隠す）
//   行の決定   = ListProc の 0x2B34DC〜0x2B35B8（前の行 sub_2241E8(0) → 行 BAC の +212 bit3 を落とす → sub_2F72C4 → sub_2241E8(行, 1)）
//   選び直し   = 地図アイコンからの選択 sub_22392C（L / R でモードが変わったとき）
// 行 BAC（ButtonActionControl）はゲームのタッチ（0xACF6E0 の構造体）を読むが、エディターの間はゲームのタッチを止めている。
// そこで行 BAC を回す呼び出しの間だけ、一覧の上で始まった指をその構造体に書いて戻す（F060 の十字キーと同じ作法）。

namespace MapModeList {

namespace {

typedef void *(*CtorFn)(void *self);
typedef int (*ArcLoadStepFn)(void *holder, const char *path);
typedef void (*LayoutFn)(void *layout);
typedef void *(*FindPaneFn)(void *layout, const char *name);
typedef void (*AnimFn)(void *anim);
typedef int (*AnimFinishedFn)(void *anim);
typedef void (*AddLayoutFn)(void *mgr, void *layout, u32 screen);
typedef void *(*FontSlotFn)(void *fontMgr, u32 kind);
typedef void (*RegisterFontFn)(void *accessor, const char *name, void *font);
typedef void (*ListSetupFn)(void *list, void *layout, void *holder, u8 kind);
typedef void (*ListFn)(void *list);
typedef void (*ListAddEntryFn)(void *list, s32 index, s32 id, u8 category, u8 iconKind);
typedef void (*ListSetRowFn)(void *list, s32 row, s32 entry, const void *word, u8 visible);
typedef s32 (*ListSelectByIdFn)(void *list, s32 category, s32 id);
typedef void (*ListSetSelectedFn)(void *list, s32 row, s32 on);
typedef void (*ListAnimFrameFn)(void *list, s32 anim, float frame);      // フレームは VFP s0（hard-float でそのまま）
typedef void (*ListAnimFn)(void *list, s32 anim);
typedef void (*BacFn)(void *bac);
typedef s32 (*BacQueryFn)(void *bac);
typedef void (*NodePlayFn)(void *node, void *anim, s32 toEnd);
typedef void (*PaneRemoveFn)(void *parent, void *child);
typedef void (*PaneInsertFn)(void *parent, void *next, void *child);

// ---- ゲーム側 ----
const CtorFn         ArcCtor        = reinterpret_cast<CtorFn>(0x00120D54);         // ArcResAccReader（584 B）
const CtorFn         ArcDtor        = reinterpret_cast<CtorFn>(0x00567310);
const ArcLoadStepFn  ArcLoadStep    = reinterpret_cast<ArcLoadStepFn>(0x00567244);  // 終わるまで 0
const CtorFn         LayoutCtor     = reinterpret_cast<CtorFn>(0x0012653C);         // ssys::ma::lyt::Layout（332 B）
const CtorFn         LayoutDtor     = reinterpret_cast<CtorFn>(0x005BEB4C);
const LayoutFn       LayoutCalc     = reinterpret_cast<LayoutFn>(0x00568030);       // vc_ACSYSTEM_DATA2
const FindPaneFn     FindPane       = reinterpret_cast<FindPaneFn>(0x00567BAC);
const AnimFn         AnimStep       = reinterpret_cast<AnimFn>(0x00568964);
const AnimFinishedFn AnimFinished   = reinterpret_cast<AnimFinishedFn>(0x0074F58C);
const AddLayoutFn    AddLayout      = reinterpret_cast<AddLayoutFn>(0x0056928C);
// 書体の登録（地図の組み立て sub_222BC8 0x222BF8〜0x222C7C と同じ: スロット 0 と 1 を保持体 +12 へ。テクスチャの一括登録はしない）
const u32            kFontMgrPtr    = 0x0094C9C8;                                    // u32: font::Mgr
const FontSlotFn     FontName       = reinterpret_cast<FontSlotFn>(0x00747528);     // SafeString（vt+8 のあと +4 = char*）
const FontSlotFn     FontGet        = reinterpret_cast<FontSlotFn>(0x0052D6A8);     // 読み込み済みなら ResFont
const RegisterFontFn RegisterFont   = reinterpret_cast<RegisterFontFn>(0x004B3FD4); // (holder+12, 名前, 書体)
const u32            kHolderAccessor = 12;
// 建物一覧 BsMenuMapList（ctor 0x22500C / dtor 0x225210。村の地図 BsMenuMapVillage +48744 に埋め込み）
const CtorFn            ListCtor       = reinterpret_cast<CtorFn>(0x0022500C);
const CtorFn            ListDtor       = reinterpret_cast<CtorFn>(0x00225210);
const ListSetupFn       ListSetup      = reinterpret_cast<ListSetupFn>(0x002247B4);     // list_00.bclyt・アニメ 6 本（地図の生成段 4）
const ListFn            ListSetupRows  = reinterpret_cast<ListFn>(0x002245AC);          // 行の MapIcon・N_icon・T_msg（段 5）
const ListFn            ListSetupBtns  = reinterpret_cast<ListFn>(0x00223D7C);          // ボタン 3 系統（段 6）
const ListAddEntryFn    ListAddEntry   = reinterpret_cast<ListAddEntryFn>(0x00224700);  // 項目（最大 28）
const ListFn            ListPageState  = reinterpret_cast<ListFn>(0x00224334);          // ページボタンの有効・無効
const ListSetRowFn      ListSetRow     = reinterpret_cast<ListSetRowFn>(0x00224B88);    // 行 = 項目・語・表示
const ListFn            ListOpenFixed  = reinterpret_cast<ListFn>(0x00224E4C);          // 部屋 164 の常時オープン
const ListSelectByIdFn  ListSelectById = reinterpret_cast<ListSelectByIdFn>(0x0022392C);// 地図アイコンからの選択（同じなら 0）
const ListSetSelectedFn ListSetSelected = reinterpret_cast<ListSetSelectedFn>(0x002241E8); // 選択の帯 N_slct_00 と P_base_00 の α
const ListAnimFrameFn   ListAnimBind   = reinterpret_cast<ListAnimFrameFn>(0x00224F18); // アニメ i をグループへ結んでフレーム
const ListAnimFn        ListAnimUnbind = reinterpret_cast<ListAnimFn>(0x00223C74);      // アニメ i をグループから外す
const ListFn            ListRelease    = reinterpret_cast<ListFn>(0x00224DE8);          // 地図の段 B case 1（Layout と MapIcon と文字箱）
// ButtonActionControl / ButtonActionNode
const BacFn          BacUpdate      = reinterpret_cast<BacFn>(0x002F7744);          // 状態の関数（+184/+188）を呼ぶ
const BacQueryFn     BacDecided     = reinterpret_cast<BacQueryFn>(0x00722C34);     // +212 bit0
const BacQueryFn     BacDecidedRow  = reinterpret_cast<BacQueryFn>(0x0072294C);     // 決めたノードの +200（無ければ −2）
const BacFn          BacReset       = reinterpret_cast<BacFn>(0x002F72C4);          // 決定を解いて待ちへ
const u32            kBacWaitState  = 0x002F6810;                                    // 待ち（タッチなら vt+16、無ければ vt+20）
// nw::lyt::Pane の子（IDA-opus-5.5-F062: sub_4B6100 = InsertChild(next, child) = next の直前へ。描く順も next の直前）
const PaneRemoveFn   PaneRemove     = reinterpret_cast<PaneRemoveFn>(0x004B6130);
const PaneInsertFn   PaneInsert     = reinterpret_cast<PaneInsertFn>(0x004B6100);
// ゲームのタッチの構造体（sub_1B7874 がフレームごとに作る。+0 押している / +1 変わった / +2,+4 生の位置 / +6,+8 押し始め /
//   +10,+12 位置 / +14 押しているフレーム / +15 離しているフレーム / +16..+29 前のフレームの写し（+26 = 前の +0）/ +32,+36 レイアウト座標）
const u32 kTouchAddr = 0x00ACF6E0;
const u32 kTouchBytes = 40;

const char kArcPath[] = "Layout/Menu/map_village.arc";
const u32 kListBytes = 10568;               // ctor が書く最後の欄 +10560（u32）まで
const u32 kListLayout = 4, kListAnims = 12, kAnimStride = 40, kListRowBac = 8736, kListRowNodes = 8964, kNodeStride = 224;
const u32 kListSelRow = 10552;              // 帯を出している行（無ければ −1）
const s32 kAnimIn = 0, kAnimOut = 5;        // list_00_in / list_00_out（表 0x949CBC）
const u32 kListNodes = 7;                   // レイアウトの行 B_name_00..06
const u32 kBacFlags = 212, kBacState = 184, kBacStateAdj = 188;
const u32 kNodeSelected = 215, kNodeEnabled = 217, kNodeTouchOk = 112, kNodeSelectOk = 152, kNodePlaySlot = 52;
// Layout / Pane / Picture / TextBox の欄（F291 / F040 / F062）
const u32 kLayoutPriority = 12;
const u32 kPaneTranslateX = 40, kPaneTranslateY = 44, kPaneScaleX = 64, kPaneScaleY = 68, kPaneSizeX = 72, kPaneSizeY = 76;
const u32 kPaneBasePos = 182;               // 下位ニブル = 基準点（横 + 縦 × 3）
const u32 kPaneFlags = 183;                 // bit0 = 表示。SRT・大きさを書いたら 0x30 を落とす（ゲームと同じ & 0xCF）
const u32 kPicUvDone = 212, kPicMaterial = 316, kPicVtxColor = 320, kPicTexCoords = 340;
const u32 kTextPosition = 252, kTextDirty = 254;

// ---- 見た目（tools/layout/mode_list_preview.mjs の K と同じ。tools/strc/verify_map_editor.py が突き合わせる）----
const float kListX = -221.6f;               // N_list の x（開いた状態は 43）。P_tag の右端が盤面の枠の左端 67 に来る
const float kWinH = 90.5f;                  // W_list_win_00 の高さ（基準点を下 = 7 にして上端を保つ = 90.5 詰める）
const u8 kWinOrigin = 7;
const float kMidH = 57.5f;                  // P_list_win_01（148 − 90.5）
const float kMidY = -27.75f;                // P_list_win_01 の y（上端 77 を保つ）
const float kCapY = -56.5f;                 // P_list_win_02 の y（−147 + 90.5）
const float kMidFullH = 148.0f;
const u32 kNoteCut = 148;                   // 紙の上の段 = テクスチャの行 [0, 148)（行 2 の下の罫線を落とす）
const u32 kNoteLow = 240;                   // 紙の下の段 = [240, 256)（細縞の周期 4 の倍数 92 を詰める）
const float kNoteTexH = 256.0f;
const float kNoteTopY = 100.0f;             // 上の段の y（上端 174 を保つ）
const float kNoteLowY = 18.0f;              // 下の段の y（上端 26 = 上の段の下端）
const u8 kTextPos = 5;                      // 横 2 = 右、縦 1 = 中央
const u8 kLayoutPrio = 2;                   // 盤面と同じ。先に登録するので盤面の下
// 一覧が描く画素の外接矩形（mode_list_preview.mjs の mode_list_rect.json）。ここで始まった指は一覧が受け持つ
const u32 kRectX0 = 0, kRectX1 = 67, kRectY0 = 16, kRectY1 = 119;

// 行の語（script::WordPtr {vtbl 0x90491C, 文字列, 容量}。vt+12 = 文字列、vt+16 = 容量 → sub_5E91F0 が 容量 − 1 字を測る）
struct WordPtr { u32 vtbl; const char16_t *text; u32 cap; };
const u32 kWordPtrVtbl = 0x0090491C;
const WordPtr kWords[kRows] = {
    { kWordPtrVtbl, u"配置", 3 },
    { kWordPtrVtbl, u"削除", 3 },
    { kWordPtrVtbl, u"選択", 3 },
};
const WordPtr kEmptyWord = { kWordPtrVtbl, u"", 1 };

alignas(8) u8 s_holder[584];
alignas(8) u8 s_layout[332];
alignas(8) u8 s_list[kListBytes];
alignas(4) u8 s_touch[kTouchBytes];         // 一覧だけが見るタッチ（sub_1B7874 と同じ作り方）
bool s_holderMade, s_arcLoaded, s_layoutMade, s_listMade, s_built;
u32 s_buildStep;
const char *s_error = "";
s32 s_anim = -1;                            // 再生中の kAnimIn / kAnimOut
void *s_arrow, *s_arrowMaterial;            // 紙の下の段に使った矢印と、その元のマテリアル（壊す前に戻す）

inline u8 *P(void *p, u32 off) { return reinterpret_cast<u8 *>(p) + off; }
inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(P(p, off)); }
inline s32 &S(void *p, u32 off) { return *reinterpret_cast<s32 *>(P(p, off)); }
inline s16 &H(void *p, u32 off) { return *reinterpret_cast<s16 *>(P(p, off)); }
inline float &F(void *p, u32 off) { return *reinterpret_cast<float *>(P(p, off)); }
inline u8 &B(void *p, u32 off) { return *P(p, off); }

void *Layout(void) { return s_layout; }
void *RowBac(void) { return P(s_list, kListRowBac); }
void *Node(u32 row) { return P(s_list, kListRowNodes + kNodeStride * row); }
void *Anim(s32 i) { return P(s_list, kListAnims + kAnimStride * (u32)i); }

void NodePlay(void *node, u32 animOff, s32 toEnd) {
    const u32 *vt = *reinterpret_cast<u32 **>(node);
    reinterpret_cast<NodePlayFn>(vt[kNodePlaySlot / 4])(node, P(node, animOff), toEnd);
}

void Touched(void *pane) { B(pane, kPaneFlags) &= 0xCFu; }

void SetTexCoords(void *pic, float v0, float v1) {
    float *tc = reinterpret_cast<float *>(W(pic, kPicTexCoords));
    // 1 組 = 4 隅の (u, v)。リソースの並び（左上・右上・左下・右下）
    tc[0] = 0.0f; tc[1] = v0; tc[2] = 1.0f; tc[3] = v0;
    tc[4] = 0.0f; tc[5] = v1; tc[6] = 1.0f; tc[7] = v1;
    B(pic, kPicUvDone) = 0;                 // 次の描画で UV を作り直す（nwlyt_Picture_DrawToCommandList 0x73CB88）
}

// ---- 形（利用者の指定: 矢印とページ送りを隠す・真ん中を切って詰める・左へはみ出させる・文字を右へ）----
bool Shape(void) {
    void *L = Layout();
    void *win = FindPane(L, "W_list_win_00");
    void *mid = FindPane(L, "P_list_win_01");
    void *cap = FindPane(L, "P_list_win_02");
    void *note = FindPane(L, "P_list_note_00");
    void *arrow = FindPane(L, "P_list_arrow_00");
    void *slct = FindPane(L, "N_slct_00");
    void *btnR = FindPane(L, "P_list_btn_R");
    void *btnL = FindPane(L, "P_list_btn_L");
    void *nlist = FindPane(L, "N_list");
    if (win == nullptr || mid == nullptr || cap == nullptr || note == nullptr || arrow == nullptr || slct == nullptr
        || btnR == nullptr || btnL == nullptr || nlist == nullptr)
        return false;
    // ページ送り（ボタンの BAC は回さないので押せない）
    B(btnR, kPaneFlags) &= 0xFEu;
    B(btnL, kPaneFlags) &= 0xFEu;
    // 窓: 子（行・紙）の親なので位置は動かさず、基準点を下にして高さを縮める。辺は角の端を伸ばし中身は単色なので切ったのと同じ見た目
    B(win, kPaneBasePos) = (u8)((B(win, kPaneBasePos) & 0xF0u) | kWinOrigin);
    F(win, kPaneSizeY) = kWinH;
    Touched(win);
    // オレンジの中段: 上端を保って縮め、テクスチャは上端側（画面の上 = v 1。倍率 −1 で上下が逆）を同じ密度で使う
    F(mid, kPaneSizeY) = kMidH;
    F(mid, kPaneTranslateY) = kMidY;
    SetTexCoords(mid, 1.0f - kMidH / kMidFullH, 1.0f);
    Touched(mid);
    F(cap, kPaneTranslateY) = kCapY;
    Touched(cap);
    // 紙の上の段
    F(note, kPaneSizeY) = (float)kNoteCut;
    F(note, kPaneTranslateY) = kNoteTopY;
    SetTexCoords(note, 0.0f, (float)kNoteCut / kNoteTexH);
    Touched(note);
    // 紙の下の段 = 矢印の Picture を借りる（sub_224E4C が隠した物。マテリアルを紙の物に差し替え、壊す前に戻す）
    s_arrow = arrow;
    s_arrowMaterial = reinterpret_cast<void *>(W(arrow, kPicMaterial));
    W(arrow, kPicMaterial) = W(note, kPicMaterial);
    for (u32 i = 0; i < 4; ++i)
        W(arrow, kPicVtxColor + 4 * i) = W(note, kPicVtxColor + 4 * i);
    B(arrow, kPaneBasePos) = (u8)((B(arrow, kPaneBasePos) & 0xF0u) | (B(note, kPaneBasePos) & 0x0Fu));
    F(arrow, kPaneScaleX) = 1.0f;
    F(arrow, kPaneScaleY) = 1.0f;
    F(arrow, kPaneSizeX) = F(note, kPaneSizeX);
    F(arrow, kPaneSizeY) = kNoteTexH - (float)kNoteLow;
    F(arrow, kPaneTranslateX) = F(note, kPaneTranslateX);
    F(arrow, kPaneTranslateY) = kNoteLowY;
    SetTexCoords(arrow, (float)kNoteLow / kNoteTexH, 1.0f);
    Touched(arrow);
    B(arrow, kPaneFlags) |= 1u;
    PaneRemove(win, arrow);                 // 紙の直後（選択の帯と行の文字より前）に描く
    PaneInsert(win, slct, arrow);
    // 文字は右寄せ（左の画面外へはみ出させるので）
    char name[] = "T_msg_00";
    for (u32 i = 0; i < kListNodes; ++i) {
        name[7] = (char)('0' + i);
        void *t = FindPane(L, name);
        if (t == nullptr)
            return false;
        B(t, kTextPosition) = kTextPos;
        B(t, kTextDirty) |= 1u;
    }
    F(nlist, kPaneTranslateX) = kListX;
    Touched(nlist);
    B(L, kLayoutPriority) = kLayoutPrio;
    return true;
}

// ゲームのタッチの作り方（sub_1B7874）を一覧の指で回す
void TouchNext(bool held, u16 x, u16 y) {
    u8 *t = s_touch;
    H(t, 16) = H(t, 6);
    H(t, 18) = H(t, 8);
    H(t, 20) = H(t, 10);
    H(t, 22) = H(t, 12);
    t[24] = t[14];
    t[25] = t[15];
    t[26] = t[0];
    t[27] = t[1];
    H(t, 28) = H(t, 4);
    t[1] = (u8)(t[0] ^ (held ? 1u : 0u));
    t[0] = held ? 1u : 0u;
    if (held) {
        H(t, 2) = (s16)x;
        H(t, 4) = (s16)y;
        if (t[26] == 0) {                   // 押した瞬間
            H(t, 6) = H(t, 2);
            H(t, 8) = H(t, 4);
            t[14] = 0;
            ++t[14];
        } else if (t[14] < 200u) {
            ++t[14];
        }
        H(t, 10) = H(t, 2);
        H(t, 12) = H(t, 4);
    } else if (t[26] != 0) {                // 離した瞬間
        t[15] = 1;
    } else if (t[15] < 200u) {
        ++t[15];
    }
    F(t, 32) = (float)H(t, 10) - 160.0f;
    F(t, 36) = 120.0f - (float)H(t, 12);
}

void TouchReset(void) {
    std::memset(s_touch, 0, sizeof(s_touch));   // sub_1B7844 と同じ初期値
    H(s_touch, 2) = -1;
    H(s_touch, 4) = -1;
    s_touch[15] = 200;
    s_touch[25] = 200;
}

// 選んだ行の見た目を戻す（sub_22392C の末尾 0x223B58〜0x223B88 と同じ: +215 = 1、touch_ok と select_ok を最後のフレームへ）
void RestoreSelectedLook(u32 row) {
    void *node = Node(row);
    B(node, kNodeSelected) = 1;
    NodePlay(node, kNodeTouchOk, 1);
    NodePlay(node, kNodeSelectOk, (s8)B(node, kNodeSelected));
}

}  // namespace

bool LoadStep(void) {
    if (!s_holderMade) {
        ArcCtor(s_holder);
        s_holderMade = true;
    }
    if (s_arcLoaded)
        return true;
    if (ArcLoadStep(s_holder, kArcPath) == 0)
        return false;
    void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
    for (u32 k = 0; k < 2; ++k) {
        void *font = fontMgr != nullptr ? FontGet(fontMgr, k) : nullptr;
        if (font == nullptr) {
            s_error = u8"ゲームの書体が取れません";
            return false;
        }
        u32 *nameObj = reinterpret_cast<u32 *>(FontName(fontMgr, k));
        reinterpret_cast<void (*)(u32 *)>(reinterpret_cast<u32 *>(nameObj[0])[2])(nameObj);   // 終端をそろえる（ゲームと同じ）
        RegisterFont(P(s_holder, kHolderAccessor), reinterpret_cast<const char *>(nameObj[1]), font);
    }
    s_arcLoaded = true;
    return true;
}

bool BuildStep(bool heapOk, u8 mode) {
    if (s_built)
        return true;
    switch (s_buildStep) {
    case 0:
        if (!heapOk) {
            s_error = u8"nw::lyt のヒープが足りません";
            return false;
        }
        LayoutCtor(s_layout);
        s_layoutMade = true;
        ListCtor(s_list);
        s_listMade = true;
        ListSetup(s_list, s_layout, s_holder, 0);
        break;
    case 1:
        ListSetupRows(s_list);
        break;
    case 2:
        ListSetupBtns(s_list);
        break;
    default: {
        // 項目（ListIn の sub_2B2DEC と同じ形: 番号・id・種類・アイコン）→ ページボタン → 行
        for (u32 i = 0; i < kRows; ++i)
            ListAddEntry(s_list, (s32)i, (s32)i, 0, 0);
        ListPageState(s_list);
        for (u32 i = 0; i < kListNodes; ++i) {
            if (i < kRows)
                ListSetRow(s_list, (s32)i, (s32)i, &kWords[i], 1);
            else
                ListSetRow(s_list, (s32)i, 27, &kEmptyWord, 0);    // 空の行（sub_2B2368 と同じ項目 27・非表示）
        }
        ListOpenFixed(s_list);
        // ListProc の enter 0x2B309C と同じく行の入力を通す。使わない行は押せないように（IsEnabled +217）
        W(RowBac(), kBacFlags) &= ~8u;
        for (u32 i = kRows; i < kListNodes; ++i)
            B(Node(i), kNodeEnabled) = 0;
        if (!Shape()) {
            s_error = u8"list_00 の部品がありません";
            return false;
        }
        TouchReset();
        ListSelectById(s_list, 0, mode < kRows ? mode : 0);
        LayoutCalc(s_layout);
        s_built = true;
        return true;
    }
    }
    ++s_buildStep;
    return false;
}

const char *Error(void) {
    return s_error;
}

bool Built(void) {
    return s_built;
}

void Enter(void) {
    if (!s_built)
        return;
    if (s_anim >= 0)
        ListAnimUnbind(s_list, s_anim);
    ListAnimBind(s_list, kAnimIn, 0.0f);
    s_anim = kAnimIn;
}

void Leave(void) {
    if (!s_built)
        return;
    if (s_anim >= 0)
        ListAnimUnbind(s_list, s_anim);
    ListAnimBind(s_list, kAnimOut, 0.0f);
    s_anim = kAnimOut;
}

bool Animating(void) {
    return s_built && s_anim >= 0;
}

s32 Frame(u8 mode, bool held, u16 x, u16 y) {
    if (!s_built)
        return -1;
    if (s_anim >= 0) {
        void *a = Anim(s_anim);
        if (AnimFinished(a)) {
            ListAnimUnbind(s_list, s_anim);
            s_anim = -1;
        } else {
            AnimStep(a);
        }
    }
    // 行 BAC を回す間だけ、ゲームのタッチの構造体を一覧の指にする
    TouchNext(held, x, y);
    u8 saved[kTouchBytes];
    u8 *game = reinterpret_cast<u8 *>(kTouchAddr);
    std::memcpy(saved, game, kTouchBytes);
    std::memcpy(game, s_touch, kTouchBytes);
    void *bac = RowBac();
    BacUpdate(bac);
    std::memcpy(game, saved, kTouchBytes);
    s32 decided = -1;
    if (BacDecided(bac)) {
        const s32 row = BacDecidedRow(bac);
        if (row >= 0 && row < (s32)kRows) {
            // ListProc 0x2B34DC〜0x2B35B8 と同じ順（地図アイコンの強調は無い）
            const s32 before = S(s_list, kListSelRow);
            if (before > -1)
                ListSetSelected(s_list, before, 0);
            W(bac, kBacFlags) &= ~8u;
            BacReset(bac);
            ListSetSelected(s_list, row, 1);
            decided = row;
        } else {
            BacReset(bac);
        }
    }
    // 選択は解除できない: 指が離れて行 BAC が待ちに戻ったら、モードの行を選んだ見た目に戻す
    const bool idle = !held && (W(bac, kBacFlags) & 3u) == 0 && W(bac, kBacState) == kBacWaitState && W(bac, kBacStateAdj) == 0;
    const u32 want = decided >= 0 ? (u32)decided : (u32)mode;
    if (idle && want < kRows) {
        if (S(s_list, kListSelRow) != (s32)want)
            ListSelectById(s_list, 0, (s32)want);           // L / R で変わった（地図アイコンからの選択と同じ）
        else if (B(Node(want), kNodeSelected) == 0)
            RestoreSelectedLook(want);                      // 押したまま外へ出して離した
    }
    LayoutCalc(s_layout);
    return decided;
}

void Draw(void *layoutMgr) {
    if (s_built && layoutMgr != nullptr)
        AddLayout(layoutMgr, s_layout, 1);
}

bool Contains(u32 x, u32 y) {
    return s_built && s_anim < 0 && x >= kRectX0 && x < kRectX1 && y >= kRectY0 && y < kRectY1;
}

void Destroy(void) {
    if (s_arrow != nullptr) {               // 借りた矢印のマテリアルを戻す（Picture の dtor が自分のマテリアルを返す）
        W(s_arrow, kPicMaterial) = reinterpret_cast<u32>(s_arrowMaterial);
        s_arrow = s_arrowMaterial = nullptr;
    }
    if (s_listMade) {
        if (s_buildStep > 0 || s_built)
            ListRelease(s_list);            // 一覧の Layout と MapIcon のアニメを外し、文字箱を片付ける
        ListDtor(s_list);
    }
    s_listMade = false;
    if (s_layoutMade)
        LayoutDtor(s_layout);
    s_layoutMade = false;
    if (s_holderMade)
        ArcDtor(s_holder);
    s_holderMade = s_arcLoaded = false;
    s_built = false;
    s_buildStep = 0;
    s_anim = -1;
    s_error = "";
}

}  // namespace MapModeList
