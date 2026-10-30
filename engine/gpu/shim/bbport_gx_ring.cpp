// SPDX-License-Identifier: GPL-2.0-or-later
// See bbport_gx_ring.h.
#include "bbport_gx_ring.h"

#include <cstdio>

namespace BbGxRing {
namespace {
std::atomic<bool> g_logged{false}, g_reported{false};
} // namespace

bool NoteRegion(std::uint64_t start, std::uint64_t end) {
    if (!start || end <= start || end - start > (std::uint64_t(1) << 30)) {
        std::printf("GPU memory: the game's GX range allocator made at %#llx..%#llx, which cannot be it: tracked "
                    "as before\n",
                    (unsigned long long)start, (unsigned long long)end);
        return false;
    }
    g_end.store(end, std::memory_order_relaxed);
    g_start.store(start, std::memory_order_release);
    if (!g_logged.exchange(true)) {
        std::printf("GPU memory: the game's GX range allocator at %#llx..%#llx (%llu MiB): read-only buffers in "
                    "it are copied when used, not tracked (BB_GX_RING_STREAM_KB=0: tracked as before)\n",
                    (unsigned long long)start, (unsigned long long)end, (unsigned long long)((end - start) >> 20));
    }
    return true;
}

void ReportMissing() {
    if (Known() || g_reported.exchange(true)) return;
    std::printf("GPU memory: the game's GX range allocator was not seen being made (the engine's hook on its "
                "init left out, see above, or no graphics device yet): tracked as before\n");
}
} // namespace BbGxRing
