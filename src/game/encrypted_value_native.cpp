#include "game/encrypted_value.hpp"
#include "game/player.hpp"
#include <CTRPluginFramework.hpp>

namespace gohan::game {
namespace {
generated::AcnlPlayerSave *Player(unsigned slot) {
    if (slot == 4) return player::LocalSave();
    return reinterpret_cast<generated::Save_GetPlayerByOnlineSlot_t>(generated::Save_GetPlayerByOnlineSlot_addr)(slot);
}
generated::AcnlGardenSavePrefix *Garden() {
    return *reinterpret_cast<generated::g_GardenSaveBasePtr_t *>(generated::g_GardenSaveBasePtr_addr);
}
std::uint32_t Decrypt(const generated::AcnlEncryptedValueF010 *value) {
    return reinterpret_cast<generated::Save_DecryptValue_t>(generated::Save_DecryptValue_addr)(value);
}
std::uint32_t Encrypt(generated::AcnlEncryptedValueF010 *value, std::uint32_t plain) {
    return reinterpret_cast<generated::Save_EncryptValue_t>(generated::Save_EncryptValue_addr)(value, plain);
}
const SaveAccess access{Player, Garden, Decrypt, Encrypt};
const EncryptedValue values(access);
bool ready = false;
} // namespace

bool WireResources() {
    // 呼び出す関数の入口を照合し、ゲームの更新違いではセーブに触らない。
    const PatchWord checks[] = {
        {generated::SaveDecryptEntry_addr, 0, generated::SaveDecryptEntry_crc32},
        {generated::SaveEncryptEntry_addr, 0, generated::SaveEncryptEntry_crc32},
        {generated::SavePlayerEntry_addr, 0, generated::SavePlayerEntry_crc32},
    };
    PatchSet probe(NativePatchMemory(), checks);
    ready = CTRPluginFramework::Process::GetTitleID() == 0x0004000000086200ULL && probe.Wire() == Result::Ok;
    return ready;
}
void ShutdownResources() { ready = false; }
bool ReadResource(Resource resource, std::int32_t *value) { return ready && values.Read(resource, value); }
Result WriteResource(Resource resource, std::int32_t value) {
    return ready ? values.Write(resource, value) : Result::Unsupported;
}
} // namespace gohan::game
