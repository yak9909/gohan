#include "game/encrypted_value.hpp"

namespace gohan::game {
static_assert(sizeof(generated::AcnlEncryptedValueF010) == 8);

bool EncryptedValue::ChecksumValid(const generated::AcnlEncryptedValueF010 &value) {
    const auto word = value.ciphertext;
    return value.checksum == static_cast<std::uint8_t>(word + (word >> 8) + (word >> 16) + (word >> 24) + 0xBA);
}

std::int32_t EncryptedValue::Maximum(Resource resource) {
    switch (resource) {
    case Resource::Wallet: case Resource::Turnips: return 99999;
    case Resource::Bank: return 999999999;
    case Resource::Coupons: case Resource::Medals: return 99999;
    }
    return 0;
}

generated::AcnlEncryptedValueF010 *EncryptedValue::Field(Resource resource) const {
    if (resource == Resource::Turnips) {
        auto *save = access_.garden();
        return save ? save->turnip_prices : nullptr;
    }
    auto *save = access_.player(4);
    if (!save) return nullptr;
    switch (resource) {
    case Resource::Wallet: return &save->wallet;
    case Resource::Bank: return &save->bank;
    case Resource::Coupons: return &save->meow_coupons;
    case Resource::Medals: return &save->medals;
    case Resource::Turnips: break;
    }
    return nullptr;
}

bool EncryptedValue::Read(Resource resource, std::int32_t *value) const {
    auto *field = Field(resource);
    if (!value || !field || !ChecksumValid(*field)) return false;
    const auto plain = access_.decrypt(field);
    if (plain > static_cast<std::uint32_t>(Maximum(resource))) return false;
    *value = static_cast<std::int32_t>(plain);
    return true;
}

Result EncryptedValue::Write(Resource resource, std::int32_t value) const {
    if (value < 0 || value > Maximum(resource)) return Result::Failed;
    auto *field = Field(resource);
    if (!field) return Result::NotReady;
    const unsigned count = resource == Resource::Turnips ? 12 : 1;
    for (unsigned i = 0; i < count; ++i) access_.encrypt(field + i, static_cast<std::uint32_t>(value));
    return Result::Ok;
}
} // namespace gohan::game
