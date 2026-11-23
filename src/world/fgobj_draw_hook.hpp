#pragma once
#include "core/feature.hpp"
#include <cstdint>
namespace gohan::world {
class FgobjDrawHook {
  public:
    enum class Where { Before, After, BeforeLayer1 };
    using Draw = void (*)(std::uintptr_t, void *);
    // ゲームのフレーム処理から使う。外した後は userdata を返してよい。
    static bool Add(const void *owner, Where, Draw, void *userdata);
    static void Remove(const void *owner);
    static bool Available();
    static const core::Feature &Feature();
};
}
