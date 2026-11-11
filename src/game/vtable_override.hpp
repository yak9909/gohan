#pragma once
// 仮想表の見出し 2 語と主の表を写し、欄を差し替える。壊す時（Restore）に元へ戻す。
#include "game/write_log.hpp"
#include <cstddef>
#include <cstdint>

namespace gohan::game {
// Slots: 写す表の欄の数。Edits: 差し替える欄の最大数。
template <std::size_t Slots, std::size_t Edits = 8> class VtableOverride {
  public:
    // object の先頭語（表の番地）を写しに替える。写しは [0..1] が見出し 2 語。
    bool Install(std::uintptr_t *object_vtable_slot, const std::uintptr_t *original) {
        if (installed_ || !object_vtable_slot || !original) return false;
        for (std::size_t i = 0; i < Slots; ++i) copy_[i] = original[i];
        slot_ = object_vtable_slot;
        original_ = *slot_;
        return true;
    }
    bool Set(std::size_t index, std::uintptr_t value) {
        if (index >= Slots || edits_ >= Edits) return false;
        copy_[index] = value; ++edits_; return true;
    }
    bool Activate() {
        if (installed_ || !slot_) return false;
        installed_ = log_.Write(slot_, reinterpret_cast<std::uintptr_t>(copy_));
        return installed_;
    }
    void Restore() { log_.Restore(); installed_ = false; edits_ = 0; slot_ = nullptr; }
    bool Active() const { return installed_; }
    std::uintptr_t Slot(std::size_t i) const { return copy_[i]; }
    std::uintptr_t Original() const { return original_; }
  private:
    std::uintptr_t copy_[Slots]{};
    std::uintptr_t *slot_ = nullptr, original_ = 0;
    std::size_t edits_ = 0;
    bool installed_ = false;
    WriteLog<1> log_;
};
} // namespace gohan::game
