// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.4 beta 14: where the game's copy shader (cs fefebf9f, vk_shader_hle.cpp)
// took the bytes it wrote, for object motion. Small skeletons whose bone palette the GPU wrote are
// stored every frame (their CPU copy is stale, so the palette cannot be compared with the last
// frame's); facing the lamps of the dense area that was 360 draws and 3.7 M vertices a frame. When
// the copy shader wrote the palette, its source is the game's memory, current on the CPU: the
// palette can be compared through it (BB_MOTION_GPU_SMALL=source).
// 0.4.4 beta 21: fefebf9f is the program hash the HLE compares (COPY_SHADER_HASH; this said
// fefebf9e). BB_SHADER_DISASM and the GPU profile's "dispatch cs" labels print hashes whole (the
// profile names this shader "copy shader HLE" instead, as it is not run). The stage B
// statistics' "dispatches by shader" list prints it as fefebf9e: that table marks a used slot by
// setting the key's bit 0 (BbSections::ByKey) and prints keys with it cleared, so every odd hash
// shows there one lower (a040875f as a040875e, a954e79d as a954e79c).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Vulkan::Motion {

/// BB_MOTION_GPU_SMALL: what is done with a small skeleton whose bone palette the GPU wrote.
/// version (default since 0.4.4 beta 16): its palette is compared with the last frame's by the
/// number of its newest GPU write (VideoCore::GpuWriteStamps), stored as before where unknown;
/// store: its positions are stored every frame, as since Lantern 0.9.38; source: compared through
/// the copy shader's source where that wrote it (beta 14); camera: never stored, camera vectors
/// only (to measure the cost: moving props then smear under an upscaler).
enum class GpuSmall { Version, Store, Source, Camera };
inline GpuSmall GpuSmallMode() {
    static const GpuSmall mode = [] {
        const char* env = std::getenv("BB_MOTION_GPU_SMALL");
        if (env && std::strcmp(env, "store") == 0) {
            return GpuSmall::Store;
        }
        if (env && std::strcmp(env, "source") == 0) {
            return GpuSmall::Source;
        }
        if (env && std::strcmp(env, "camera") == 0) {
            return GpuSmall::Camera;
        }
        return GpuSmall::Version;
    }();
    return mode;
}
inline const char* GpuSmallModeName() {
    switch (GpuSmallMode()) {
    case GpuSmall::Store:
        return "store";
    case GpuSmall::Source:
        return "source";
    case GpuSmall::Camera:
        return "camera";
    default:
        return "version";
    }
}

/// The copies of this frame and the last: guest destination, source, size, found by the 4 KiB
/// pages of their destination. The copy shader and the draws that read its output run in stream
/// order on the draw recording thread; the lock is for the GPU command thread after a drain.
class CopySources {
public:
    static CopySources& Get() {
        static CopySources instance;
        return instance;
    }

    /// Whether copies are recorded: object motion on, in the source mode (0.4.4 beta 21: before,
    /// with frame statistics in any mode too).
    void SetWanted(bool on) {
        wanted = on;
    }
    [[nodiscard]] bool Wanted() const {
        return wanted;
    }

    /// One copy shader dispatch's ranges (vk::BufferCopy: offsets from its source and destination
    /// bases).
    template <class Copies>
    void Record(uint64_t src_base, uint64_t dst_base, const Copies& copies) {
        std::scoped_lock lock{mutex};
        auto& frame = frames[current];
        for (const auto& copy : copies) {
            const uint64_t dst = dst_base + copy.dstOffset;
            const uint64_t size = copy.size;
            if (size == 0 || size > MaxCopy) {
                continue; // palettes are up to 640 bytes; a larger copy is something else
            }
            if (frame.records.size() >= MaxRecords || frame.pages_used >= TableSize / 4) {
                return; // no frame start for a while (object motion off): stop, not grow
            }
            const uint32_t record = uint32_t(frame.records.size());
            frame.records.push_back({dst, src_base + copy.srcOffset, uint32_t(size)});
            for (uint64_t page = dst >> PageShift; page <= (dst + size - 1) >> PageShift; ++page) {
                Slot& slot = frame.Find(page);
                const uint32_t link = uint32_t(frame.links.size());
                const bool used = slot.stamp == frame.stamp;
                frame.links.push_back({record, used ? slot.head : NoLink});
                frame.pages_used += used ? 0 : 1;
                slot = {page, link, frame.stamp};
            }
        }
    }

    /// The source of `size` bytes at `dst`, all written by one copy of this frame or the last (the
    /// newest one), or 0.
    [[nodiscard]] uint64_t Source(uint64_t dst, uint64_t size) {
        std::scoped_lock lock{mutex};
        for (const uint32_t which : {current, current ^ 1u}) {
            auto& frame = frames[which];
            const Slot& slot = frame.Find(dst >> PageShift);
            if (slot.stamp != frame.stamp) {
                continue;
            }
            for (uint32_t link = slot.head; link != NoLink; link = frame.links[link].next) {
                const auto& r = frame.records[frame.links[link].record];
                if (r.dst <= dst && dst + size <= r.dst + r.size) {
                    return r.src + (dst - r.dst);
                }
            }
        }
        return 0;
    }

