// SPDX-License-Identifier: GPL-3.0-or-later
// The FMOD ring guard's helpers (gpu/shim/engine/fmod_ring.h) on a synthetic ring in host memory.
#include "gpu/shim/engine/fmod_ring.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace BbEngine::FmodRing;

namespace {
const auto Read = [](std::uint64_t at, auto* out) {
    if (at < 0x10000) return false;
    std::memcpy(out, reinterpret_cast<const void*>(at), sizeof *out);
    return true;
};
const auto Write = [](std::uint64_t at, auto value) { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof value); };

template <typename T>
T At(std::uint64_t at) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
    return value;
}

// A head node (next, prev) and sounds, each with its node at +0x1d0.
struct Ring {
    alignas(16) std::uint64_t head[3] = {};
    std::vector<std::vector<std::uint8_t>> sounds;
    explicit Ring(int n) : sounds(n, std::vector<std::uint8_t>(0x240, 0)) {
        std::uint64_t prev = Head();
        for (int i = 0; i <= n; ++i) {
            const std::uint64_t node = i < n ? Sound(i) + kNode : Head();
            Write(prev, node);
            Write(node + 8, prev);
            if (i < n) Write(node + 16, Sound(i));
            prev = node;
        }
    }
    std::uint64_t Head() { return reinterpret_cast<std::uint64_t>(head); }
    std::uint64_t Sound(int i) { return reinterpret_cast<std::uint64_t>(sounds[i].data()); }
};
} // namespace

int main() {
    Ring ring(3);
    for (int i = 0; i < 3; ++i) assert(Linked(ring.Sound(i), Read));

    // The middle one out: its neighbours joined, its node linked to itself with no data.
    const std::uint64_t a = ring.Sound(0), b = ring.Sound(1), c = ring.Sound(2);
    assert(Unlink(b, Read, Write));
    assert(At<std::uint64_t>(a + kNode) == c + kNode && At<std::uint64_t>(c + kNode + 8) == a + kNode);
    assert(At<std::uint64_t>(b + kNode) == b + kNode && At<std::uint64_t>(b + kNode + 8) == b + kNode);
    assert(At<std::uint64_t>(b + kNode + 16) == 0);
    assert(!Linked(b, Read) && !Unlink(b, Read, Write));
    assert(Linked(a, Read) && Linked(c, Read));

    // A node whose neighbour no longer points back is not linked, and nothing is written to it.
    Write(a + kNode + 8, std::uint64_t{0x1234});
    Write(ring.Head() + 8, std::uint64_t{0x5678}); // the head's prev: c's next points at the head
    Write(ring.Head(), std::uint64_t{0x10});        // the head's next no longer names a
    assert(!Linked(a, Read) && !Unlink(a, Read, Write));
    assert(At<std::uint64_t>(a + kNode + 8) == 0x1234);

    // A shared subsound names itself; it refuses its release while 0x20, 0x100 or 0x200 is set.
    assert(!RefusedShared(c, Read));
    Write(c + kShared, c);
    assert(!RefusedShared(c, Read));
    for (const std::uint32_t flag : {0x20u, 0x100u, 0x200u}) {
        Write(c + kFlags, flag | 0x1u);
        assert(RefusedShared(c, Read));
    }
    Write(c + kShared, a);
    assert(!RefusedShared(c, Read));

    // Kept: a sound of its own, in the error state.
    Write(c + kShared, c);
    Write(c + kParent, a);
    Write(c + kStreamSample, std::uint64_t{0x9999});
    Write(c + kOpenState, std::uint32_t{0});
    Orphan(c, Write);
    assert(!At<std::uint64_t>(c + kParent) && !At<std::uint64_t>(c + kShared) && !At<std::uint64_t>(c + kStreamSample));
    assert(At<std::uint32_t>(c + kOpenState) == kOpenStateError);
    assert(At<std::uint32_t>(c + kOpenState + 4) == 0); // four bytes, as FMOD_OPENSTATE
    std::puts("PASS: FMOD ring guard: linked, unlinked as SoundI::release leaves it, refusing shared subsound, kept");
}
