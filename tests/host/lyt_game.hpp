#pragma once
// ゲームの描画 API の代役（PC 試験用）。ゲームの命令やデータは含まない。
// 管理者・アロケータ・コマンドリストの動きだけを、数え上げと整合の検査つきで真似る。
#include "game/lyt/resource_api.hpp"
#include "game/lyt/scene.hpp"
#include <cassert>
#include <cstdlib>
#include <new>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace fakegame {
using namespace gohan::game;
using namespace gohan::game::lyt;

struct Game {
    static inline generated::SsysMaAllocator allocator{};
    static inline generated::SsysMaLayoutMgr manager{};
    static inline bool haveAllocator = true, haveManager = true, textReady = true;
    static inline bool allocateFails = false, flushFails = false;
    static inline unsigned freeBytes = 4000000, largestBlock = 3000000, extraBufferBytes = 0;
    static inline unsigned bound = 7, next = 20;
    static inline int allocations = 0, frees = 0, attempts = 0, failCreate = 0, storages = 0;
    static inline int batchInits = 0, addCalls = 0;
    static inline int allocateCalls = 0, flushCalls = 0, spaceCalls = 0;
    static inline int failAllocate = 0, failFlush = 0, failSpace = 0;
    static inline bool commandsInLayoutHeap = false;
    static inline unsigned allocationOverhead = 0;
    static inline std::map<void *, unsigned> blocks;
    static inline void *resident = nullptr;
    static inline std::set<unsigned> lists;
    static inline std::string releases;

    static void Reset() {
        assert(lists.empty() && blocks.empty() && allocations == frees);
        haveAllocator = haveManager = textReady = true;
        allocateFails = flushFails = false;
        freeBytes = 4000000;
        largestBlock = 3000000;
        extraBufferBytes = 0;
        bound = 7;
        next = 20;
        allocations = frees = attempts = failCreate = storages = batchInits = addCalls = 0;
        allocateCalls = flushCalls = spaceCalls = failAllocate = failFlush = failSpace = 0;
        releases.clear();
        commandsInLayoutHeap = false;
        allocationOverhead = 0;
        resident = nullptr;
        manager = {};
    }
    static bool Balanced() { return lists.empty() && blocks.empty() && allocations == frees; }

