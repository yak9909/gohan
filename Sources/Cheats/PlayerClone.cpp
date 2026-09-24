#include "PlayerClone.hpp"

#include "Cheats.hpp"
#include "GridCursor.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

namespace PlayerClone {

namespace {

// ---- ゲームの関数（IDA-opus-5.5-F028 / F029）------------------------------------------------------
typedef void (*CtorFn)(void *pm);
typedef u32 (*CreateFromProfileFn)(void *pm, u32 profile, u32 a3, u32 maskBit, u32 a5, u32 a6);
typedef u32 (*DestroyStepFn)(void *pm);
typedef void (*DtorFn)(void *pm);
typedef void (*SetMatrixFn)(void *pm, const float *m);
typedef void (*ModelStepFn)(void *pm);
typedef void (*CalcAnimFn)(void *pm, u32 a2, u32 a3);
typedef u32 (*ProfileFn)(u32 index);
typedef void (*SubmitFn)(void *holder, u32 scene);

const CtorFn PlayerModelCtor = reinterpret_cast<CtorFn>(0x001D3854);
const CreateFromProfileFn CreateFromProfile = reinterpret_cast<CreateFromProfileFn>(0x001CF090);
const DestroyStepFn DestroyStep = reinterpret_cast<DestroyStepFn>(0x001D30D0);
const DtorFn PlayerModelDtor = reinterpret_cast<DtorFn>(0x001D3980);
const SetMatrixFn SetMatrix = reinterpret_cast<SetMatrixFn>(0x001CEC10);        // vtbl[9]
const ModelStepFn StepFaceTool = reinterpret_cast<ModelStepFn>(0x001D2BE8);     // vtbl[2]
const CalcAnimFn CalcAnim = reinterpret_cast<CalcAnimFn>(0x001CE8D8);
const ModelStepFn StepAttach = reinterpret_cast<ModelStepFn>(0x001D33FC);       // vtbl[4]
const ModelStepFn StepParts = reinterpret_cast<ModelStepFn>(0x001D2CCC);        // vtbl[5]
const ProfileFn PlayerProfile = reinterpret_cast<ProfileFn>(0x002FEB60);        // vc_PSOFFSET
const SubmitFn Submit = reinterpret_cast<SubmitFn>(0x004ED630);                 // Scene_SubmitNode

const u32 kPlayerPtr = 0x00AA7994;          // AcPlayer*
const u32 kPlayerMgrPtr = 0x0094A374;       // BsPlayerMgr*（部品バンクの管理役 = +24）
const u32 kRoomIdByte = 0x0095133A;         // 今の部屋（g_CurrentRoomId）
const u32 kSceneOwnerPtr = 0x00948E70;      // 場面の持ち主（部屋の切り替えで 0 になり、別の番地で戻る。GridCursor と同じ検査）
const u32 kBankMgrOffset = 24;
const u32 kActorModel = 436;                // AcPlayer + 436 = PlayerModel
const u32 kActorPlayerIndex = 428;          // u8
const u32 kModelBody = 120;                 // TransformNodeHolder（+4 がノード、ノード +0x4C が行列）
const u32 kHolderNode = 4;
const u32 kNodeMatrix = 0x4C;
const u32 kModelHair = 548;                 // u8
const u32 kModelHairColor = 552;            // u32
const u32 kModelBytes = 624;                // AcNpcDemoDollPlayer: +2852 から次の欄 +3476 まで
const u32 kProfileMaskByte = 22286;         // bit6 = Mii マスク（人形 0x2ECB30 と同じ）
const u32 kDollToolParam = 4;               // 人形の第 6 引数

const float kSideOffset = 30.0f;            // 体の行列の X 軸の向きへずらす量（world 単位）
const u32 kCreateLimit = 300;               // 10 秒（30fps）で作成が終わらなければ諦める

// 部品バンク（管理役からの位置と、1 体が取る枠の数）。PlayerModel_Setup 0x1CF474 が取るもの
struct BankNeed { u32 offset; u16 need; };
const BankNeed kBanks[] = {
    { 0, 1 }, { 156, 2 }, { 4148, 1 }, { 13292, 1 }, { 21680, 1 }, { 30348, 1 }, { 39016, 1 },
    { 47376, 1 }, { 51872, 1 }, { 60624, 1 }, { 76152, 2 }, { 76448, 2 },
};
const u32 kBankCount = 4;                   // BankTable +4 = 枠数
const u32 kBankUsed = 8;                    // BankTable +8 = 使用中の数（u16。BankTable_Acquire 0x2137CC が増やす）

enum Stage : u32 { kOff = 0, kCreating = 1, kLive = 2, kDestroying = 3, kFailed = 4 };

u8 s_model[kModelBytes] __attribute__((aligned(8)));
bool s_constructed;
u32 s_mgr;                                  // 作ったときの BsPlayerMgr
u32 s_player;                               // 作ったときの AcPlayer
u8 s_room;                                  // 作ったときの部屋
u32 s_owner;                                // 作ったときの場面の持ち主

volatile bool s_want;
volatile u32 s_stage = kOff;
volatile u32 s_fail;
volatile u32 s_frames;
volatile u32 s_submits;
volatile s32 s_hairStyle = -1;
volatile s32 s_hairColor = -1;
bool s_hooked;

u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
u16 R16(u32 a) { return *reinterpret_cast<const volatile u16 *>(a); }
u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }
bool IsHeap(u32 p) { return p >= 0x08000000u && p < 0x40000000u && (p & 3u) == 0u; }
u32 Model(void) { return reinterpret_cast<u32>(s_model); }

bool BanksHaveRoom(u32 mgr) {
    for (const BankNeed &b : kBanks) {
        const u32 table = mgr + b.offset;
        if ((u32)R16(table + kBankUsed) + b.need > R32(table + kBankCount))
            return false;
    }
    return true;
}

// 場面が変わった（管理役かプレイヤーが作り直された）。バンクごと消えているのでゲームの関数は呼ばない。
// 部屋の番号と場面の持ち主も見る（切り替えの途中は管理役の番地が同じまま片付けが進みうる）。
bool SceneChanged(void) {
    return R32(kPlayerMgrPtr) != s_mgr || R32(kPlayerPtr) != s_player || R8(kRoomIdByte) != s_room
        || R32(kSceneOwnerPtr) != s_owner;
}

void Abandon(u32 reason) {
    std::memset(s_model, 0, sizeof(s_model));
    s_constructed = false;
    s_fail = reason;
    s_stage = kFailed;
    s_frames = 0;
}

void ApplyHair(void) {
    const s32 style = s_hairStyle;
    const s32 color = s_hairColor;
    if (style >= 0 && style < 0x22 && R8(Model() + kModelHair) != (u8)style)
        *reinterpret_cast<volatile u8 *>(Model() + kModelHair) = (u8)style;
    if (color >= 0 && color < 0x10 && R32(Model() + kModelHairColor) != (u32)color)
        *reinterpret_cast<volatile u32 *>(Model() + kModelHairColor) = (u32)color;
}

// 本物の体の行列を、その X 軸の向きへずらす
void Pose(void) {
    const u32 node = R32(s_player + kActorModel + kModelBody + kHolderNode);
    if (!IsHeap(node))
        return;
    float m[12];
    std::memcpy(m, reinterpret_cast<const void *>(node + kNodeMatrix), sizeof(m));
    m[3] += m[0] * kSideOffset;
    m[7] += m[4] * kSideOffset;
    m[11] += m[8] * kSideOffset;
    SetMatrix(s_model, m);
}

void StepCreate(void) {
    const u32 player = R32(kPlayerPtr);
    const u32 manager = R32(kPlayerMgrPtr);
    if (!s_constructed) {
        if (!IsHeap(player) || !IsHeap(manager) || !IsHeap(R32(kSceneOwnerPtr))) {
            Abandon(1);
            return;
        }
        const u32 profile = PlayerProfile(R8(player + kActorPlayerIndex));
        if (profile == 0u) {
            Abandon(2);
            return;
        }
        if (!BanksHaveRoom(manager + kBankMgrOffset)) {
            Abandon(5);
            return;
        }
        std::memset(s_model, 0, sizeof(s_model));
        PlayerModelCtor(s_model);
        s_constructed = true;
        s_mgr = manager;
        s_player = player;
        s_room = R8(kRoomIdByte);
        s_owner = R32(kSceneOwnerPtr);
        s_frames = 0;
    }
    if (SceneChanged()) {
        Abandon(4);
        return;
    }
    const u32 profile = PlayerProfile(R8(s_player + kActorPlayerIndex));
    if (profile == 0u) {
        s_stage = kDestroying;                      // 作りかけを片付ける
        s_frames = 0;
        return;
    }
    const u32 maskBit = (R8(profile + kProfileMaskByte) >> 6) & 1u;
    if (CreateFromProfile(s_model, profile, 0, maskBit, 0, kDollToolParam) != 0u) {
        s_stage = kLive;
        s_frames = 0;
        return;
    }
    if (++s_frames >= kCreateLimit) {
        s_fail = 3;
        s_stage = kDestroying;
        s_frames = 0;
    }
}

void StepLive(void) {
    if (SceneChanged()) {
        Abandon(4);
        return;
    }
    ApplyHair();
    Pose();
    StepFaceTool(s_model);
    CalcAnim(s_model, 1, 1);
    StepAttach(s_model);
    StepParts(s_model);
    Submit(reinterpret_cast<void *>(Model() + kModelBody), 0);
    ++s_submits;
    ++s_frames;
}

void StepDestroy(void) {
    if (!s_constructed) {
        s_stage = s_fail != 0u ? kFailed : kOff;
        return;
    }
    if (SceneChanged()) {
        Abandon(4);
        return;
    }
    ++s_frames;
    if (DestroyStep(s_model) == 0u && s_frames < kCreateLimit)
        return;
    PlayerModelDtor(s_model);
    std::memset(s_model, 0, sizeof(s_model));
    s_constructed = false;
    s_stage = s_fail != 0u ? kFailed : kOff;
    s_frames = 0;
}

}  // namespace

