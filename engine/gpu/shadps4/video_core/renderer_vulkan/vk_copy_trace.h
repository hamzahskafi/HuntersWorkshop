// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.5 beta 8: one frame's copy-shader dispatches in their render passes, twice
// a session, with frame statistics on (BB_FRAME_STATS). The copy shader (fefebf9f, the toolkit's)
// costs 0.35-0.45 ms a frame in play: about 64 dispatches, and about 20-25 render passes a frame
// begun again on the same targets after one of them ended the pass. Moving a pass's copies ahead of
// it needs that no earlier draw or dispatch of the pass touches what they copy: the trace says, for
// each pass, how many copies came at its start, inside it (and of those, how many could move to its
// start), and at its end or between passes.
//
// A pass here is what the GPU sees as one: render passes on the same targets, begun again after a
// copy or anything else ended them (Scheduler::BeginRendering). Steps are the draws and dispatches
// recorded in it, with the buffer ranges they bound (Rasterizer::ResetBindings: storage and uniform
// buffers, and a draw's vertex buffers); copies are the copy shader's ranges as recorded
// (ExecuteCopyShaderHLE). Transfers the engine makes on its own (uploads, the stream buffer) are not
// seen. Recorded by one thread at a time.
#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "common/types.h"

namespace Vulkan::CopyTrace {

struct Range {
    u64 buffer; // the VkBuffer handle
    u64 begin, end;
};

struct Access {
    Range range;
    bool write;
    u32 step;
};

struct Copy {
    u32 steps_before;
    std::vector<Range> src, dst;
};

struct State {
    bool active = false;
    u32 traces = 0;
    u32 qualifying_frames = 0;
    u32 copies_this_frame = 0;
    u32 lines = 0;
    // The pass being recorded.
    bool pass = false;
    u32 pass_index = 0, render_passes = 0, steps = 0;
    std::string targets;
    bool wants_targets = false;
    std::vector<Access> accesses;
    std::vector<Copy> copies;
    Copy current{};
    bool copying = false;
    // The frame's totals.
    u32 total = 0, at_start = 0, inside = 0, movable = 0, at_end = 0, outside = 0;
    u32 restarts = 0, later_reads = 0, details = 0;
};

inline State state;
constexpr u32 MAX_LINES = 160;
constexpr u32 MAX_DETAILS = 24;

inline u64 Key(const void* handle) {
    return reinterpret_cast<u64>(handle);
}

inline bool Overlaps(const Range& a, const Range& b) {
    return a.buffer == b.buffer && a.begin < b.end && b.begin < a.end;
}

inline void Print(const std::string& text) {
    if (state.lines < MAX_LINES) {
        ++state.lines;
        std::printf("  %s\n", text.c_str());
    }
}

// The pass ends (another pass begins, or the frame): its copies are sorted and counted.
inline void FinishPass() {
    auto& s = state;
    if (!s.pass) {
        return;
    }
    s.pass = false;
    if (s.copies.empty()) {
        s.accesses.clear();
        return;
    }
    u32 at_start = 0, inside = 0, movable = 0, at_end = 0, later = 0;
    for (const Copy& copy : s.copies) {
        if (copy.steps_before == 0) {
            ++at_start;
            continue;
        }
        if (copy.steps_before >= s.steps) {
            ++at_end;
            continue;
        }
        ++inside;
        const char* why = nullptr;
        const Range* where = nullptr;
        u32 later_steps = 0;
        u32 last_step = ~0u;
        for (const Access& access : s.accesses) {
            if (access.step < copy.steps_before) {
                if (why) {
                    continue;
                }
                for (const Range& dst : copy.dst) {
                    if (Overlaps(dst, access.range)) {
                        why = access.write ? "an earlier step wrote its destination"
                                           : "an earlier step read its destination";
                        where = &access.range;
                        break;
                    }
                }
                if (!why && access.write) {
                    for (const Range& src : copy.src) {
                        if (Overlaps(src, access.range)) {
                            why = "an earlier step wrote its source";
                            where = &access.range;
                            break;
                        }
                    }
                }
            } else if (!access.write && access.step != last_step) {
                for (const Range& dst : copy.dst) {
                    if (Overlaps(dst, access.range)) {
                        ++later_steps;
                        last_step = access.step;
                        break;
                    }
                }
            }
        }
        later += later_steps;
        if (!why) {
            ++movable;
        } else if (s.details < MAX_DETAILS) {
            ++s.details;
            char text[256];
            std::snprintf(text, sizeof(text),
                          "  pass %u, the copy after step %u of %u: %s (buffer %llx, bytes "
                          "%llu-%llu)",
                          s.pass_index, copy.steps_before, s.steps, why,
                          static_cast<unsigned long long>(where->buffer),
                          static_cast<unsigned long long>(where->begin),
                          static_cast<unsigned long long>(where->end));
            Print(text);
        }
    }
    char text[512];
    std::snprintf(text, sizeof(text),
                  "pass %u (%s; %u steps in %u render passes): %zu copies: %u at its start, %u "
                  "inside (%u could move to its start), %u at its end; %u later steps read what "
                  "they copied",
                  s.pass_index, s.targets.empty() ? "an engine pass" : s.targets.c_str(),
                  s.steps, s.render_passes, s.copies.size(), at_start, inside, movable, at_end,
                  later);
    Print(text);
    s.at_start += at_start;
    s.inside += inside;
    s.movable += movable;
    s.at_end += at_end;
    s.later_reads += later;
    s.copies.clear();
    s.accesses.clear();
}

// Scheduler::BeginRendering, a render pass beginning: on the targets of the one before (begun
// again) or on others (a new pass).
inline void PassBegin(bool same_targets) {
    auto& s = state;
    if (!s.active) {
        return;
    }
    if (same_targets && s.pass) {
        ++s.render_passes;
        ++s.restarts;
        return;
    }
    FinishPass();
    s.pass = true;
    ++s.pass_index;
    s.render_passes = 1;
    s.steps = 0;
    s.targets.clear();
    s.wants_targets = true;
}

// A draw or dispatch recorded, with the buffer ranges it bound.
inline void StepBegin() {
    if (state.active && state.pass) {
        ++state.steps;
    }
}
inline void StepAccess(u64 buffer, u64 offset, u64 size, bool write) {
    auto& s = state;
    if (s.active && s.pass && size) {
        s.accesses.push_back({{buffer, offset, offset + size}, write, s.steps - 1});
    }
}

// The copy shader: a dispatch, its batches' ranges.
inline void CopyBegin() {
    ++state.copies_this_frame;
    if (state.active) {
        state.current = {state.pass ? state.steps : 0u, {}, {}};
        state.copying = true;
    }
}
inline void CopyRange(u64 src, u64 src_offset, u64 dst, u64 dst_offset, u64 size) {
    auto& s = state;
    if (s.copying && size) {
        s.current.src.push_back({src, src_offset, src_offset + size});
        s.current.dst.push_back({dst, dst_offset, dst_offset + size});
    }
}
inline void CopyEnd() {
    auto& s = state;
    if (!s.copying) {
        return;
    }
    s.copying = false;
    ++s.total;
    if (s.pass) {
        s.copies.push_back(std::move(s.current));
    } else {
        ++s.outside;
    }
}

// The frame's start: the traced frame ends; the 600th and the 6,000th frame with at least 40
// copy-shader dispatches before it are traced.
inline void FrameStart(bool statistics) {
    auto& s = state;
    if (s.active) {
        FinishPass();
        std::printf("Copy trace %u: end of the frame: %u copy-shader dispatches: %u at a pass's "
                    "start, %u inside a pass (%u could move to its start), %u at a pass's end, "
                    "%u between passes; render passes begun again on the same targets %u; later "
                    "steps reading what was copied %u\n",
                    s.traces + 1, s.total, s.at_start, s.inside, s.movable, s.at_end, s.outside,
                    s.restarts, s.later_reads);
        s.active = false;
        ++s.traces;
    }
    if (statistics && s.traces < 2 && s.copies_this_frame >= 40) {
        ++s.qualifying_frames;
        if (s.qualifying_frames == 600 || s.qualifying_frames == 6000) {
            s.active = true;
            s.lines = 0;
            s.details = 0;
            s.pass = false;
            s.pass_index = 0;
            s.total = s.at_start = s.inside = s.movable = s.at_end = s.outside = 0;
            s.restarts = s.later_reads = 0;
            std::printf("Copy trace %u (0.4.5 beta 8), one frame: the copy shader's dispatches in "
                        "their passes (a pass: render passes on the same targets, begun again "
                        "after what ended them; steps: its draws and dispatches)\n",
                        s.traces + 1);
        }
    }
    s.copies_this_frame = 0;
}

} // namespace Vulkan::CopyTrace
