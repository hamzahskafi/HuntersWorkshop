// SPDX-License-Identifier: GPL-3.0-or-later
// Two guards in the game's code against crashes found by bbport 0.7 (deadinside28): Havok's hit collector,
// which a degenerate query fills without end (from SrShadowy's fork), and GX occlusion queries left without a
// handle (bbport #336, whp199's #343). Image offsets; Binary Ninja's are 0x400000 higher. game_fixes.cpp puts
// them in (hit_cap, gx_query); the logic is here for crash-guards-test.
#pragma once
#include <cstddef>
#include <cstdint>

namespace BbEngine::CrashGuards {
#define CRASH_GUARDS_ABI __attribute__((sysv_abi))

// ---- Havok's hit collector ----------------------------------------------------------------------------
// The 96-byte hit array's push (0xb84ce0, a virtual: rdi the collector, rsi the hit; r14 the collector from
// its prologue on): its data at +0x20, its count at +0x28, its capacity in the low 30 bits of +0x2c; a full
// array is grown (kHitGrow: lea rsi, [r14 + 0x20]; lea rdi, [the allocator]; mov edx, 0x60; then the grow's
// call), then the hit copied in. A degenerate query (a zero-length ray while the world is paused) filled one
// without end until an allocation failed (SIGSEGV at 0xf00010, SrShadowy's fork). The growth is a cave's: an
// array full at kHitCap hits or more is not grown, and the push returns (kHitReturn: its epilogue) with the hit
// dropped and counted (HitDropped). The push returns nothing, so a hit dropped is all that changes, and only a
// push that would grow the array passes through the cave. (bbport writes the hit into a scratch array instead.)
constexpr std::size_t kHitPush = 0xb84ce0, kHitGrow = 0xb84cfe, kHitGrowCall = 0xb84d0e, kHitReturn = 0xb84d69;
constexpr std::size_t kHitAllocator = 0x54018b0; // the lea's rip-relative target
// The push whole, checked before the cave goes in (the cave jumps to its grow's call and to its epilogue).
constexpr std::uint8_t kHitPushCode[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x89, 0xf3, 0x49, 0x89, 0xfe, 0x41, 0x8b, 0x46,
    0x28, 0xb9, 0xff, 0xff, 0xff, 0x3f, 0x41, 0x23, 0x4e, 0x2c, 0x39, 0xc8, 0x75, 0x19, 0x49, 0x8d,
    0x76, 0x20, 0x48, 0x8d, 0x3d, 0xa7, 0xcb, 0x87, 0x04, 0xba, 0x60, 0x00, 0x00, 0x00, 0xe8, 0xed,
    0x43, 0x89, 0xff, 0x41, 0x8b, 0x46, 0x28, 0x49, 0x8b, 0x4e, 0x20, 0x48, 0x98, 0x48, 0x8d, 0x04,
    0x40, 0x48, 0xc1, 0xe0, 0x05, 0xc5, 0xf8, 0x28, 0x03, 0xc5, 0xf8, 0x28, 0x4b, 0x10, 0xc5, 0xf8,
    0x28, 0x53, 0x20, 0xc5, 0xf8, 0x28, 0x5b, 0x30, 0xc5, 0xf8, 0x29, 0x4c, 0x01, 0x10, 0xc5, 0xf8,
    0x29, 0x04, 0x01, 0xc5, 0xf8, 0x29, 0x5c, 0x01, 0x30, 0xc5, 0xf8, 0x29, 0x54, 0x01, 0x20, 0xc5,
    0xf8, 0x28, 0x43, 0x50, 0xc5, 0xf8, 0x29, 0x44, 0x01, 0x50, 0xc5, 0xf8, 0x28, 0x43, 0x40, 0xc5,
    0xf8, 0x29, 0x44, 0x01, 0x40, 0x41, 0xff, 0x46, 0x28, 0x5b, 0x41, 0x5e, 0x5d, 0xc3};
constexpr std::size_t kHitGrowBytes = kHitGrowCall - kHitGrow; // the three instructions the cave's jump takes
constexpr std::int32_t kHitCap = 65536;
constexpr std::uint64_t kHitData = 0x20, kHitCount = 0x28;
// cmp dword [r14 + 0x28], kHitCap; jae full; lea rsi, [r14 + 0x20]; movabs rdi, allocator; mov edx, 0x60;
// jmp [grow's call]; full: mov rdi, r14; movabs rax, HitDropped; call rax; jmp [the epilogue]
constexpr std::size_t kHitCaveSize = 8 + 2 + 4 + 10 + 5 + 14 + 3 + 10 + 2 + 14;
/// The cave for the game's image at `image`.
void HitCave(std::uint64_t image, std::uint8_t (&cave)[kHitCaveSize]);
/// The cave's call for a dropped hit: counted, and reported now and then.
CRASH_GUARDS_ABI void HitDropped(std::uint64_t collector);

// ---- GX occlusion queries -----------------------------------------------------------------------------
// A query (0x30 bytes; its handle at +0, its state at +8: 0 new, 2 begun, 1 ended; its result at +0x10) gets
// its handle from its creation (0xe2f080, query and kind 1, from the pool's 0x2288d00), which asks the GX
// device (0x21674f0) for a 0x18-byte handle (its allocator at +0xdc10) and the query's 0x80 bytes of results
// (+0xdc18; 0x26a98a0); out of room it returns false and leaves the handle null. There is no pool of queries
// to size: both allocators are the game's lock-and-TLSF graphics allocators (0x1fc5ed0, 0x1fc5f30), which every
// GX object shares, and the query's release (0xe2f020) gives both back. Their heaps are what runs out, and the
// start program already gives them more room when a scene outgrows the PS4's (start/patches.cpp: above 1080p,
// highest model detail, screen space reflections). The guard stays for what still runs them out. The render thread's begin (0xe2f130: state 0 -> 2, then the device's begin 0x2169ec0(device, device +
// 0xb730, handle)) and end (0xe2f160: state 1, the device's end 0x216a070) read the handle: a null one faulted
// at 0x2169edc on GXRenderThread late in a long session (bbport #336). Here the creation is watched (a failed
// one's query kept, kFailedSlots of them), and begin and end are ours in place of the game's two short
// functions, as they are with a handle. Without one, a query whose creation failed is created again first (as
// the pool does); one still without a handle is not begun and its end is skipped, so its state stays 0 and its
// result getter (0xe2f190) gives the stored 0: nothing visible for that query. A null handle the creation did
// not leave (a query released while the render thread holds it) is only skipped: creating one would write into
// a freed object.
constexpr std::size_t kQueryCreate = 0xe2f080, kQueryBegin = 0xe2f130, kQueryEnd = 0xe2f160;
constexpr std::size_t kDeviceBegin = 0x2169ec0, kDeviceEnd = 0x216a070;
constexpr std::uint64_t kDeviceQueries = 0xb730; // the device's query state, the device functions' second argument
// push rbp; mov rbp, rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp, 0x18
constexpr std::uint8_t kQueryCreateEntry[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                              0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};
// The game's begin and end whole (their calls of the device's begin and end included).
constexpr std::uint8_t kQueryBeginCode[] = {
    0x55, 0x48, 0x89, 0xe5, 0x83, 0x7f, 0x08, 0x00, 0x75, 0x1d, 0xc7, 0x47, 0x08, 0x02, 0x00, 0x00, 0x00,
    0x48, 0x8b, 0x46, 0x08, 0x48, 0x8b, 0x17, 0x48, 0x8d, 0xb0, 0x30, 0xb7, 0x00, 0x00, 0x48, 0x89, 0xc7,
    0xe8, 0x69, 0xad, 0x33, 0x01, 0xb0, 0x01, 0x5d, 0xc3};
constexpr std::uint8_t kQueryEndCode[] = {
    0x55, 0x48, 0x89, 0xe5, 0xc7, 0x47, 0x08, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x46, 0x08, 0x48, 0x8b,
    0x17, 0x48, 0x8d, 0xb0, 0x30, 0xb7, 0x00, 0x00, 0x48, 0x89, 0xc7, 0xe8, 0xef, 0xae, 0x33, 0x01, 0xb0,
    0x01, 0x5d, 0xc3};
constexpr std::size_t kFailedSlots = 256;

/// The game's functions the guard calls: the creation itself (its hook's original), the device's begin and end.
void SetQueryGame(std::uint64_t create, std::uint64_t device_begin, std::uint64_t device_end);
/// Prepare's callback at kQueryCreate: saved[5] the query, saved[4] the kind; the creation's result returned.
CRASH_GUARDS_ABI std::int64_t QueryCreated(std::uint64_t, const std::uint64_t* saved);
/// Replace's targets at kQueryBegin and kQueryEnd: the query and the render command's context (+8: the device).
CRASH_GUARDS_ABI std::uint64_t QueryBegin(std::uint64_t query, std::uint64_t context);
CRASH_GUARDS_ABI std::uint64_t QueryEnd(std::uint64_t query, std::uint64_t context);

struct Counts {
    unsigned hits_dropped, queries_failed, queries_recreated, queries_skipped;
};
Counts Counted();
void Reset(); ///< tests: counts and the failed queries forgotten
} // namespace BbEngine::CrashGuards
