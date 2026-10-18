#include "core/feature.hpp"
#include "core/format.hpp"
#include "core/frame.hpp"
#include "core/mailbox.hpp"
#include "core/request.hpp"
#include "core/utf.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace gohan::core;

namespace {
void TestFormat() {
    char buffer[96];
    TextWriter writer(buffer, sizeof(buffer));
    writer.Append("値:")
        .Signed(std::numeric_limits<std::int32_t>::min())
        .Append("/")
        .Unsigned(std::numeric_limits<std::uint32_t>::max())
        .Append("/")
        .Unsigned(42, 16, 8)
        .Append("/")
        .Unsigned(0)
        .Append("/")
        .Signed(123);
    assert(writer.Complete());
    assert(std::string(buffer) == "値:-2147483648/4294967295/0000002A/0/123");
    assert(writer.Size() == std::strlen(buffer));
    char guard[] = {'x', 'x', 'x', '!'};
    TextWriter tiny(guard, 3);
    tiny.Append("abc").Unsigned(1);
    assert(!tiny.Complete() && std::string(guard) == "ab" && guard[3] == '!');
    char one = 'x';
    TextWriter empty(&one, 1);
    empty.Append("x");
    assert(one == 0 && !empty.Complete());
    TextWriter none(nullptr, 0);
    none.Signed(1);
    assert(!none.Complete());
    TextWriter invalid(buffer, sizeof(buffer));
    invalid.Unsigned(1, 1);
    assert(!invalid.Complete());
    TextWriter binary(buffer, sizeof(buffer));
    binary.Unsigned(std::numeric_limits<std::uint32_t>::max(), 2);
    assert(binary.Complete() && binary.Size() == 32);
}

void TestUtf() {
    const std::string text = "ASCII 日本語 😀";
    char16_t utf16[64];
    char utf8[128];
    auto result = ToUtf16(text, utf16, 64);
    assert(result.valid && !result.truncated && result.written == 12);
    auto back = ToUtf8(std::u16string_view(utf16, result.written), utf8, 128);
    assert(back.valid && !back.truncated && std::string(utf8) == text);
    // Unicode の全スカラー値を往復し、境界・サロゲートの組み合わせを確かめる。
    for (std::uint32_t code = 0; code <= 1114111; ++code) {
        if (code >= 0xD800 && code <= 0xDFFF)
            continue;
        char16_t units[2] = {static_cast<char16_t>(code), 0};
        unsigned count = 1;
        if (code >= 65536) {
            units[0] = static_cast<char16_t>(0xD800 + ((code - 65536) >> 10));
            units[1] = static_cast<char16_t>(0xDC00 + ((code - 65536) & 0x3FF));
            count = 2;
        }
        back = ToUtf8(std::u16string_view(units, count), utf8, 128);
        result = ToUtf16(std::string_view(utf8, back.written), utf16, 64);
        assert(back.valid && result.valid && result.written == count);
        assert(std::equal(units, units + count, utf16));
    }
    for (const auto &bad :
         {std::string("\xC0\xAF"), std::string("\xED\xA0\x80"), std::string("\xF4\x90\x80\x80"),
          std::string("\xE3\x81"), std::string("\x80"),
          std::string("\xE3"
                      "A")}) {
        result = ToUtf16(bad, utf16, 64);
        assert(!result.valid && !result.truncated && utf16[0] == 0xFFFD);
    }
    assert(utf16[1] == u'A');
    const char16_t bad16[] = {0xD800, u'A', 0xDC00};
    result = ToUtf8(std::u16string_view(bad16, 3), utf8, 128);
    assert(!result.valid && std::string(utf8) == "\xEF\xBF\xBD"
                                                 "A"
                                                 "\xEF\xBF\xBD");
    result = ToUtf16("😀", utf16, 2);
    assert(result.truncated && result.written == 0 && utf16[0] == 0);
    result = ToUtf8(u"😀", utf8, 4);
    assert(result.truncated && result.written == 0 && utf8[0] == 0);
    assert(ToUtf8(u"", nullptr, 0).written == 0);
    assert(ToUtf16("x", nullptr, 0).truncated);
    assert(!ToUtf16("", utf16, 1).truncated && utf16[0] == 0);
}

struct Packet {
    unsigned value;
    unsigned inverse;
    unsigned copy[16];
};
Packet MakePacket(unsigned value) {
    Packet packet{value, ~value, {}};
    for (auto &copy : packet.copy)
        copy = value;
    return packet;
}
void CheckPacket(const Packet &packet) {
    assert(packet.inverse == ~packet.value);
    for (auto copy : packet.copy)
        assert(copy == packet.value);
}

void TestMailbox() {
    Mailbox<Packet> mailbox;
    Packet read{};
    std::uint32_t generation = 0;
    assert(mailbox.ReadIfNew(read, generation) == MailRead::Unchanged);
    assert(mailbox.TryPublish(MakePacket(1)));
    assert(mailbox.ReadIfNew(read, generation) == MailRead::Updated && generation == 1);
    CheckPacket(read);
    assert(mailbox.ReadIfNew(read, generation) == MailRead::Unchanged);
    assert(mailbox.TryPublish(MakePacket(2)) && mailbox.TryPublish(MakePacket(3)));
    assert(mailbox.ReadIfNew(read, generation) == MailRead::Updated && read.value == 3 &&
           generation == 3);
    constexpr unsigned final = 100000;
    std::thread writer([&] {
        for (unsigned i = 4; i <= final; ++i)
            while (!mailbox.TryPublish(MakePacket(i)))
                std::this_thread::yield();
    });
    unsigned last = 3;
    while (last != final) {
        if (mailbox.ReadIfNew(read, generation) == MailRead::Updated) {
            CheckPacket(read);
            assert(read.value > last);
            last = read.value;
        } else {
            std::this_thread::yield();
        }
    }
    writer.join();
}

void TestRequest() {
    Request<Packet> request;
    Packet packet{};
    assert(!request.TryTake(packet));
    assert(request.TrySubmit(MakePacket(1)));
    assert(!request.TrySubmit(MakePacket(2)));
    assert(request.TryTake(packet) && packet.value == 1);
    assert(!request.TryTake(packet));
    std::thread producer([&] {
        for (unsigned i = 1; i <= 100000; ++i)
            while (!request.TrySubmit(MakePacket(i)))
                std::this_thread::yield();
    });
    for (unsigned i = 1; i <= 100000; ++i) {
        while (!request.TryTake(packet))
            std::this_thread::yield();
        CheckPacket(packet);
        assert(packet.value == i);
    }
    producer.join();
    assert(!request.TryTake(packet));
}

std::vector<unsigned> calls;
template <unsigned I> void Step() { calls.push_back(I); }
template <std::size_t... I> auto Steps(std::index_sequence<I...>) {
    return std::array<frame::Step, sizeof...(I)>{Step<I>...};
}
frame::Dispatcher *current;
std::atomic<bool> entered{false};
std::atomic<bool> releaseStep{false};
void BlockingStep() {
    entered.store(true, std::memory_order_release);
    while (!releaseStep.load(std::memory_order_acquire))
        std::this_thread::yield();
}
void ChangeDuringFrame() {
    calls.push_back(99);
    current->Remove(Step<1>);
    current->Add(Step<2>);
    current->Dispatch(); // 再入はそのフレームの実行を重ねない。
}

void TestFrame() {
    frame::Dispatcher dispatcher;
    assert(dispatcher.Add(nullptr).result == frame::Result::Invalid);
    assert(dispatcher.Remove(nullptr).result == frame::Result::Invalid);
    auto change = dispatcher.Add(Step<1>, 1);
    assert(!dispatcher.IsApplied(change.revision));
    assert(dispatcher.Add(Step<0>, 0).result == frame::Result::Queued);
    assert(dispatcher.Add(Step<3>, 1).result == frame::Result::Queued);
    assert(dispatcher.Add(Step<1>, -1).result == frame::Result::Unchanged);
    dispatcher.Dispatch();
    assert(dispatcher.IsApplied(change.revision));
    assert((calls == std::vector<unsigned>{0, 1, 3}));
    change = dispatcher.Remove(Step<1>);
    assert(!dispatcher.IsApplied(change.revision));
    calls.clear();
    dispatcher.Dispatch();
    assert(dispatcher.IsApplied(change.revision) && (calls == std::vector<unsigned>{0, 3}));
    assert(dispatcher.Remove(Step<1>).result == frame::Result::Unchanged);
    dispatcher.Reset();
    constexpr auto count = limits::FrameSteps;
    auto steps = Steps(std::make_index_sequence<count + 1>{});
    for (std::size_t i = 0; i < count; ++i)
        assert(dispatcher.Add(steps[i]).result == frame::Result::Queued);
    assert(dispatcher.Add(steps[count]).result == frame::Result::Full);
    assert(dispatcher.Remove(steps[0]).result == frame::Result::Queued);
    assert(dispatcher.Add(steps[count]).result == frame::Result::Queued);
    dispatcher.Reset();
    calls.clear();
    current = &dispatcher;
    dispatcher.Add(ChangeDuringFrame, -1);
    dispatcher.Add(Step<1>);
    dispatcher.Dispatch();
    assert((calls == std::vector<unsigned>{99, 1}));
    calls.clear();
    dispatcher.Dispatch();
    assert((calls == std::vector<unsigned>{99, 2}));
    dispatcher.Reset();
    dispatcher.Add(BlockingStep);
    std::thread running([&] { dispatcher.Dispatch(); });
    while (!entered.load(std::memory_order_acquire))
        std::this_thread::yield();
    change = dispatcher.Remove(BlockingStep);
    assert(change.result == frame::Result::Queued && !dispatcher.IsApplied(change.revision));
    dispatcher.Dispatch();
    assert(!dispatcher.IsApplied(change.revision));
    releaseStep.store(true, std::memory_order_release);
    running.join();
    dispatcher.Dispatch();
    assert(dispatcher.IsApplied(change.revision));
}

bool WireOk() { return true; }
bool WireFail() { return false; }
void TestFeature() {
    FeatureRegistry registry;
    assert(registry.Add({"other", WireOk, Step<4>, ShutdownStage::Other}));
    assert(registry.Add({"draw", WireOk, Step<3>, ShutdownStage::DrawHooks}));
    assert(registry.Add({"game1", WireOk, Step<1>, ShutdownStage::GameHooks}));
    assert(registry.Add({"game2", WireOk, Step<0>, ShutdownStage::GameHooks}));
    assert(registry.Add({"failed", WireFail, Step<99>, ShutdownStage::Other}));
    assert(!registry.Add({"other", WireOk, Step<99>, ShutdownStage::Other}));
    assert(!registry.WireAll());
    assert(!registry.Add({"late", WireOk, Step<99>, ShutdownStage::Other}));
    calls.clear();
    registry.ShutdownAll();
    assert((calls == std::vector<unsigned>{0, 1, 3, 4}));
    registry.ShutdownAll();
    assert(calls.size() == 4 && !registry.WireAll());
}
} // namespace

int main() {
    TestFormat();
    TestUtf();
    TestMailbox();
    TestRequest();
    TestFrame();
    TestFeature();
}
