// DecorLayout — SD の DARC をゲームの nw::lyt で出す下回り。説明は DecorLayout.hpp。
// 番地は HhdScreen.cpp / DecorTrash.cpp / HiddenIcons.cpp（いずれも実機で動いている）と同じ。文字の幅は持ち物欄 sub_5E9430 の写し（IDA-opus-5.5-F051）。

#include "DecorLayout.hpp"

#include <cfloat>
#include <cstring>

namespace DecorLayout {

namespace {

typedef void *(*CtorFn)(void *self);
typedef int (*AttachFn)(void *accessor, void *arc, const char *root);
typedef int (*LayoutBuildFn)(void *layout, const char *name, void *holder, u32 cmdBytes);
typedef void (*LayoutFn)(void *layout);
typedef void (*AddLayoutFn)(void *mgr, void *layout, u32 screen);
typedef int (*RegisterTexFn)(void *holder);
typedef void (*FlushFn)(void *p, u32 size);
typedef void *(*HeapAllocFn)(void *heap, u32 size, u32 align);
typedef void (*HeapFreeFn)(void *heap, void *p);
typedef u32 (*HeapFreeSizeFn)(void *heap);
typedef void *(*FontSlotFn)(void *fontMgr, u32 kind);
typedef void (*RegisterFontFn)(void *accessor, const char *name, void *font);
typedef void *(*FindFn)(void *layout, const char *name);
typedef void *(*FindGroupFn)(void *layout, const char *name, u32 recursive);
typedef int (*AnimLoadFn)(void *anim, const char *name, void *holder);
typedef void (*GroupAnimFn)(void *layout, void *anim, void *group, u32 zero);
typedef void (*AnimSetFrameFn)(void *anim, float frame);
typedef int (*AnimFinishedFn)(const void *anim);
typedef int (*SetStringFn)(void *textBox, const u16 *str, u32 dst, u32 len);
typedef void (*GetTextureFn)(u32 *out, void *accessor, const char *name);
typedef void (*TexMapUpdateFn)(u32 texMap);
typedef void (*ScaleFn)(void *writer, float sx, float sy);
typedef float (*MeasureFn)(void *writer, const u16 *str, int len);

const CtorFn         ArcCtor        = reinterpret_cast<CtorFn>(0x00120D54);
const CtorFn         ArcDtor        = reinterpret_cast<CtorFn>(0x00567310);
const AttachFn       Attach         = reinterpret_cast<AttachFn>(0x004B5900);
const RegisterTexFn  RegisterTex    = reinterpret_cast<RegisterTexFn>(0x00568CFC);
const FlushFn        FlushRange     = reinterpret_cast<FlushFn>(0x0012D294);
const CtorFn         LayoutCtor     = reinterpret_cast<CtorFn>(0x0012653C);
const CtorFn         LayoutDtor     = reinterpret_cast<CtorFn>(0x005BEB4C);
const LayoutBuildFn  LayoutBuild    = reinterpret_cast<LayoutBuildFn>(0x005685A4);
const LayoutFn       LayoutFinalize = reinterpret_cast<LayoutFn>(0x00133A5C);
const LayoutFn       LayoutCalc     = reinterpret_cast<LayoutFn>(0x00568030);
const AddLayoutFn    AddLayout      = reinterpret_cast<AddLayoutFn>(0x0056928C);
const FontSlotFn     FontName       = reinterpret_cast<FontSlotFn>(0x00747528);
const FontSlotFn     FontGet        = reinterpret_cast<FontSlotFn>(0x0052D6A8);
const RegisterFontFn RegisterFont   = reinterpret_cast<RegisterFontFn>(0x004B3FD4);
const FindFn         FindPane       = reinterpret_cast<FindFn>(0x00567BAC);
const FindGroupFn    FindGroup      = reinterpret_cast<FindGroupFn>(0x004B4328);
const CtorFn         AnimCtor       = reinterpret_cast<CtorFn>(0x001261EC);
const CtorFn         AnimDtor       = reinterpret_cast<CtorFn>(0x00568CAC);
const AnimLoadFn     AnimLoad       = reinterpret_cast<AnimLoadFn>(0x00568A04);
const GroupAnimFn    GroupBind      = reinterpret_cast<GroupAnimFn>(0x00567A50);
const GroupAnimFn    GroupUnbind    = reinterpret_cast<GroupAnimFn>(0x00567DA4);
const AnimSetFrameFn AnimSetFrame   = reinterpret_cast<AnimSetFrameFn>(0x00568C00);
const LayoutFn       AnimStep       = reinterpret_cast<LayoutFn>(0x00568964);
const AnimFinishedFn AnimFinished   = reinterpret_cast<AnimFinishedFn>(0x0074F58C);
const HeapFreeSizeFn HeapFreeSize   = reinterpret_cast<HeapFreeSizeFn>(0x0074D744);   // sead::ExpHeap::getFreeSize
const SetStringFn    SetString      = reinterpret_cast<SetStringFn>(0x004BACBC);
const GetTextureFn   GetTexture     = reinterpret_cast<GetTextureFn>(0x004B5844);     // nwlyt_ArcResourceAccessor_GetTexture
const TexMapUpdateFn TexMapUpdate   = reinterpret_cast<TexMapUpdateFn>(0x004B9830);   // nwlyt_TexMap_UpdateGpuRegs
const CtorFn         WriterCtor     = reinterpret_cast<CtorFn>(0x007E8488);           // nw::font::TextWriter
const CtorFn         WriterDtor     = reinterpret_cast<CtorFn>(0x004D5BCC);
const ScaleFn        WriterScale    = reinterpret_cast<ScaleFn>(0x004D51B0);          // RecomputeScale(s0, s1)
const MeasureFn      WriterWidth    = reinterpret_cast<MeasureFn>(0x008268E0);        // nwfont_TextWriter_CalcStringWidth

const u32 kLayoutMgrPtr = 0x0096FC38;
const u32 kFontMgrPtr = 0x0094C9C8;
const u32 kLoaderPtr = 0x0096FC40, kLoaderHeap = 4;
const u32 kLoaderAlignPtr = 0x0096FC2C;
const u32 kHeapAllocSlot = 24 / 4, kHeapFreeSlot = 28 / 4;
const u32 kHolderArc = 8, kHolderAccessor = 0xC, kHolderArcLoaded = 0x158;
const u32 kLayoutHolder = 236, kLayoutPriority = 12;
const u32 kPaneFlags = 0xB7, kPaneX = 0x28, kPaneY = 0x2C, kPaneW = 0x48, kPaneH = 0x4C, kPaneAlpha = 180;
const u32 kPicMaterial = 0x13C, kMatFlags = 0x4D, kMatTexMaps = 52;
// TextBox の欄（sub_5E9430 が TextWriter へ写す）: +0xE0 書体、+0xE4 / +0xE8 大きさ、+0xEC / +0xF0 文字間・行間
const u32 kBoxFont = 0xE0, kBoxScale = 0xE4, kBoxCharSpace = 0xEC, kBoxLineSpace = 0xF0;
const u32 kWriterBytes = 0x74, kTagProcVtbl = 0x00903864;

inline u8 &B(void *p, u32 off) { return reinterpret_cast<u8 *>(p)[off]; }
inline u32 &W(void *p, u32 off) { return *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(p) + off); }
inline float &F(void *p, u32 off) { return *reinterpret_cast<float *>(reinterpret_cast<u8 *>(p) + off); }

inline bool IsHeapPointer(const void *p) {
    const u32 v = reinterpret_cast<u32>(p);
    return v >= 0x30000000u && v < 0x40000000u && (v & 3u) == 0u;
}

void *LoaderHeap(void) {
    const u32 loader = *reinterpret_cast<const volatile u32 *>(kLoaderPtr);
    if (!IsHeapPointer(reinterpret_cast<void *>(loader)))
        return nullptr;
    void *heap = *reinterpret_cast<void **>(loader + kLoaderHeap);
    return IsHeapPointer(heap) ? heap : nullptr;
}

}  // namespace

