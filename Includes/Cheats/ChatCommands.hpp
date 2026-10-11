#pragma once
#include <3ds/types.h>
namespace ChatCommands {
void Wire();
void Tick(u16 held);
void Shutdown();
bool KeyboardPaused(); // ゲームスレッドのIME hookから。リスト退場中も入力を止める
bool HandleKeyboardHotkey(); // nativeの送信/取消判定より先にコマンドを捕捉する
}