    static generated::SsysMaAllocator *Allocator() { return haveAllocator ? &allocator : nullptr; }
    static void *Allocate(generated::SsysMaAllocator *owner, unsigned bytes, int alignment) {
        assert(owner == &allocator && alignment == 128);
        if (++allocateCalls == failAllocate || allocateFails)
            return nullptr;
        void *data = ::operator new(bytes, std::align_val_t{128});
        assert(data);
        std::memset(data, 0xA5, bytes);
        blocks[data] = bytes;
        if (!resident) resident = data;
        ++allocations;
        return data;
    }
    static void Free(generated::SsysMaAllocator *owner, void *data) {
        assert(owner == &allocator && blocks.contains(data));
        if (data == resident) {
            assert(lists.empty());
            resident = nullptr;
        } else {
            assert(!manager.top.head && !manager.bottom.head);
        }
        blocks.erase(data);
        ::operator delete(data, std::align_val_t{128});
        ++frees;
        releases += 'A';
    }
    static void Property(unsigned property, unsigned *value) {
        assert(property == 519);
        *value = bound;
    }
    static void Create(int count, unsigned *id) {
        assert(count == 1 && *id == 0);
        if (++attempts == failCreate)
            return;
        *id = next++;
        assert(lists.insert(*id).second);
    }
    static void Select(unsigned id) {
        assert(id == 7 || lists.contains(id));
        bound = id;
    }
    static void Storage(unsigned bytes, int queue) {
        assert(lists.contains(bound));
        assert(bytes == gohan::core::limits::RendererRecordBytes && queue == gohan::core::limits::RendererQueueEntries);
        ++storages;
    }
    static void Destroy(int count, const unsigned *id) {
        assert(count == 1 && bound != *id && lists.erase(*id) == 1);
        releases += 'L';
    }
    static bool Flush(void *data, std::size_t size) {
        // 動く帯は確保全体ではなく、使用していない画像の範囲だけを排出する。
        bool contained = false;
        const auto at = reinterpret_cast<std::uintptr_t>(data);
        for (const auto &[base, bytes] : blocks) {
            const auto begin = reinterpret_cast<std::uintptr_t>(base);
            if (at >= begin && at - begin <= bytes && size <= bytes - (at - begin)) contained = true;
        }
        assert(contained && size > 0);
        return ++flushCalls != failFlush && !flushFails;
    }
    static std::uint32_t Physical(const void *data) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(data) & 0x0FFFFFFFu);
    }
    static bool Space(generated::SsysMaAllocator *owner, int alignment, detail::HeapSpace *out) {
        assert(owner == &allocator && alignment == 128);
        if (++spaceCalls == failSpace) return false;
        unsigned used = 0;
        for (const auto &[data, bytes] : blocks) { (void)data; used += bytes + allocationOverhead; }
        if (commandsInLayoutHeap)
            used += lists.size() * (68 + gohan::core::limits::RendererRecordBytes + 28 * gohan::core::limits::RendererQueueEntries);
        out->freeBytes = freeBytes > used ? freeBytes - used : 0;
        out->largestBlock = largestBlock > used ? largestBlock - used : 0;
        return true;
    }
    static generated::SsysMaLayoutMgr *Manager() { return haveManager ? &manager : nullptr; }
    static bool TextReady() { return textReady; }
    static unsigned BufferBytes(unsigned chars) { return detail::BatchBytes(chars) + extraBufferBytes; }

    // ゲームの文字バッチの初期化と同じ配置を作る。
    static generated::NwFontTextBatchPrefix *BatchInit(generated::NwFontTextBatchPrefix *batch, unsigned chars) {
        ++batchInits;
        auto *base = reinterpret_cast<unsigned char *>(batch);
        std::memset(batch, 0, sizeof(*batch));
        batch->max_chars = chars;
        batch->draw_glyph_limit = 0xFFFF;
        batch->glyph_bits = base + sizeof(generated::NwFontTextBatchPrefix) +
                            sizeof(generated::NwFontTextBatchGlyph) * chars;
        const auto bits = reinterpret_cast<std::uintptr_t>(batch->glyph_bits) - reinterpret_cast<std::uintptr_t>(base) +
                          ((chars + 7) / 8 + 3) / 4 * 4;
        auto *commands = base + (bits + 15) / 16 * 16;
        batch->commands = batch->command_buffer = reinterpret_cast<unsigned *>(commands);
        batch->command_word_capacity = detail::BatchCommandWords(chars);
        // 末尾が割り当てた大きさ（ゲームの必要量）を超えないこと。
        assert(commands + 4 * batch->command_word_capacity <= base + BufferBytes(chars));
        return batch;
    }
    static void Add(generated::SsysMaLayoutMgr *owner, generated::SsysMaLayoutNode *node, unsigned screen) {
        assert(owner == &manager && screen < 2);
        ++addCalls;
        auto &list = screen ? owner->bottom : owner->top;
        assert(node->prev == nullptr || list.head != nullptr);
        if (!list.head) {
            node->prev = node->next = nullptr;
            list.head = list.tail = node;
            return;
        }
        // 二重登録はゲームの側でも壊れる。ここで検出する。
        assert(list.tail != node && !node->prev && !node->next);
        node->prev = list.tail;
        node->next = nullptr;
        list.tail->next = node;
        list.tail = node;
    }
    // 描画が済んだあとのゲームの動作: 上下のリストの全ノードを外す。
    static void EndTop() { Unlink(manager.top); }
    static void EndBottom() { Unlink(manager.bottom); }
    static void EndPasses() {
        EndTop();
        EndBottom();
    }
    static void Unlink(generated::SsysMaLayoutList &list) {
        for (auto *node = list.head; node;) {
            auto *following = node->next;
            node->prev = node->next = nullptr;
            node = following;
        }
        list.head = list.tail = nullptr;
    }
};

inline const detail::ResourceApi &Api() {
    static const detail::ResourceApi api{Game::Allocator, Game::Allocate, Game::Free, Game::Property,
                                         Game::Create,    Game::Select,   Game::Storage, Game::Destroy,
                                         Game::Flush,     Game::Physical, Game::Space,   Game::Manager,
                                         Game::Add,       Game::BatchInit, Game::BufferBytes, Game::TextReady};
    return api;
}
} // namespace fakegame
