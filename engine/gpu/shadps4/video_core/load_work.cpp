// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 15: the load clock and part 5's line on each load's first frames (load_work.h).
// Everything here runs on the flip thread: one load at a time, its state in this file.

#include "video_core/load_work.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

#include "bbport_toggles.h"
#include "engine/frame_window.h"
#include "engine/loading.h"
#include "video_core/renderer_vulkan/pipeline_compile_queue.h"
#include "video_core/renderer_vulkan/vk_pipeline_common.h"
#include "video_core/texture_cache/ready_surfaces.h"

namespace VideoCore::LoadWork {
namespace {

constexpr u64 kAfterNs = 2'000'000'000ull; ///< the line covers the 2 s after the load
constexpr u64 kFreeNs = 3'000'000'000ull;  ///< ready surfaces not taken by then are freed
constexpr u64 kMakeFrame = 30;              ///< after YEBIS's rebuild at the screen's start

u64 NowNs() {
    return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count());
}

struct Snapshot {
    u64 made = 0, ahead = 0, ahead_copied = 0, drawn = 0, released = 0, dropped = 0, held = 0;
    u64 told = 0, pages_dropped = 0, pages_writable = 0;
    u64 uploads = 0, bind_ns = 0;
    ReadySurfaces::Totals ready;
    Vulkan::PipelineBurstTotals pipes;
    u64 waits = 0, wait_ns = 0;
};

/// `reset_most`: the most compile threads a burst used starts over (at a load's start and its line).
Snapshot Take(bool reset_most) {
    const auto& c = counters;
    Snapshot s;
    s.made = c.made_bytes.load();
    s.ahead = c.ahead_bytes.load();
    s.ahead_copied = c.ahead_copied.load();
    s.drawn = c.drawn_bytes.load();
    s.released = c.released_bytes.load();
    s.dropped = c.dropped_bytes.load();
    s.held = c.frames_held.load();
    s.told = c.copies_told.load();
    s.pages_dropped = c.pages_dropped.load();
    s.pages_writable = c.pages_writable.load();
    s.uploads = BbStats::buffer_upload_bytes.load();
    s.bind_ns = BbStats::sparse_bind_ns.load();
    s.ready = ReadySurfaces::TotalsNow();
    s.pipes = Vulkan::PipelineBurstTotalsNow(reset_most);
    s.waits = Vulkan::pipeline_compile_stats.waits_total.load();
    s.wait_ns = Vulkan::pipeline_compile_stats.wait_ns_total.load();
    return s;
}

struct Clock {
    u32 load = 0;
    bool in_load = false, made = false, reported = true, freed = true;
    u64 end_ns = 0;
    Snapshot begin, end;
} g;

double Mb(u64 bytes) {
    return double(bytes) / 1e6;
}

void Report(u64 now) {
    g.reported = true;
    ReadySurfaces::EndLearning();
    const BbEngine::Frames::Window frames = BbEngine::Frames::Close(BbEngine::Frames::kLoad);
    const Snapshot n = Take(true);
    const Snapshot& b = g.begin;
    const Snapshot& e = g.end;
    const u64 longest = Vulkan::pipeline_compile_stats.longest_wait_mark_ns.exchange(0);
    const u64 made = e.made - b.made;
    const u64 ahead = n.ahead - b.ahead, drawn = n.drawn - b.drawn, released = n.released - b.released,
              dropped = n.dropped - b.dropped;
    const u64 left = made - std::min(made, ahead + drawn + released); // dropped, or still queued
    (void)dropped;
    // Buffer uploads at draws after the screen: all of them less the queue's own.
    const u64 after_uploads = (n.uploads - e.uploads) - std::min(n.uploads - e.uploads, n.ahead_copied - e.ahead_copied);
    const u64 ready_made = n.ready.made - b.ready.made, ready_taken = n.ready.taken - b.ready.taken;
    const u64 jobs = n.pipes.jobs - b.pipes.jobs, bursts = n.pipes.bursts - b.pipes.bursts;
    const std::string maps = BbEngine::Loading::LastMaps();
    const std::string held =
        n.held != b.held ? " (VRAM not comfortable for " + std::to_string(n.held - b.held) + " frames)" : "";
    std::string pipes;
    {
        char text[256];
        std::snprintf(text, sizeof text,
                      "pipelines %llu compiled (%.0f ms of compiling), waits %llu (%.1f ms, longest %.1f ms), ",
                      static_cast<unsigned long long>(jobs), (n.pipes.compile_ns - b.pipes.compile_ns) / 1e6,
                      static_cast<unsigned long long>(n.waits - b.waits), (n.wait_ns - b.wait_ns) / 1e6,
                      longest / 1e6);
        pipes = text;
        if (bursts) {
            std::snprintf(text, sizeof text, "bursts %llu (%.2f s) on up to %u threads",
                          static_cast<unsigned long long>(bursts),
                          (n.pipes.burst_wall_ns - b.pipes.burst_wall_ns) / 1e9, n.pipes.threads_most);
        } else {
            std::snprintf(text, sizeof text, "no bursts");
        }
        pipes += text;
    }
    std::printf("Loading: the %.1f s after %s: frames over 25 ms %u of %u (worst %.1f ms); the game's buffers made "
                "during the screen %.1f MB: uploaded ahead %.1f MB, already uploaded by a draw %.1f MB, released "
                "unused %.1f MB, left to their draw %.1f MB%s; buffer uploads at draws after it %.1f MB; sparse "
                "binds during the screen %.1f ms, after it %.1f ms; surfaces ready %llu (%.0f MiB), taken %llu, "
                "allocated at their draw %llu (%.1f ms); %s; create-call copies told %llu, pages dropped without a "
                "readback %llu, made writable %llu\n",
                (now - g.end_ns) / 1e9, maps.empty() ? "the load" : maps.c_str(), frames.over_25, frames.frames,
                frames.worst_ms, Mb(made), Mb(ahead), Mb(drawn), Mb(released), Mb(left), held.c_str(),
                Mb(after_uploads), (e.bind_ns - b.bind_ns) / 1e6, (n.bind_ns - e.bind_ns) / 1e6,
                static_cast<unsigned long long>(ready_made), (n.ready.made_bytes - b.ready.made_bytes) / 1048576.0,
                static_cast<unsigned long long>(ready_taken),
                static_cast<unsigned long long>(n.ready.allocated - b.ready.allocated),
                (n.ready.allocated_ns - b.ready.allocated_ns) / 1e6, pipes.c_str(),
                static_cast<unsigned long long>(n.told - b.told),
                static_cast<unsigned long long>(n.pages_dropped - b.pages_dropped),
                static_cast<unsigned long long>(n.pages_writable - b.pages_writable));
    std::fflush(stdout);
}

} // namespace

void OnFlip() {
    const BbEngine::Loading::Status st = BbEngine::Loading::Current();
    const u64 now = NowNs();
    if (st.up && st.load != g.load) {
        // A new load: one that began within 2 s of the last gets the last one's line now.
        if (!g.reported) {
            Report(now);
        }
        if (!g.freed) {
            ReadySurfaces::FreeUntaken();
            g.freed = true;
        }
        g.load = st.load;
        g.in_load = true;
        g.made = false;
        Vulkan::pipeline_compile_stats.longest_wait_mark_ns.store(0);
        g.begin = Take(true);
    }
    if (g.in_load && st.up && !g.made && st.frames >= kMakeFrame) {
        g.made = true;
        ReadySurfaces::Make();
    }
    if (g.in_load && !st.up) {
        g.in_load = false;
        g.end_ns = now;
        g.end = Take(false);
        g.reported = g.freed = false;
        BbEngine::Frames::Open(BbEngine::Frames::kLoad);
    }
    if (!g.reported && now - g.end_ns >= kAfterNs) {
        Report(now);
    }
    if (!g.freed && now - g.end_ns >= kFreeNs) {
        ReadySurfaces::FreeUntaken();
        g.freed = true;
    }
}

} // namespace VideoCore::LoadWork
