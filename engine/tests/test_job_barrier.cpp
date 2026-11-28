// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 18 (shim/engine/job_barrier.h): the fork-join dispatcher's two wait caves run for real,
// on x86-64 Linux. A fake image holds, where each wait goes on, a way back to the test; a harness
// loads the registers the dispatcher has at each site, sets the stack as in its body, and jumps to
// the cave as the site's jump would. Checked: the sites' bytes branch where the header says; each
// cave hands its wait the game's operands; the wait is called with the stack aligned; each exit is
// the game's for the same state (all in, work arrived, every slot done); the registers the game
// keeps live come back unchanged and the stack balanced; a long wait yields, a short one doesn't.
#include "gpu/shim/engine/job_barrier.h"

#include <sched.h>
#include <sys/mman.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace BbEngine::JobBarrier;

struct Regs {
    std::uint64_t rbx, r12, r13, r14, r15, rbp, rcx, rdx, rax, cave;
};
struct Out {
    std::uint64_t rbx, r12, r13, r14, r15, rbp, rsp;
};
extern "C" {
Out g_out;
std::uint64_t g_saved_rsp, g_site_rsp;
int harness_enter(const Regs* regs);
void harness_exit();
}

// The harness: keeps the host's callee-saved registers, loads the site's, aligns the stack as in
// the dispatcher's body (its entry's return address and five pushes: 16-byte aligned), and jumps to
// the cave. An exit stub puts its number in eax and jumps to harness_exit, which records the
// registers and returns that number.
asm(R"(
.intel_syntax noprefix
.text
.globl harness_enter
harness_enter:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rip + g_saved_rsp], rsp
    mov rbx, [rdi]
    mov r12, [rdi + 8]
    mov r13, [rdi + 16]
    mov r14, [rdi + 24]
    mov r15, [rdi + 32]
    mov rbp, [rdi + 40]
    mov rcx, [rdi + 48]
    mov rdx, [rdi + 56]
    mov rax, [rdi + 64]
    mov r11, [rdi + 72]
    sub rsp, 8
    mov [rip + g_site_rsp], rsp
    jmp r11
.globl harness_exit
harness_exit:
    mov [rip + g_out], rbx
    mov [rip + g_out + 8], r12
    mov [rip + g_out + 16], r13
    mov [rip + g_out + 24], r14
    mov [rip + g_out + 32], r15
    mov [rip + g_out + 40], rbp
    mov [rip + g_out + 48], rsp
    mov rsp, [rip + g_saved_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
.att_syntax prefix
)");

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

constexpr std::uint64_t kTestBudgetUs = 50;
std::uint64_t TicksPerUs() {
    static const std::uint64_t per_us = [] {
        const auto t0 = std::chrono::steady_clock::now();
        const std::uint64_t c0 = __rdtsc();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const std::uint64_t c1 = __rdtsc();
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        return static_cast<std::uint64_t>((c1 - c0) / us);
    }();
    return per_us;
}

struct Seen {
    const void* counter = nullptr;
    std::int32_t need = 0;
    const void* more = nullptr;
    std::uint64_t base = 0;
    std::uint32_t first = 0, end = 0;
    bool aligned = false;
    Waited waited;
} g_seen;

void Yield() {
    sched_yield();
}

__attribute__((sysv_abi, noinline)) std::int32_t TestCountWait(const void* counter, std::int32_t need,
                                                                 const void* more) {
    g_seen.aligned = (reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)) & 15) == 0;
    g_seen.counter = counter;
    g_seen.need = need;
    g_seen.more = more;
    g_seen.waited = Wait([&] { return CountReady(counter, need, more); }, kTestBudgetUs * TicksPerUs(), Yield);
    return g_seen.waited.result == 1 ? 0 : 1;
}

__attribute__((sysv_abi, noinline)) void TestSlotsWait(std::uint64_t base, std::uint32_t first, std::uint32_t end) {
    g_seen.aligned = (reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)) & 15) == 0;
    g_seen.base = base;
    g_seen.first = first;
    g_seen.end = end;
    std::uint32_t next = first;
    g_seen.waited = Wait([&] { return SlotsReady(base, next, end); }, kTestBudgetUs * TicksPerUs(), Yield);
}

void Put(std::uint8_t*& at, std::initializer_list<std::uint8_t> bytes) {
    for (const std::uint8_t b : bytes) *at++ = b;
}
void Put64(std::uint8_t*& at, std::uint64_t value) {
    std::memcpy(at, &value, 8);
    at += 8;
}
// At an exit: mov eax, number; movabs r11, harness_exit; jmp r11.
void Exit(std::uint8_t* image, std::size_t offset, std::uint32_t number) {
    std::uint8_t* at = image + offset;
    Put(at, {0xb8});
    std::memcpy(at, &number, 4);
    at += 4;
    Put(at, {0x49, 0xbb});
    Put64(at, reinterpret_cast<std::uint64_t>(&harness_exit));
    Put(at, {0x41, 0xff, 0xe3});
}

