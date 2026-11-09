#pragma once
#include "game/generated/api.hpp"
#include <cstdint>

namespace gohan::game::player {
// その場で動く自分のアクター。通信番号から取り、ホストの枠とは区別する。
// セーブとは寿命も構造も異なり、部屋の移動中は取得できないことがある。
void *LocalActor();
// 自分の永続データ。所持金等を持つセーブであり、座標や動作を持つアクターではない。
generated::AcnlPlayerSave *LocalSave();
unsigned LocalIndex();
bool Digging(void *actor);
bool WorldGrid(unsigned &x, unsigned &z);
void Translate(void *actor, float x, float z);
void SetXZ(void *actor, float x, float z);
} // namespace gohan::game::player
