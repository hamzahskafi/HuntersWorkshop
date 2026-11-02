// SPDX-License-Identifier: GPL-3.0-or-later
// The follow camera's wall turn run as the game runs it: its own code from the 1.09 executable (argument 1, or
// out/eboot.elf; skipped without it), patch 0x183dc3a (after the two probes' casts: r14b probe A's hit, al probe
// B's) to 0x183de87 (before the turn's sine), frame after frame, with and without camera_rate.cpp's caves. Probe A
// hits for 0.3 s, then neither does for 0.7 s; the turn the camera makes (+0x294 a frame) is summed. With the caves
// every rate must turn as the game does at 30 (within 4%), and at 30 exactly as it does; the game's own code turns
// nothing above 60, its first step under its 1 degree cut-off every frame.
#include "../gpu/shim/engine/camera_rate.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// rdi: the code, rsi: the camera (r13), rdx: the frame (rbp), rcx: al in bits 0-7, r14b in bits 8-15.
extern "C" __attribute__((sysv_abi)) void bb_run_wall(const void* code, void* camera, void* frame, std::uint64_t probes);
asm(R"(
    .text
    .globl bb_run_wall
bb_run_wall:
    .intel_syntax noprefix
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    mov r13, rsi
    mov rbp, rdx
    mov rax, rcx
    shr rcx, 8
    movzx r14d, cl
    and eax, 0xff
    call rdi
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
    .att_syntax prefix
)");

namespace {
constexpr std::uint64_t kBase = 0x400000;
constexpr double kDegree = 0.017453292519943295;

std::uint8_t* Allocate(std::size_t n) {
#ifdef _WIN32
    return static_cast<std::uint8_t*>(VirtualAlloc(nullptr, n, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#else
    void* p = mmap(nullptr, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(p);
#endif
}

// The executable's loaded segments at their image offsets, a ret at the two ends of the wall code.
std::uint8_t* Map(const std::vector<std::uint8_t>& elf, std::size_t& size) {
    std::uint64_t phoff;
    std::uint16_t phentsize, phnum;
    std::memcpy(&phoff, elf.data() + 0x20, 8);
    std::memcpy(&phentsize, elf.data() + 0x36, 2);
    std::memcpy(&phnum, elf.data() + 0x38, 2);
    size = 0;
    for (int pass = 0; pass < 2; ++pass) {
        std::uint8_t* image = pass ? Allocate(size) : nullptr;
        if (pass && !image) return nullptr;
        for (std::uint16_t i = 0; i < phnum; ++i) {
            const std::uint8_t* ph = elf.data() + phoff + std::size_t(i) * phentsize;
            std::uint32_t type;
            std::uint64_t offset, vaddr, filesz, memsz;
            std::memcpy(&type, ph, 4);
            std::memcpy(&offset, ph + 8, 8);
            std::memcpy(&vaddr, ph + 16, 8);
            std::memcpy(&filesz, ph + 32, 8);
            std::memcpy(&memsz, ph + 40, 8);
            if (type != 1) continue;
            if (!pass) size = std::max<std::size_t>(size, (vaddr + memsz + 0xfff) & ~std::uint64_t(0xfff));
            else if (offset + filesz <= elf.size()) std::memcpy(image + vaddr, elf.data() + offset, filesz);
        }
        if (pass) {
            image[0x183de87 - kBase] = 0xc3; // the turn's sine
            image[0x183e271 - kBase] = 0xc3; // no turn
            return image;
        }
    }
    return nullptr;
}

// Degrees turned over 1 s at `fps`: probe A hitting at `fraction` for 0.3 s, then neither probe.
double Turned(std::uint8_t* image, double fps, float fraction) {
    alignas(16) static std::uint8_t camera[0x400];
    alignas(16) static std::uint8_t frame[0x800];
    std::memset(camera, 0, sizeof camera);
    const auto field = [&](std::uint32_t at, float v) { std::memcpy(camera + at, &v, 4); };
    // The game's own: the escape rates near (+0x29c) and far (+0x2a4), the near share (+0x2a8), the limit a second
    // (+0x2b0) and the fade (+0x2b4); the wall escape held (+0x28c).
    field(0x29c, 1.0471976f), field(0x2a4, 0.17453292f), field(0x2a8, 0.3f), field(0x2b0, 4.18879f);
    field(0x2b4, 0.55f);
    camera[0x28c] = 1;
    const double dt = 1 / fps;
    BbEngine::CameraRate::Frame(dt);
    std::uint8_t* rbp = frame + 0x600;
    double turned = 0;
    const int frames = int(std::lround(fps)), hit = int(std::lround(0.3 * fps));
    for (int k = 0; k < frames; ++k) {
        alignas(16) const float step[4] = {float(dt), 0, 0, 0};
        std::memcpy(rbp - 0x250, step, 16);
        const float b = 1.0f;
        std::memcpy(rbp - 0x1a4, &fraction, 4);
        std::memcpy(rbp - 0x1a8, &b, 4);
        bb_run_wall(image + (0x183dc3a - kBase), camera, rbp, k < hit ? 0x100 : 0);
        float turn;
        std::memcpy(&turn, camera + 0x294, 4);
        turned += turn;
    }
    return std::fabs(turned) / kDegree;
}
} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "out/eboot.elf";
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::printf("camera-wall-test: skipped (no %s)\n", path);
        return 0;
    }
    const std::vector<std::uint8_t> elf((std::istreambuf_iterator<char>(file)), {});
    std::size_t size = 0;
    std::uint8_t* game = Map(elf, size);
    std::uint8_t* ours = Map(elf, size);
    std::uint64_t bad = 0;
    if (!game || !ours || !BbEngine::CameraRate::Check(game, size, false, &bad)) {
        std::printf("camera-wall-test: skipped (%s is not the 1.09 executable at 0x%llx)\n", path,
                    (unsigned long long)bad);
        return 0;
    }
    if (!BbEngine::CameraRate::Install(ours, size, false)) {
        std::puts("camera-wall-test: the caves could not be installed");
        return 1;
    }
    int failed = 0;
    // Fractions: 0 (the near rate, 1.047 rad/s), 0.5 (0.80), 0.9 (0.42: under 1 degree a frame at 30, no turn).
    for (const float fraction : {0.0f, 0.5f, 0.9f}) {
        const double at30 = Turned(game, 30, fraction);
        const double ours30 = Turned(ours, 30, fraction);
        std::printf("fraction %.1f: the game at 30 %.2f degrees, with the caves %.2f\n", fraction, at30, ours30);
        if (ours30 != at30) ++failed;
        for (const double fps : {60.0, 90.0, 120.0, 144.0, 240.0}) {
            const double got = Turned(ours, fps, fraction), plain = Turned(game, fps, fraction);
            const bool within = at30 == 0 ? got == 0 : std::fabs(got - at30) <= 0.04 * at30;
            std::printf("  %3.0f FPS: with the caves %.2f, the game's own %.2f%s\n", fps, got, plain, within ? "" : "  <- off");
            failed += !within;
            if (fps >= 90 && plain != 0) ++failed; // the game's never starts above 60
        }
    }
    std::puts(failed ? "camera-wall-test: FAILED" : "camera-wall-test: ok");
    return failed ? 1 : 0;
}