// The registers the game keeps, with values a cave could not make up.
Regs Kept() {
    Regs r{};
    r.rbx = 0x1111222233334444ull;
    r.r12 = 0x5555666677778888ull;
    r.r13 = 0x99990000aaaabbbbull;
    r.r14 = 0xccccddddeeeeffffull;
    r.rbp = 0x0123456789abcdefull;
    return r;
}

void CheckKept(const Regs& in, const char* site) {
    char what[128];
    std::snprintf(what, sizeof what, "%s: rbx, r12, r13, r14, r15 and rbp come back unchanged", site);
    check(g_out.rbx == in.rbx && g_out.r12 == in.r12 && g_out.r13 == in.r13 && g_out.r14 == in.r14 &&
              g_out.r15 == in.r15 && g_out.rbp == in.rbp,
          what);
    std::snprintf(what, sizeof what, "%s: the stack as the site left it, the wait called aligned", site);
    check(g_out.rsp == g_site_rsp && g_seen.aligned, what);
}

void CheckSites() {
    // jle rel32 at +3 (6 bytes) to the all-in exit; je rel8 at +16 back to the site; the byte's
    // rip-relative displacement at +11, from the end of its 7-byte cmp.
    std::int32_t rel = 0;
    std::memcpy(&rel, kCountWaitCode + 5, 4);
    check(kCountWaitCode[3] == 0x0f && kCountWaitCode[4] == 0x8e && kCountWait + 9 + rel == kCountDone,
          "the count wait's jle reaches its all-in exit");
    std::memcpy(&rel, kCountWaitCode + 11, 4);
    check(kCountWait + 16 + rel == kMoreWork, "the count wait's byte is the work-arrived byte");
    check(kCountWaitCode[16] == 0x74 && kCountWait + 18 + std::int8_t(kCountWaitCode[17]) == kCountWait,
          "the count wait's je loops to the site");
    check(kCountWait + sizeof(kCountWaitCode) == kCountMore, "the count wait falls through to work arrived");
    // The slot wait: jne rel8 at +37 to +32 (the inner head), jb rel8 at +43 to the site, ending at its exit.
    check(kSlotsWaitCode[37] == 0x75 && 39 + std::int8_t(kSlotsWaitCode[38]) == 32, "the slot wait's jne loops on a slot");
    check(kSlotsWaitCode[43] == 0x72 && 45 + std::int8_t(kSlotsWaitCode[44]) == 0, "the slot wait's jb loops to the site");
    check(kSlotsWait + sizeof(kSlotsWaitCode) == kSlotsDone, "the slot wait ends at its exit");
    check(sizeof(kCountWaitCode) >= 14 && sizeof(kSlotsWaitCode) >= 14, "both sites hold the 14-byte jump");
    std::uint32_t stride = 0, state = 0;
    std::memcpy(&stride, kSlotsWaitCode + 5, 4);
    std::memcpy(&state, kSlotsWaitCode + 13, 4);
    check(stride == kSlotStride && state == kSlotState && kSlotsWaitCode[36] == kSlotDone,
          "the slots' stride, state offset and done state are the game's");
}

void CheckConditions() {
    alignas(4) std::int32_t counter = 0;
    std::uint8_t more = 0;
    counter = 3;
    check(CountReady(&counter, 3, &more) == 1, "all in at need == counter");
    check(CountReady(&counter, 4, &more) == 0, "not in below need");
    more = 1;
    check(CountReady(&counter, 4, &more) == 2, "work arrived when the byte is set");
    check(CountReady(&counter, 3, &more) == 1, "all in comes first, as the game looks");
    counter = -3;
    more = 0;
    check(CountReady(&counter, 5, &more) == 0, "the counter compares signed, as jle");
    check(CountReady(&counter, -4, &more) == 1, "a negative need is in at once, as jle");
}

