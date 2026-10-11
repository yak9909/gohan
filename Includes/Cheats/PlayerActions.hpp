#pragma once
#include <3ds/types.h>
namespace PlayerActions {
void Wire();
void Service();
bool Tick(int index, u16 held);
bool Disable(int index);
}