    /// Frame start (ObjectMotion::OnFrameStart): this frame's copies become the last frame's.
    void NextFrame() {
        std::scoped_lock lock{mutex};
        current ^= 1u;
        frames[current].Reset();
    }

private:
    static constexpr uint32_t NoLink = ~0u;
    static constexpr uint64_t PageShift = 12;
    static constexpr uint64_t MaxCopy = 64 * 1024;
    static constexpr size_t MaxRecords = 1u << 15;
    static constexpr size_t TableSize = 1u << 16; // pages a frame stay well under a quarter
    struct Copy {
        uint64_t dst, src;
        uint32_t size;
    };
    struct Link {
        uint32_t record, next;
    };
    struct Slot {
        uint64_t page = 0;
        uint32_t head = NoLink, stamp = 0;
    };
    struct Frame {
        std::vector<Copy> records;
        std::vector<Link> links;
        std::vector<Slot> table = std::vector<Slot>(TableSize);
        uint32_t stamp = 1; ///< a slot of another stamp is empty (no clearing every frame)
        size_t pages_used = 0; ///< kept under a quarter of the table: probes stay short

        /// The page's slot, or the empty one where it would go (open addressing).
        Slot& Find(uint64_t page) {
            size_t at = size_t((page * 0x9E3779B97F4A7C15ull) >> 48) & (TableSize - 1);
            while (table[at].stamp == stamp && table[at].page != page) {
                at = (at + 1) & (TableSize - 1);
            }
            return table[at];
        }
        void Reset() {
            records.clear();
            links.clear();
            pages_used = 0;
            if (++stamp == 0) {
                table.assign(TableSize, Slot{});
                stamp = 1;
            }
        }
    };
    std::mutex mutex;
    Frame frames[2];
    uint32_t current = 0;
    bool wanted = false;
};

/// Frame statistics on those draws (with frame statistics on, in any mode): how many; how many had
/// every such palette's newest GPU write known, and of those how many found it as in the last
/// frame (what the version mode leaves to camera vectors, its hold aside); in the source mode
/// only (the copies are traced there only, 0.4.4 beta 21), how many had every palette from one
/// copy of the copy shader with a source the CPU holds current, and of those how many found the
/// source as in the last frame. Draw recording thread only.
class GpuSmallStats {
public:
    static GpuSmallStats& Get() {
        static GpuSmallStats instance;
        return instance;
    }
    /// A draw's identity (as History::GateKey); its palettes' newest GPU write when known, and
    /// their source hash when traced.
    void Note(uint64_t identity, bool versioned, uint64_t version_hash, bool traced,
              uint64_t source_hash) {
        ++draws;
        // The same mesh drawn again this frame (another lamp): its own entry.
        const uint64_t key = identity ^ (uint64_t(occurrences[identity]++) * 0x9E3779B97F4A7C15ull);
        const auto prev = previous.find(key);
        auto& entry = current[key];
        if (versioned) {
            ++known;
            same_version += prev != previous.end() && prev->second.versioned &&
                            prev->second.version == version_hash;
            entry.versioned = true;
            entry.version = version_hash;
        }
        if (traced) {
            ++from_copy;
            unchanged += prev != previous.end() && prev->second.traced &&
                         prev->second.source == source_hash;
            entry.traced = true;
            entry.source = source_hash;
        }
    }
    void NextFrame() {
        previous.swap(current);
        current.clear();
        occurrences.clear();
        ++frames;
    }
    /// The line for the 600-frame report, then the counts start again.
    void Report() {
        if (frames && draws) {
            // The copy shader's part only where its sources are traced (the source mode): 0
            // elsewhere would read as "none came from it".
            char from_copy_text[128] = "";
            if (GpuSmallMode() == GpuSmall::Source) {
                std::snprintf(from_copy_text, sizeof(from_copy_text),
                              "; every palette from the copy shader %.0f (as in the last frame "
                              "%.0f)",
                              double(from_copy) / frames, double(unchanged) / frames);
            }
            std::printf("Object motion: small skeletons the GPU wrote %.0f draws a frame; newest GPU "
                        "write known %.0f (none since the last frame %.0f)%s "
                        "(BB_MOTION_GPU_SMALL=%s)\n",
                        double(draws) / frames, double(known) / frames,
                        double(same_version) / frames, from_copy_text, GpuSmallModeName());
        }
        draws = known = same_version = from_copy = unchanged = frames = 0;
    }

private:
    struct Entry {
        bool versioned = false, traced = false;
        uint64_t version = 0, source = 0;
    };
    uint64_t draws = 0, known = 0, same_version = 0, from_copy = 0, unchanged = 0, frames = 0;
    std::unordered_map<uint64_t, Entry> current, previous;
    std::unordered_map<uint64_t, uint32_t> occurrences;
};

} // namespace Vulkan::Motion