void RunCount(std::uint8_t* image, std::uint8_t* page) {
    std::uint8_t cave[kCaveSize];
    const std::size_t length = CountCave(reinterpret_cast<std::uint64_t>(image),
                                         reinterpret_cast<std::uint64_t>(&TestCountWait), cave);
    check(length <= kCaveSize, "the count cave fits");
    std::memcpy(page, cave, length);
    auto* const more = image + kMoreWork;
    alignas(64) static std::int32_t counter;

    // Already all in: the all-in exit, at once, with the game's operands.
    counter = 7;
    *more = 0;
    Regs r = Kept();
    r.r12 = 0xdeadbeef00000007ull; // only r12d is N-1
    r.r15 = reinterpret_cast<std::uint64_t>(&counter);
    r.cave = reinterpret_cast<std::uint64_t>(page);
    r.rax = r.rcx = r.rdx = 0xbad;
    g_seen = {};
    check(harness_enter(&r) == 1, "count: all in goes on at 0x21b8930");
    check(g_seen.counter == &counter && g_seen.need == 7 && g_seen.more == more,
          "count: the wait gets [r15]'s address, r12d and the byte");
    check(g_seen.waited.yields == 0, "count: a wait already over doesn't yield");
    CheckKept(r, "count, all in");

    // Not all in, work arrived: that exit.
    counter = 2;
    *more = 1;
    g_seen = {};
    check(harness_enter(&r) == 2, "count: work arrived goes on at 0x21b88a2");
    CheckKept(r, "count, work arrived");

    // The workers count themselves in later: pause first, then yields, then the all-in exit.
    counter = 0;
    *more = 0;
    g_seen = {};
    std::thread workers([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        for (int i = 0; i < 7; ++i) __atomic_fetch_add(&counter, 1, __ATOMIC_RELEASE);
    });
    const int exit = harness_enter(&r);
    workers.join();
    check(exit == 1, "count: a late count in goes on at 0x21b8930");
    check(g_seen.waited.yields > 0, "count: a wait past the budget yields");
    check(g_seen.waited.ticks >= kTestBudgetUs * TicksPerUs(), "count: its time counted");
    CheckKept(r, "count, late");
}

void RunSlots(std::uint8_t* image, std::uint8_t* page) {
    std::uint8_t cave[kCaveSize];
    const std::size_t length = SlotsCave(reinterpret_cast<std::uint64_t>(image),
                                         reinterpret_cast<std::uint64_t>(&TestSlotsWait), cave);
    check(length <= kCaveSize, "the slot cave fits");
    std::memcpy(page + 1024, cave, length);
    constexpr std::uint32_t kWorkers = 4;
    static std::uint8_t list[kSlotStride * kWorkers + 64];
    const auto state = [](std::uint32_t i) {
        return reinterpret_cast<std::uint32_t*>(list + i * kSlotStride + kSlotState);
    };
    std::memset(list, 0, sizeof list);
    *state(0) = 9; // the main thread's own slot: never looked at
    for (std::uint32_t i = 1; i < kWorkers; ++i) *state(i) = 2;

    Regs r = Kept();
    r.r15 = 0x7777777777777777ull;
    r.rcx = reinterpret_cast<std::uint64_t>(list);
    r.rdx = 0xffffffff00000001ull; // edx: the first slot
    r.rax = 0xffffffff00000000ull | kWorkers; // eax: N
    r.cave = reinterpret_cast<std::uint64_t>(page + 1024);
    g_seen = {};
    check(harness_enter(&r) == 3, "slots: all done goes on at 0x21b896d");
    check(g_seen.base == reinterpret_cast<std::uint64_t>(list) && g_seen.first == 1 && g_seen.end == kWorkers,
          "slots: the wait gets rcx, edx and eax");
    check(g_seen.waited.yields == 0, "slots: a wait already over doesn't yield");
    CheckKept(r, "slots, done");

    // Slot 2 done late, slot 3 later still: in order, then the exit.
    *state(2) = 1;
    *state(3) = 0;
    g_seen = {};
    std::thread workers([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        __atomic_store_n(state(3), 2u, __ATOMIC_RELEASE);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        __atomic_store_n(state(2), 2u, __ATOMIC_RELEASE);
    });
    const int exit = harness_enter(&r);
    workers.join();
    check(exit == 3, "slots: late slots go on at 0x21b896d");
    check(g_seen.waited.yields > 0, "slots: a wait past the budget yields");
    CheckKept(r, "slots, late");
}

} // namespace

int main() {
    const std::size_t size = kMoreWork + 0x1000;
    auto* image = static_cast<std::uint8_t*>(mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    auto* page = static_cast<std::uint8_t*>(
        mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (image == MAP_FAILED || page == MAP_FAILED) {
        std::printf("job-barrier-test: no executable memory\n");
        return 1;
    }
    Exit(image, kCountDone, 1);
    Exit(image, kCountMore, 2);
    Exit(image, kSlotsDone, 3);
    CheckSites();
    CheckConditions();
    RunCount(image, page);
    RunSlots(image, page);
    if (g_fail) {
        std::printf("job-barrier-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("job-barrier-test: ok\n");
    return 0;
}