// ---- 資源 ----
void *HeapAlloc(u32 size, u32 align) {
    void *heap = LoaderHeap();
    if (heap == nullptr)
        return nullptr;
    void *p = reinterpret_cast<HeapAllocFn>((*reinterpret_cast<u32 **>(heap))[kHeapAllocSlot])(heap, size, align);
    return IsHeapPointer(p) ? p : nullptr;
}

void HeapFree(void *p) {
    void *heap = LoaderHeap();
    if (heap != nullptr && p != nullptr)
        reinterpret_cast<HeapFreeFn>((*reinterpret_cast<u32 **>(heap))[kHeapFreeSlot])(heap, p);
}

u32 HeapFreeBytes(void) {
    void *heap = LoaderHeap();
    return heap != nullptr ? HeapFreeSize(heap) : 0;
}

bool LoadArc(Arc &a, const u8 *file, u32 size) {
    a.heap = a.data = nullptr;
    a.made = false;
    void *heap = LoaderHeap();
    void *fontMgr = *reinterpret_cast<void *const *>(kFontMgrPtr);
    if (heap == nullptr || fontMgr == nullptr || FontGet(fontMgr, 0) == nullptr || file == nullptr || size == 0)
        return false;
    const u32 align = *reinterpret_cast<const volatile u32 *>(kLoaderAlignPtr);
    void *data = reinterpret_cast<HeapAllocFn>((*reinterpret_cast<u32 **>(heap))[kHeapAllocSlot])(heap, size, align);
    if (!IsHeapPointer(data))
        return false;
    a.heap = heap;
    a.data = data;
    std::memcpy(data, file, size);
    FlushRange(data, size);
    ArcCtor(a.holder);
    a.made = true;
    W(a.holder, kHolderArcLoaded) = reinterpret_cast<u32>(data);
    W(a.holder, kHolderArc) = reinterpret_cast<u32>(data);
    if (Attach(a.holder + kHolderAccessor, data, ".") == 0)
        return false;
    u32 *name = reinterpret_cast<u32 *>(FontName(fontMgr, 0));
    reinterpret_cast<void (*)(u32 *)>(reinterpret_cast<u32 *>(name[0])[2])(name);
    RegisterFont(a.holder + kHolderAccessor, reinterpret_cast<const char *>(name[1]), FontGet(fontMgr, 0));
    RegisterTex(a.holder);
    return true;
}

