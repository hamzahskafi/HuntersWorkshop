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
// The 96-byte hit array's push (0xb84ce0, a virtual: rdi the collector, rsi the hit): its data at +0x20, its
// count at +0x28, its capacity in the low 30 bits of +0x2c; a full array is grown, then the hit copied in. A
// degenerate query (a zero-length ray while the world is paused) filled one without end until an allocation
// failed (SIGSEGV at 0xf00010, SrShadowy's fork). Past kHitCap hits the push is skipped: it returns nothing and
// does nothing else, so a hit dropped is all that changes (bbport writes it into a scratch array instead).
constexpr std::size_t kHitPush = 0xb84ce0;
// push rbp; mov rbp, rsp; push r14; push rbx; mov rbx, rsi; mov r14, rdi; mov eax, [r14 + 0x28]
constexpr std::uint8_t kHitPushEntry[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x89,
                                          0xf3, 0x49, 0x89, 0xfe, 0x41, 0x8b, 0x46, 0x28};
constexpr std::int32_t kHitCap = 65536;
constexpr std::uint64_t kHitData = 0x20, kHitCount = 0x28;
/// A push onto `collector` is skipped: kHitCap hits in it already, or a count with no data.
bool HitFull(std::uint64_t collector);
/// Prepare's callback at kHitPush: saved[5] the collector.
CRASH_GUARDS_ABI std::int64_t HitPush(std::uint64_t, const std::uint64_t* saved);

// ---- GX occlusion queries -----------------------------------------------------------------------------
// A query (0x30 bytes; its handle at +0, its state at +8: 0 new, 2 begun, 1 ended; its result at +0x10) gets
// its handle from its creation (0xe2f080, query and kind 1, from the pool's 0x2288d00), which asks the GX
// device (0x21674f0) for a 0x18-byte handle and its data; out of room it returns false and leaves the handle
// null. The render thread's begin (0xe2f130: state 0 -> 2, then the device's begin 0x2169ec0(device, device +
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
