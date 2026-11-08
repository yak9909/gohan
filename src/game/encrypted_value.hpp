#pragma once
#include "game/generated/api.hpp"
#include "game/patch_set.hpp"

namespace gohan::game {
enum class Resource { Wallet, Bank, Coupons, Medals, Turnips };
struct SaveAccess {
    generated::AcnlPlayerSave *(*player)(unsigned slot);
    generated::AcnlGardenSavePrefix *(*garden)();
    std::uint32_t (*decrypt)(const generated::AcnlEncryptedValueF010 *);
    std::uint32_t (*encrypt)(generated::AcnlEncryptedValueF010 *, std::uint32_t);
};

class EncryptedValue {
  public:
    explicit EncryptedValue(const SaveAccess &access) : access_(access) {}
    bool Read(Resource resource, std::int32_t *value) const;
    Result Write(Resource resource, std::int32_t value) const;
    static bool ChecksumValid(const generated::AcnlEncryptedValueF010 &value);
    static std::int32_t Maximum(Resource resource);
  private:
    generated::AcnlEncryptedValueF010 *Field(Resource resource) const;
    const SaveAccess &access_;
};

bool WireResources();
void ShutdownResources();
bool ReadResource(Resource resource, std::int32_t *value);
Result WriteResource(Resource resource, std::int32_t value);
} // namespace gohan::game