void FrameStep(void) {
    const u32 stage = s_stage;
    if (!s_want) {
        if (stage == kCreating || stage == kLive) {
            s_stage = kDestroying;
            s_frames = 0;
        } else if (stage == kDestroying) {
            StepDestroy();
        }
        return;
    }
    switch (stage) {
    case kOff:
        s_fail = 0;
        s_submits = 0;
        s_stage = kCreating;
        StepCreate();
        return;
    case kCreating:
        StepCreate();
        return;
    case kLive:
        StepLive();
        return;
    case kDestroying:
        StepDestroy();
        return;
    default:
        return;                                     // 失敗: 切って入れ直すまで何もしない
    }
}

bool Show(void) {
    if (!s_hooked) {
        if (!GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep))
            return false;
        s_hooked = true;
    }
    if (s_stage == kFailed && !s_constructed)
        s_stage = kOff;
    s_want = true;
    return true;
}

void Hide(void) {
    s_want = false;
    if (s_stage == kFailed && !s_constructed)
        s_stage = kOff;
}

bool IsShown(void) {
    return s_want;
}

void SetHair(s32 style, s32 color) {
    s_hairStyle = style;
    s_hairColor = color;
}

Status Read(void) {
    Status s;
    s.stage = s_stage;
    s.failReason = s_fail;
    s.frames = s_frames;
    s.submits = s_submits;
    s.hair = s_constructed ? R8(Model() + kModelHair) : 0;
    s.hairColor = s_constructed ? R32(Model() + kModelHairColor) : 0;
    return s;
}

