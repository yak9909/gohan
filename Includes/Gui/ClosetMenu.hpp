#pragma once
#include <3ds/types.h>

// native BsMenuChestを検索用の私有アイテム領域で開く。呼出しはplugin thread、
// native関数/vtable/list操作はゲームthreadだけ。通常の保存クローゼットには書かない。
namespace ClosetMenu {
struct Item { u16 id, flags; };
struct Slot { u32 index; Item item; };
bool Begin(bool returnToChat); // 表示を開始要求。検索/入場完了を待たない
bool Publish(const Item *items, u32 count, u32 initialSlot = 0);
bool PublishSlots(const Slot *slots, u32 count, u32 initialSlot = 0);
void Close();
bool Busy();
bool BlocksInput(); // native bodyへは入力を流す。切替/復帰中だけ止める
const char *TakeError();
void Tick();
void Shutdown();
}