void FreeArc(Arc &a) {
    if (a.made)
        ArcDtor(a.holder);
    if (a.data != nullptr && a.heap != nullptr)
        reinterpret_cast<HeapFreeFn>((*reinterpret_cast<u32 **>(a.heap))[kHeapFreeSlot])(a.heap, a.data);
    a.heap = a.data = nullptr;
    a.made = false;
}

// ---- レイアウト ----
bool Build(Layout &l, Arc &a, const char *name, u32 cmdBytes, u8 priority) {
    l.made = l.built = false;
    LayoutCtor(l.obj);
    l.made = true;
    W(l.obj, kLayoutHolder) = reinterpret_cast<u32>(a.holder);
    if (LayoutBuild(l.obj, name, nullptr, cmdBytes) == 0)
        return false;
    l.built = true;
    l.obj[kLayoutPriority] = priority;
    return true;
}

void Free(Layout &l) {
    if (l.made) {
        if (l.built)
            LayoutFinalize(l.obj);
        LayoutDtor(l.obj);
    }
    l.made = l.built = false;
}

void Draw(Layout &l, u32 screen) {
    if (!l.built)
        return;
    void *mgr = *reinterpret_cast<void *const *>(kLayoutMgrPtr);
    LayoutCalc(l.obj);
    if (mgr != nullptr)
        AddLayout(mgr, l.obj, screen);
}

void *Pane(Layout &l, const char *name) { return l.built ? FindPane(l.obj, name) : nullptr; }
void *Group(Layout &l, const char *name) { return l.built ? FindGroup(l.obj, name, 1) : nullptr; }

// ---- アニメ ----
bool LoadAnim(Anim &an, Arc &a, const char *name) {
    an.lay = nullptr;
    an.group[0] = an.group[1] = nullptr;
    an.bound = false;
    AnimCtor(an.obj);
    an.made = true;
    return AnimLoad(an.obj, name, a.holder) != 0;
}

bool Bind(Anim &an, Layout &l, const char *group, float frame, const char *group2) {
    if (!an.made || !l.built)
        return false;
    Unbind(an);
    void *g = FindGroup(l.obj, group, 1);
    void *g2 = group2 != nullptr ? FindGroup(l.obj, group2, 1) : nullptr;
    if (g == nullptr || (group2 != nullptr && g2 == nullptr))
        return false;
    GroupBind(l.obj, an.obj, g, 0);
    if (g2 != nullptr)
        GroupBind(l.obj, an.obj, g2, 0);
    AnimSetFrame(an.obj, frame);
    an.lay = &l;
    an.group[0] = g;
    an.group[1] = g2;
    an.bound = true;
    return true;
}

void Unbind(Anim &an) {
    if (an.bound && an.lay != nullptr && an.lay->built)
        for (u32 i = 0; i < 2; ++i)
            if (an.group[i] != nullptr)
                GroupUnbind(an.lay->obj, an.obj, an.group[i], 0);
    an.bound = false;
    an.lay = nullptr;
    an.group[0] = an.group[1] = nullptr;
}

void FreeAnim(Anim &an) {
    Unbind(an);
    if (an.made)
        AnimDtor(an.obj);
    an.made = false;
}

bool Step(Anim &an) {
    if (!an.bound || AnimFinished(an.obj))
        return false;
    AnimStep(an.obj);
    return true;
}

bool Done(const Anim &an) { return !an.bound || AnimFinished(an.obj); }

void SetFrame(Anim &an, float frame) {
    if (an.made)
        AnimSetFrame(an.obj, frame);
}

// ---- ペイン ----
void SetVisible(void *pane, bool on) {
    if (pane != nullptr)
        B(pane, kPaneFlags) = (u8)((B(pane, kPaneFlags) & ~1u) | (on ? 1u : 0u));
}

