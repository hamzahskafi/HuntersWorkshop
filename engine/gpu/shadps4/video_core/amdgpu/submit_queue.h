// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 20 (docs/chain-waits.md, item 2): a queue's submissions, handed from the game's
// submitting threads to the GPU command thread without a lock on the GPU thread's side. The game's
// threads push one at a time (they hold the queue's lock among themselves, which keeps numbering,
// the draw preparation's queue and the push in one order); the GPU command thread alone reads the
// front and pops it. Blocks of 64 entries linked as they fill, never moved; an emptied block goes
// back to the pushing side for its next one, so in steady play nothing is allocated. Before this,
// the GPU command thread took the queue's lock twice a submission (at every resume of its decode
// task and at its pop), held by the game's submit (a lower role): about 5% of its time.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace AmdGpu {

template <typename T, std::size_t kBlock = 64>
class SubmitQueue {
public:
    SubmitQueue() : read_block{new Block}, write_block{read_block} {}
    ~SubmitQueue() {
        for (Block* b = read_block; b;) {
            Block* next = b->next.load(std::memory_order_relaxed);
            delete b;
            b = next;
        }
        delete spare.load(std::memory_order_relaxed);
    }
    SubmitQueue(const SubmitQueue&) = delete;
    SubmitQueue& operator=(const SubmitQueue&) = delete;

    /// The pushing side: one thread at a time (the caller's lock orders them).
    void Push(const T& value) {
        if (write_at == kBlock) {
            Block* b = spare.exchange(nullptr, std::memory_order_acquire);
            if (!b) {
                b = new Block;
            }
            b->next.store(nullptr, std::memory_order_relaxed);
            write_block->next.store(b, std::memory_order_relaxed); // published with `pushed`
            write_block = b;
            write_at = 0;
        }
        write_block->slots[write_at++] = value;
        pushed.store(pushed.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    /// The GPU command thread: the oldest entry, left in place. False when empty.
    bool Front(T& out) {
        if (popped.load(std::memory_order_relaxed) == pushed.load(std::memory_order_acquire)) {
            return false;
        }
        if (read_at == kBlock) {
            Block* next = read_block->next.load(std::memory_order_relaxed);
            Block* old = read_block;
            read_block = next;
            read_at = 0;
            delete spare.exchange(old, std::memory_order_release); // at most one kept
        }
        out = read_block->slots[read_at];
        return true;
    }
    /// The GPU command thread, after Front returned true.
    void Pop() {
        ++read_at;
        popped.store(popped.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    /// Entries queued (a snapshot, from any thread).
    [[nodiscard]] std::size_t Size() const {
        const std::uint64_t out = popped.load(std::memory_order_acquire);
        return static_cast<std::size_t>(pushed.load(std::memory_order_acquire) - out);
    }

private:
    struct Block {
        T slots[kBlock]{};
        std::atomic<Block*> next{nullptr};
    };
    // The GPU command thread's side.
    alignas(64) Block* read_block;
    std::size_t read_at = 0;
    std::atomic<std::uint64_t> popped{0};
    // The pushing side.
    alignas(64) Block* write_block;
    std::size_t write_at = 0;
    std::atomic<std::uint64_t> pushed{0};
    alignas(64) std::atomic<Block*> spare{nullptr};
};

} // namespace AmdGpu
