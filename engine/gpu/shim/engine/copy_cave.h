// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 15 part 4 (BB_COPY_NOTICE; docs/memory-model.md, beta 15 part 4): the caves at the GX
// creators' copies of the caller's data. Both copy with the game's memcpy (0x261a1b0), each from one
// call site: CreateBuffer's helper 0x216d420 at 0x216d5ea, CreateTexture's helper 0x2171d60 at
// 0x2171fea. Each cave takes the place of the three argument loads and the call (15 and 18 bytes,
// checked against 1.09's before it is placed; nothing branches into them), tells the engine the
// destination and size, makes the game's copy, and jumps back. Plain code: tests/test_copy_cave.cpp
// runs both.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace BbEngine::GxResources {

constexpr std::size_t kGameMemcpy = 0x261a1b0;

struct CopySite {
    std::size_t at, back;        ///< the argument loads and the call; where the game goes on
    const std::uint8_t* bytes;   ///< 1.09's there
    std::size_t size;
};

inline constexpr std::uint8_t kBufferCopyCode[] = {
    0x48, 0x8b, 0x33,             // mov rsi, [rbx]        the source
    0x41, 0x8b, 0x14, 0x24,       // mov edx, [r12]        the size (32 bits)
    0x4c, 0x89, 0xf7,             // mov rdi, r14          the destination
    0xe8, 0xc1, 0xcb, 0x4a, 0x00, // call 0x261a1b0
};
inline constexpr std::uint8_t kTextureCopyCode[] = {
    0x49, 0x8b, 0x36,                         // mov rsi, [r14]        the source
    0x4c, 0x89, 0xef,                         // mov rdi, r13          the destination
    0x48, 0x8b, 0x95, 0x30, 0xff, 0xff, 0xff, // mov rdx, [rbp - 0xd0] the size
    0xe8, 0xc1, 0x81, 0x4a, 0x00,             // call 0x261a1b0
};
inline constexpr CopySite kCopySites[2] = {
    {0x216d5e0, 0x216d5ef, kBufferCopyCode, sizeof(kBufferCopyCode)},
    {0x2171fdd, 0x2171fef, kTextureCopyCode, sizeof(kTextureCopyCode)},
};

/// Site `texture`'s cave (0: the buffer's, 1: the texture's) for the image at `image`, calling
/// `notice` (sysv_abi, (destination, size)) before the copy. Returns its length. Reached by a jump
/// from the site, so rsp is as at the game's call (16-byte aligned): each call keeps it so.
inline std::size_t CopyCave(int texture, std::uint64_t image, std::uint64_t notice, std::uint8_t (&cave)[64]) {
    std::uint8_t* c = cave;
    const auto put = [&](std::initializer_list<std::uint8_t> bytes) {
        for (const std::uint8_t b : bytes) *c++ = b;
    };
    const auto call = [&](std::uint64_t fn) { // movabs rax, fn; call rax
        put({0x48, 0xb8});
        std::memcpy(c, &fn, 8);
        c += 8;
        put({0xff, 0xd0});
    };
    const CopySite& site = kCopySites[texture ? 1 : 0];
    if (!texture) {
        put({0x4c, 0x89, 0xf7});       // mov rdi, r14     the destination
        put({0x41, 0x8b, 0x34, 0x24}); // mov esi, [r12]   the size
    } else {
        put({0x4c, 0x89, 0xef});                         // mov rdi, r13          the destination
        put({0x48, 0x8b, 0xb5, 0x30, 0xff, 0xff, 0xff}); // mov rsi, [rbp - 0xd0] the size
    }
    call(notice);
    for (std::size_t i = 0; i + 5 < site.size; ++i) *c++ = site.bytes[i]; // the game's own loads
    call(image + kGameMemcpy);
    put({0xff, 0x25, 0, 0, 0, 0}); // jmp [rip + 0]: where the game goes on
    const std::uint64_t back = image + site.back;
    std::memcpy(c, &back, 8);
    c += 8;
    return static_cast<std::size_t>(c - cave);
}

} // namespace BbEngine::GxResources
