#include "game/encrypted_value.hpp"
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>

using namespace gohan::game;
using Value = generated::AcnlEncryptedValueF010;
namespace {
generated::AcnlPlayerSave player_save{};
generated::AcnlGardenSavePrefix garden_save{};
bool player_ready = true, garden_ready = true;
unsigned salt, rotation, encrypt_calls, decrypt_calls;
generated::AcnlPlayerSave *Player(unsigned slot) { assert(slot == 4); return player_ready ? &player_save : nullptr; }
generated::AcnlGardenSavePrefix *Garden() { return garden_ready ? &garden_save : nullptr; }
// ゲームの代役はビットを一つずつ循環させる。参照式の std::rotl/rotr とは独立に計算。
std::uint32_t Encrypt(Value *value, std::uint32_t plain) {
    ++encrypt_calls;
    value->salt = salt;
    value->rotation = rotation;
    auto word = plain + salt + 0x8F187432u;
    for (unsigned i = 0; i < rotation + 4; ++i) word = (word << 1) | (word >> 31);
    value->ciphertext = word;
    unsigned sum = 0xBA;
    for (unsigned i = 0; i < 4; ++i) sum += (word >> (i * 8)) & 255;
    value->checksum = sum;
    return word;
}
std::uint32_t Decrypt(const Value *value) {
    ++decrypt_calls;
    auto word = value->ciphertext;
    for (unsigned i = 0; i < value->rotation + 4u; ++i) word = (word >> 1) | (word << 31);
    return word - value->salt - 0x8F187432u;
}
const SaveAccess access{Player, Garden, Decrypt, Encrypt};
} // namespace
int main() {
    EncryptedValue value(access);
    const std::array<Resource, 5> resources{Resource::Wallet, Resource::Bank, Resource::Coupons, Resource::Medals, Resource::Turnips};
    std::int32_t read = -1;
    for (auto resource : resources) {
        const auto maximum = EncryptedValue::Maximum(resource);
        for (unsigned s : {0u, 1u, 32768u, 65535u}) {
            salt = s;
            for (rotation = 0; rotation < 26; ++rotation) {
                for (std::int32_t plain : {0, 1, maximum / 2, maximum}) {
                    encrypt_calls = 0;
                    assert(value.Write(resource, plain) == Result::Ok);
                    assert(encrypt_calls == (resource == Resource::Turnips ? 12u : 1u));
                    assert(value.Read(resource, &read) && read == plain);
                    const Value *field = resource == Resource::Wallet ? &player_save.wallet :
                        resource == Resource::Bank ? &player_save.bank :
                        resource == Resource::Coupons ? &player_save.meow_coupons :
                        resource == Resource::Medals ? &player_save.medals : garden_save.turnip_prices;
                    assert(field->ciphertext == std::rotl(static_cast<std::uint32_t>(plain) + salt + 0x8F187432u, rotation + 4));
                    assert(std::rotr(field->ciphertext, field->rotation + 4) - field->salt - 0x8F187432u == static_cast<unsigned>(plain));
                    if (resource == Resource::Turnips) {
                        for (const auto &price : garden_save.turnip_prices) assert(Decrypt(&price) == static_cast<unsigned>(plain));
                    }
                }
            }
        }
        assert(value.Write(resource, -1) == Result::Failed);
        assert(value.Write(resource, maximum + 1) == Result::Failed);
    }
    rotation = 7; salt = 123;
    assert(value.Write(Resource::Wallet, 0) == Result::Ok && value.Read(Resource::Wallet, &read) && read == 0);
    auto saved = player_save.wallet;
    for (unsigned byte = 0; byte < 4; ++byte) {
        player_save.wallet = saved;
        player_save.wallet.ciphertext ^= 1u << (byte * 8);
        decrypt_calls = 0;
        assert(!value.Read(Resource::Wallet, &read) && decrypt_calls == 0);
    }
    player_save.wallet = saved;
    player_save.wallet.checksum ^= 1;
    assert(!value.Read(Resource::Wallet, &read));
    Encrypt(&player_save.medals, 10000);
    assert(!value.Read(Resource::Medals, &read));
    // 先頭だけを読む。ほかの価格が違っていても先頭の値を返す。
    Encrypt(&garden_save.turnip_prices[0], 1234);
    Encrypt(&garden_save.turnip_prices[1], 5678);
    assert(value.Read(Resource::Turnips, &read) && read == 1234);
    player_ready = false;
    for (auto resource : resources) {
        if (resource == Resource::Turnips) continue;
        assert(!value.Read(resource, &read) && value.Write(resource, 1) == Result::NotReady);
    }
    garden_ready = false;
    assert(!value.Read(Resource::Turnips, &read) && value.Write(Resource::Turnips, 1) == Result::NotReady);
    std::puts("暗号化値の参照式・往復・検査和・セーブ欄・範囲: 成功");
}
