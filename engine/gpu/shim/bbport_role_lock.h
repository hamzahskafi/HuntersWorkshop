// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 20 (docs/chain-waits.md, item 3): a lock that carries priority. Beta 18's roles set each
// thread's priority by what it is for, but a thread also waits on whoever holds a lock it needs, and
// Windows' slim locks carry no priority to their holder: the render chain (above normal) waited on
// locks the draw preparation's threads (idle) held, about 12% of stage A in the tester's profile of
// engine id 135. This lock notes its holder when taken (one plain store); only a thread that finds
// it held looks: a holder of a lower role than the waiter's is raised to the waiter's priority (high
// QoS, any core: the roles' raise) for the rest of its hold, and goes back to its role when it lets
// go. Every such wait is counted per lock and printed with the `Roles:` line ("waited on lower
// roles"), so a tester's log shows one on any CPU. BB_LOCK_INHERIT=0 (this beta only): count, don't
// raise. Use it for every lock the chain shares with a lower role.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

namespace BbRoles {

namespace LockDetail {
/// The calling thread's role record (null: not named yet) and its raised-by-a-lock flag, set when it
/// is named (bbport_roles.cpp).
extern thread_local constinit void* self;
extern thread_local constinit std::atomic<bool>* raised_flag;
/// The role locks the calling thread holds exclusively: a lock's raise ends when the last is let go
/// (a holder raised for lock B keeps it while it lets go of lock A first).
extern thread_local constinit unsigned depth;
struct Stats;
Stats* Register(const char* name);
/// The slow path of a contended acquire: counts, raises a lower-role holder; returns the raise's
/// generation (0: none) for Acquired.
std::uint64_t Contended(Stats* stats, void* holder, std::uint64_t& start);
void Acquired(Stats* stats, void* holder, std::uint64_t generation, std::uint64_t start);
/// The calling thread was raised by a lock it held: back to its role.
void RestoreSelf();
inline void* Locked() {
    ++depth;
    return self;
}
inline void AfterUnlock() {
    if (--depth == 0) {
        if (auto* flag = raised_flag; flag && flag->load(std::memory_order_relaxed)) [[unlikely]] {
            RestoreSelf();
        }
    }
}
} // namespace LockDetail

/// std::mutex's interface (lock, try_lock, unlock), so std::scoped_lock and std::unique_lock work.
class Mutex {
public:
    explicit Mutex(const char* name) : stats{LockDetail::Register(name)} {}
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() {
        if (!m.try_lock()) [[unlikely]] {
            Slow();
        }
        holder.store(LockDetail::Locked(), std::memory_order_release); // its record, to a waiter
    }
    bool try_lock() {
        if (!m.try_lock()) {
            return false;
        }
        holder.store(LockDetail::Locked(), std::memory_order_release); // its record, to a waiter
        return true;
    }
    void unlock() {
        holder.store(nullptr, std::memory_order_relaxed);
        m.unlock();
        LockDetail::AfterUnlock();
    }
    /// For std::condition_variable_any (its wait unlocks and relocks through these).
    std::mutex& native() {
        return m;
    }

private:
    void Slow() {
        void* const h = holder.load(std::memory_order_acquire);
        std::uint64_t start = 0;
        const std::uint64_t generation = LockDetail::Contended(stats, h, start);
        m.lock();
        LockDetail::Acquired(stats, h, generation, start);
    }
    std::mutex m;
    std::atomic<void*> holder{nullptr};
    LockDetail::Stats* stats;
};

/// std::shared_mutex's interface. The exclusive holder is the one noted: a waiter, shared or
/// exclusive, behind a writer of a lower role raises it.
class SharedMutex {
public:
    explicit SharedMutex(const char* name) : stats{LockDetail::Register(name)} {}
    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;

    void lock() {
        if (!m.try_lock()) [[unlikely]] {
            void* const h = holder.load(std::memory_order_acquire);
            std::uint64_t start = 0;
            const std::uint64_t generation = LockDetail::Contended(stats, h, start);
            m.lock();
            LockDetail::Acquired(stats, h, generation, start);
        }
        holder.store(LockDetail::Locked(), std::memory_order_release); // its record, to a waiter
    }
    bool try_lock() {
        if (!m.try_lock()) {
            return false;
        }
        holder.store(LockDetail::Locked(), std::memory_order_release); // its record, to a waiter
        return true;
    }
    void unlock() {
        holder.store(nullptr, std::memory_order_relaxed);
        m.unlock();
        LockDetail::AfterUnlock();
    }
    void lock_shared() {
        if (!m.try_lock_shared()) [[unlikely]] {
            void* const h = holder.load(std::memory_order_acquire);
            std::uint64_t start = 0;
            const std::uint64_t generation = LockDetail::Contended(stats, h, start);
            m.lock_shared();
            LockDetail::Acquired(stats, h, generation, start);
        }
    }
    bool try_lock_shared() {
        return m.try_lock_shared();
    }
    void unlock_shared() {
        m.unlock_shared();
    }

private:
    std::shared_mutex m;
    std::atomic<void*> holder{nullptr};
    LockDetail::Stats* stats;
};

/// For the `Roles:` line: the locks a thread waited on behind a lower role, this window.
void ReportLockInversions(double seconds, char* out, std::size_t size);

} // namespace BbRoles