bool Visible(const void *pane) {
    return pane != nullptr && (reinterpret_cast<const u8 *>(pane)[kPaneFlags] & 1u) != 0;
}

void SetAlpha(void *pane, u8 a) {
    if (pane != nullptr)
        B(pane, kPaneAlpha) = a;
}

void SetPos(void *pane, float x, float y) {
    if (pane == nullptr)
        return;
    F(pane, kPaneX) = x;
    F(pane, kPaneY) = y;
    B(pane, kPaneFlags) &= 0xCFu;               // 行列を作り直させる（HhdScreen::MovePaneX と同じ）
}

float PosX(const void *pane) { return pane != nullptr ? *reinterpret_cast<const float *>(reinterpret_cast<const u8 *>(pane) + kPaneX) : 0.0f; }
float PosY(const void *pane) { return pane != nullptr ? *reinterpret_cast<const float *>(reinterpret_cast<const u8 *>(pane) + kPaneY) : 0.0f; }

void SetSize(void *pane, float w, float h) {
    if (pane == nullptr)
        return;
    F(pane, kPaneW) = w;
    F(pane, kPaneH) = h;
    B(pane, kPaneFlags) &= 0xCFu;
}

float Width(const void *pane) { return pane != nullptr ? *reinterpret_cast<const float *>(reinterpret_cast<const u8 *>(pane) + kPaneW) : 0.0f; }
float Height(const void *pane) { return pane != nullptr ? *reinterpret_cast<const float *>(reinterpret_cast<const u8 *>(pane) + kPaneH) : 0.0f; }

void SetText(void *textBox, const u16 *text, u32 len) {
    if (textBox != nullptr)
        SetString(textBox, text, 0, len);
}

float MeasureText(void *textBox, const u16 *text, u32 len) {
    if (textBox == nullptr || text == nullptr || len == 0)
        return 0.0f;
    alignas(8) u8 w[kWriterBytes];
    WriterCtor(w);
    W(w, 0x3C) = W(textBox, kBoxFont);
    WriterScale(w, F(textBox, kBoxScale), F(textBox, kBoxScale + 4));
    W(w, 0x54) = W(textBox, kBoxCharSpace);
    W(w, 0x50) = W(textBox, kBoxLineSpace);
    F(w, 0x4C) = FLT_MAX;
    W(w, 0x68) = W(w, 0x24);
    W(w, 0x6C) = W(w, 0x28);
    W(w, 0x60) = reinterpret_cast<u32>(w + 0x64);
    W(w, 0x64) = kTagProcVtbl;
    W(w, 0x5C) = 0;
    const float width = WriterWidth(w, text, (int)len);
    WriterDtor(w);
    return width;
}

bool SetTexture(void *picture, u32 va, u16 w, u16 h, u32 format) {
    if (picture == nullptr || va == 0)
        return false;
    void *mat = reinterpret_cast<void *>(W(picture, kPicMaterial));
    if (!IsHeapPointer(mat))
        return false;
    const u32 t = W(mat, kMatTexMaps);
    if (!IsHeapPointer(reinterpret_cast<void *>(t)))
        return false;
    u32 *tm = reinterpret_cast<u32 *>(t);
    tm[0] = 0;
    tm[1] = va - 0x10000000u;                   // PA（リニアの FCRAM: VA = PA + 0x10000000。HiddenIcons と同じ）
    tm[2] = (u32)w | (u32)h << 16;
    tm[3] = (u32)w | (u32)h << 16;
    tm[4] = ((format << 8) & 0xF00u) | (tm[4] & 0xFFFFF0FFu);
    TexMapUpdate(t);
    B(mat, kMatFlags) &= ~4u;
    return true;
}

bool SetTextureByName(void *picture, Arc &a, const char *name) {
    if (picture == nullptr)
        return false;
    void *mat = reinterpret_cast<void *>(W(picture, kPicMaterial));
    if (!IsHeapPointer(mat))
        return false;
    u32 info[5] = { 0, 0, 0, 0, 0 };
    GetTexture(info, a.holder + kHolderAccessor, name);
    u32 *t = reinterpret_cast<u32 *>(W(mat, kMatTexMaps));
    if (info[1] == 0 || !IsHeapPointer(t))
        return false;
    t[0] = info[0];
    t[1] = info[1];
    t[2] = info[2];
    t[3] = info[3];
    t[4] = ((info[4] & 0xFFu) << 8 & 0xF00u) | (t[4] & ~0xF00u);
    TexMapUpdate(reinterpret_cast<u32>(t));
    B(mat, kMatFlags) &= ~4u;
    return true;
}

}  // namespace DecorLayout
