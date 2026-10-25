// SPDX-License-Identifier: GPL-3.0-or-later
// FMOD Ex 4.44.50 in the 1.09 eboot keeps every sound it has made in the master sound group's ring:
// SoundI +0x1d0 is a node (next, prev, the sound), the group's head SoundGroupI +0x30 (the master
// group SystemI +0x9668). SoundI::release (patch 0x11be120) takes a sound out before freeing it,
// with one exception: a stream of several subsounds shares one subsound object (+0xb0 names
// itself), and the stream's release asks it to release itself, ignores the answer and frees it
// (patch 0x11be682). The object refuses, and so stays linked, while its flags say 0x200 (an event's
// programmer sound in use), 0x100 (preloaded) or 0x20 (a release under way): freed while linked, the
// next sound linked beside it writes through freed memory. That was a crash arriving at Rom's lake
// from the Fishing Hamlet.
//
// The guard (fmod_ring.cpp): FMOD's MemPool free first takes a sound still linked out of its ring,
// under FMOD's critical section; at the one free of a refusing shared subsound it keeps the object
// instead, as a sound of its own nothing plays again, for whoever still holds it. An event's
// programmer sound kept so is never freed: a few hundred bytes each time.
//
// After droogie (bbhost src/engine/fmod_ring.h and fmod_ring.cpp, f1d4fa3): the guard, not its
// detector. The helpers take `read(address, T* out)` (false where memory cannot be read) and
// `write(address, T value)`, so tests run them on a synthetic ring.
#pragma once
#include <cstddef>
#include <cstdint>

#include "engine_hooks.h"

namespace BbEngine::FmodRing {

// SoundI.
constexpr std::uint64_t kNode = 0x1d0;         // the group ring's node: next, prev, the sound
constexpr std::uint64_t kFlags = 0x50;         // 0x20 being released, 0x100 preloaded, 0x200 programmer sound in use
constexpr std::uint64_t kShared = 0xb0;        // the subsound object a stream of several subsounds shares
constexpr std::uint64_t kParent = 0xc0;
constexpr std::uint64_t kOpenState = 0x1c0;    // FMOD_OPENSTATE
constexpr std::uint64_t kStreamSample = 0x238; // a stream's sample buffer, which its subsound object shares
constexpr std::uint32_t kOpenStateError = 2;   // FMOD_OPENSTATE_ERROR
constexpr std::uint32_t kRefusing = 0x20 | 0x100 | 0x200;

// In a ring: the node names the sound and both neighbours point back at it.
template <typename Read>
bool Linked(std::uint64_t obj, Read read) {
    const std::uint64_t node = obj + kNode;
    std::uint64_t next = 0, prev = 0, data = 0, back = 0, forward = 0;
    if (!read(node, &next) || !read(node + 8, &prev) || !read(node + 16, &data)) return false;
    if (data != obj || next == node || prev == node) return false;
    return read(next + 8, &back) && back == node && read(prev, &forward) && forward == node;
}

// Takes a linked sound out of its ring and leaves the node as SoundI::release does (linked to
// itself, no data). False, with nothing written, when it is not linked.
template <typename Read, typename Write>
bool Unlink(std::uint64_t obj, Read read, Write write) {
    if (!Linked(obj, read)) return false;
    const std::uint64_t node = obj + kNode;
    std::uint64_t next = 0, prev = 0;
    read(node, &next);
    read(node + 8, &prev);
    write(prev, next);
    write(next + 8, prev);
    write(node, node);
    write(node + 8, node);
    write(node + 16, std::uint64_t{0});
    return true;
}

// A stream's shared subsound (its +0xb0 names itself) that refused its own release.
template <typename Read>
bool RefusedShared(std::uint64_t obj, Read read) {
    std::uint64_t shared = 0;
    std::uint32_t flags = 0;
    return read(obj + kShared, &shared) && shared == obj && read(obj + kFlags, &flags) && (flags & kRefusing) != 0;
}

// What the guard makes of such a subsound instead of letting its stream free it: a sound of its own
// (no parent, no shared block, no stream sample buffer, which the stream's release freed), in the
// error state, so nothing plays it again: an event's start and the system's channel allocation
// would read through its cleared codec, while getOpenState reports the error with every pointer it
// follows checked.
template <typename Write>
void Orphan(std::uint64_t obj, Write write) {
    write(obj + kParent, std::uint64_t{0});
    write(obj + kShared, std::uint64_t{0});
    write(obj + kStreamSample, std::uint64_t{0});
    write(obj + kOpenState, kOpenStateError);
}

// The guard's hook: FMOD's MemPool free (patch 0x11ab3a0; pool in rdi, the block in rsi).
constexpr std::size_t kPoolFree = 0xdab3a0; // image offset
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; push rax
constexpr std::uint8_t kPoolFreePrologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41,
                                              0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50};
__attribute__((sysv_abi)) std::int64_t PoolFree(std::uint64_t, const std::uint64_t* saved);
/// The other code the guard relies on (the shared subsound's free, FMOD's critical section) is
/// still 1.09's; keeps the image for PoolFree.
bool Check(std::uint8_t* image, std::size_t size);

} // namespace BbEngine::FmodRing
