// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 15 part 4 (shim/engine/copy_cave.h): both caves run for real, on x86-64 Linux. A fake
// image holds the game's memcpy (a jump to TestMemcpy) and, where each site goes on, a way back to
// the test; a harness sets the registers each site keeps the copy's arguments in and jumps to the
// cave as the site's jump would. Checked: the notice gets the destination and size first, the copy
// then gets the game's own arguments, both are called with the stack aligned as at the game's call,
// and the code comes back where the game goes on.
#include "gpu/shim/engine/copy_cave.h"

#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace BbEngine::GxResources;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

struct Seen {
    int order = 0;
    std::uint64_t notice_dest = 0, notice_size = 0;
    int notice_at = 0;
    bool notice_aligned = false;
    void* copy_dest = nullptr;
    const void* copy_src = nullptr;
    std::size_t copy_size = 0;
    int copy_at = 0;
    bool copy_aligned = false;
} g_seen;

__attribute__((sysv_abi, noinline)) void TestNotice(std::uint64_t destination, std::uint64_t size) {
    // With a frame pointer, rbp = the entry rsp - 8: 16-byte aligned when the call was.
    g_seen.notice_aligned = (reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)) & 15) == 0;
    g_seen.notice_dest = destination;
    g_seen.notice_size = size;
    g_seen.notice_at = ++g_seen.order;
}

__attribute__((sysv_abi, noinline)) void* TestMemcpy(void* dst, const void* src, std::size_t n) {
    g_seen.copy_aligned = (reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)) & 15) == 0;
    g_seen.copy_dest = dst;
    g_seen.copy_src = src;
    g_seen.copy_size = n;
    g_seen.copy_at = ++g_seen.order;
    std::memcpy(dst, src, n);
    return dst;
}

/// The registers a site keeps the copy's arguments in.
struct Context {
    std::uint64_t rbx, r12, r14, r13, rbp;
};

void Put(std::uint8_t*& at, std::initializer_list<std::uint8_t> bytes) {
    for (const std::uint8_t b : bytes) *at++ = b;
}
void Put64(std::uint8_t*& at, std::uint64_t value) {
    std::memcpy(at, &value, 8);
    at += 8;
}

void RunSite(int texture, std::uint8_t* image, std::uint8_t* page) {
    g_seen = {};
    // The cave, in a page of its own as Divert puts it.
    std::uint8_t cave[64];
    const std::size_t length =
        CopyCave(texture, reinterpret_cast<std::uint64_t>(image), reinterpret_cast<std::uint64_t>(&TestNotice), cave);
    check(length <= sizeof cave, "the cave fits");
    std::uint8_t* cave_at = page + 1024;
    std::memcpy(cave_at, cave, length);
    // Where the game goes on: the harness's way back.
    std::uint8_t* back = image + kCopySites[texture].back;
    Put(back, {0x48, 0x83, 0xc4, 0x08});                                     // add rsp, 8
    Put(back, {0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5d, 0x5b}); // pop r15 .. rbx
    Put(back, {0xc3});                                                       // ret
    // The harness: keep the callee-saved registers, load the site's, align as at a call, jump.
    std::uint8_t* h = page;
    Put(h, {0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}); // push rbx .. r15
    Put(h, {0x48, 0x8b, 0x1f});                                           // mov rbx, [rdi]
    Put(h, {0x4c, 0x8b, 0x67, 0x08});                                     // mov r12, [rdi + 8]
    Put(h, {0x4c, 0x8b, 0x77, 0x10});                                     // mov r14, [rdi + 16]
    Put(h, {0x4c, 0x8b, 0x6f, 0x18});                                     // mov r13, [rdi + 24]
    Put(h, {0x48, 0x8b, 0x6f, 0x20});                                     // mov rbp, [rdi + 32]
    Put(h, {0x48, 0x83, 0xec, 0x08});                                     // sub rsp, 8
    Put(h, {0x48, 0xb8});                                                 // movabs rax, the cave
    Put64(h, reinterpret_cast<std::uint64_t>(cave_at));
    Put(h, {0xff, 0xe0}); // jmp rax

    char source[300], destination[300] = {};
    for (int i = 0; i < 300; ++i) source[i] = static_cast<char>(i * 7);
    const char* source_pointer = source;
    std::uint32_t size32 = 257;
    std::uint64_t frame[64] = {};
    Context ctx{};
    if (!texture) {
        ctx.rbx = reinterpret_cast<std::uint64_t>(&source_pointer); // [rbx]: the source
        ctx.r12 = reinterpret_cast<std::uint64_t>(&size32);         // [r12]: the size
        ctx.r14 = reinterpret_cast<std::uint64_t>(destination);     // r14: the destination
    } else {
        ctx.r14 = reinterpret_cast<std::uint64_t>(&source_pointer); // [r14]: the source
        ctx.r13 = reinterpret_cast<std::uint64_t>(destination);     // r13: the destination
        ctx.rbp = reinterpret_cast<std::uint64_t>(&frame[40]);      // [rbp - 0xd0]: the size
        const std::uint64_t size64 = 263;
        std::memcpy(reinterpret_cast<std::uint8_t*>(ctx.rbp) - 0xd0, &size64, 8);
    }
    reinterpret_cast<void (*)(Context*)>(page)(&ctx);
    const std::uint64_t want = texture ? 263 : 257;
    char what[96];
    std::snprintf(what, sizeof what, "site %d: the notice first, with the destination and size", texture);
    check(g_seen.notice_at == 1 && g_seen.notice_dest == reinterpret_cast<std::uint64_t>(destination) &&
              g_seen.notice_size == want,
          what);
    std::snprintf(what, sizeof what, "site %d: then the game's copy, with its own arguments", texture);
    check(g_seen.copy_at == 2 && g_seen.copy_dest == destination && g_seen.copy_src == source &&
              g_seen.copy_size == want && !std::memcmp(destination, source, want) && destination[want] == 0,
          what);
    std::snprintf(what, sizeof what, "site %d: both called with the stack aligned", texture);
    check(g_seen.notice_aligned && g_seen.copy_aligned, what);
}

} // namespace

int main() {
    // The fake image: up to the game's memcpy and a little past it (reserved, touched only there).
    const std::size_t size = kGameMemcpy + 0x1000;
    auto* image = static_cast<std::uint8_t*>(mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    auto* page = static_cast<std::uint8_t*>(
        mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (image == MAP_FAILED || page == MAP_FAILED) {
        std::printf("copy-cave-test: no executable memory\n");
        return 1;
    }
    std::uint8_t* at = image + kGameMemcpy; // the game's memcpy: movabs rax, TestMemcpy; jmp rax
    Put(at, {0x48, 0xb8});
    Put64(at, reinterpret_cast<std::uint64_t>(&TestMemcpy));
    Put(at, {0xff, 0xe0});

    // The sites' bytes are 1.09's: three loads and a call whose rel32 reaches the game's memcpy.
    for (const CopySite& site : kCopySites) {
        std::int32_t rel = 0;
        std::memcpy(&rel, site.bytes + site.size - 4, 4);
        check(site.bytes[site.size - 5] == 0xe8 && site.at + site.size + rel == kGameMemcpy,
              "a site's call reaches the game's memcpy");
        check(site.back == site.at + site.size && site.size >= 14, "a site holds the 14-byte jump and ends where the game goes on");
    }
    RunSite(0, image, page);
    RunSite(1, image, page);
    if (g_fail) {
        std::printf("copy-cave-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("copy-cave-test: ok\n");
    return 0;
}