const char *StageName(u32 stage) {
    switch (stage) {
    case kOff: return u8"無し";
    case kCreating: return u8"作成中";
    case kLive: return u8"表示中";
    case kDestroying: return u8"片付け中";
    case kFailed: return u8"失敗";
    default: return u8"?";
    }
}

const char *FailName(u32 reason) {
    switch (reason) {
    case 1: return u8"プレイヤーがいない";
    case 2: return u8"プロフィールが無い";
    case 3: return u8"作成が終わらない";
    case 4: return u8"場面が変わった";
    case 5: return u8"部品の空き枠が無い";
    default: return u8"";
    }
}

}  // namespace PlayerClone

// ---------------------------------------------------------------------------------------
// gohan のメニューとの結び付け（gohan.md「テスト」フォルダ）
// ---------------------------------------------------------------------------------------

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            int             g_pcShowIndex = -1;
            int             g_pcHairIndex = -1;
            int             g_pcColorIndex = -1;

            void    HairApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                PlayerClone::SetHair(g_pcHairIndex >= 0 ? GuiMenu::ItemApplied(g_pcHairIndex) : -1,
                                     g_pcColorIndex >= 0 ? GuiMenu::ItemApplied(g_pcColorIndex) : -1);
            }

            bool    CloneIsActive(int index)
            {
                (void)index;
                return PlayerClone::IsShown();
            }

            void    CloneSetActive(int index, bool active)
            {
                (void)index;
                if (!active)
                {
                    PlayerClone::Hide();
                    return;
                }
                HairApplied(0, 0);
                if (!PlayerClone::Show())
                    GuiNotification::NotifyRed(kPcShow, u8"フックを入れられない");
            }

            const GuiMenu::ToggleEffectFuncs kCloneFuncs = { CloneIsActive, CloneSetActive };

            void    CloneStatus(int index)
            {
                (void)index;
                const PlayerClone::Status s = PlayerClone::Read();
                static char message[96];

                if (s.failReason != 0u)
                    std::snprintf(message, sizeof(message), u8"%s: %s", PlayerClone::StageName(s.stage),
                                  PlayerClone::FailName(s.failReason));
                else
                    std::snprintf(message, sizeof(message), u8"%s %luF 提%lu 髪%u/%lu", PlayerClone::StageName(s.stage),
                                  (unsigned long)s.frames, (unsigned long)s.submits, (unsigned)s.hair,
                                  (unsigned long)s.hairColor);
                GuiNotification::Notify(kPcStat, message);
            }
        }

        void    WirePlayerClone(void)
        {
            g_pcShowIndex = GuiMenu::FindItem(kPcShow);
            g_pcHairIndex = GuiMenu::FindItem(kPcHair);
            g_pcColorIndex = GuiMenu::FindItem(kPcColor);
            const int statIndex = GuiMenu::FindItem(kPcStat);

            if (g_pcShowIndex >= 0)
                GuiMenu::RegisterToggleEffect(g_pcShowIndex, &kCloneFuncs);
            if (g_pcHairIndex >= 0)
                GuiMenu::RegisterApply(g_pcHairIndex, HairApplied);
            if (g_pcColorIndex >= 0)
                GuiMenu::RegisterApply(g_pcColorIndex, HairApplied);
            if (statIndex >= 0)
                GuiMenu::RegisterExecute(statIndex, CloneStatus);
        }
    }
}
