#include "FrameTrace.hpp"

// 名前空間の外に置く（nm で g_frameTrace / g_frameTraceNext を引けるように）
extern "C" {
FrameTrace::Entry g_frameTrace[FrameTrace::kEntries];
volatile u32 g_frameTraceNext;
}

namespace FrameTrace {

void Mark(u16 code, u32 arg, u16 arg16) {
    const u32 i = g_frameTraceNext % kEntries;
    Entry &e = g_frameTrace[i];
    e.tick = (u32)svcGetSystemTick();
    e.code = code;
    e.arg16 = arg16;
    e.arg = arg;
    g_frameTraceNext = g_frameTraceNext + 1;
}

}  // namespace FrameTrace
