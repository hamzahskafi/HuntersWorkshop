// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vulkan::Motion {

struct VertexRange {
    uint32_t first{}, count{};
    bool operator==(const VertexRange&) const = default;
};

// Reserve only the vertices referenced by this draw, not the whole shared vertex buffer.
// Primitive restart markers are not vertex shader invocations. Reject signed overflow.
template <class Index, size_t Extent>
VertexRange IndexedRange(std::span<const Index, Extent> indices, int32_t base_vertex, bool restart) {
    uint32_t lo = UINT32_MAX, hi = 0;
    for (const auto index : indices) {
        if (restart && index == std::numeric_limits<Index>::max()) {
            continue;
        }
        lo = std::min(lo, uint32_t(index));
        hi = std::max(hi, uint32_t(index));
    }
    if (lo > hi || int64_t(lo) + base_vertex < 0 ||
        int64_t(hi) + base_vertex > INT32_MAX) {
        return {};
    }
    return {uint32_t(int64_t(lo) + base_vertex), hi - lo + 1};
}

// Vertex shader constant buffers (stride 16) of Bloodborne's G-buffer shaders: the 864-byte
// scene constants (camera), 416-byte model constants and palettes of 3x4 bone matrices
// (48 bytes per bone). Characters have 656+ bytes; weapons and props 2-8 bones (96-384).
enum class BufferRole { Other, Skeleton, SmallSkeleton };
inline BufferRole ClassifyBuffer(uint32_t size) {
    if (size == 864 || size > 16384) {
        return BufferRole::Other;
    }
    if (size >= 640) {
        return BufferRole::Skeleton;
    }
    return size >= 96 && size % 48 == 0 ? BufferRole::SmallSkeleton : BufferRole::Other;
}

// Scanning and hashing the index list of every stored draw each frame took ~10% of the GPU
// thread. Static meshes keep their index buffers, so a result is reused and fully verified
// again every `Revalidate` frames. A stale result only costs camera-quality vectors: the
// shader bounds every history access by the stored range.
class IndexRangeCache {
public:
    static constexpr uint64_t Revalidate = 32, Unused = 600;
    struct Key {
        uint64_t address{};
        uint32_t count{}, index_size{};
        int32_t base_vertex{};
        bool restart{};
        bool operator==(const Key&) const = default;
    };
    struct Result {
        VertexRange range;
        uint64_t topology{};
    };
    struct Stats {
        uint64_t hits{}, scans{}, stale{};
    } stats;

    template <class Scan>
    Result Get(const Key& key, uint64_t frame, Scan&& scan) {
        auto& entry = entries[key];
        entry.last_use = frame;
        if (entry.valid && frame - entry.verified < Revalidate) {
            ++stats.hits;
            return entry.result;
        }
        ++stats.scans;
        const Result fresh = scan();
        if (entry.valid && (fresh.topology != entry.result.topology ||
                            !(fresh.range == entry.result.range))) {
            ++stats.stale; // the cached result was wrong for up to Revalidate frames
        }
        entry.result = fresh;
        entry.verified = frame;
        entry.valid = true;
        return entry.result;
    }
    void Trim(uint64_t frame) {
        std::erase_if(entries, [frame](const auto& e) { return frame - e.second.last_use > Unused; });
    }
    size_t Size() const { return entries.size(); }

private:
    struct Entry {
        Result result;
        uint64_t verified{}, last_use{};
        bool valid{};
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            uint64_t h = k.address * 0x9e3779b97f4a7c15ULL;
            h ^= (uint64_t(k.count) << 32 | uint32_t(k.base_vertex)) + (h << 6) + (h >> 2);
            h ^= uint64_t(k.index_size << 1 | uint32_t(k.restart)) + (h << 6) + (h >> 2);
            return size_t(h);
        }
    };
    std::unordered_map<Key, Entry, KeyHash> entries;
};

// Open-addressing map for the per-frame tables of History: entries are keyed per draw and
// the tables are cleared every frame. std::unordered_map allocated a node per insert (four
// per draw) and freed them all at the frame end; here clear() only starts a new generation.
template <class Key, class Value, class Hash>
class FlatMap {
public:
    FlatMap() : slots(64) {}

    Value& operator[](const Key& key) {
        return Insert(key).first->value;
    }
    /// Inserts `value` when `key` is absent (like std::unordered_map::emplace).
    void emplace(const Key& key, const Value& value) {
        const auto [slot, inserted] = Insert(key);
        if (inserted) {
            slot->value = value;
        }
    }
    const Value* find(const Key& key) const {
        const size_t mask = slots.size() - 1;
        for (size_t i = Start(key) & mask;; i = (i + 1) & mask) {
            const Slot& slot = slots[i];
            if (slot.generation != generation) {
                return nullptr;
            }
            if (slot.key == key) {
                return &slot.value;
            }
        }
    }
    void clear() {
        size = 0;
        if (++generation == 0) { // wrapped: stale slots could look current
            for (auto& slot : slots) {
                slot.generation = 0;
            }
            generation = 1;
        }
    }
    void swap(FlatMap& other) noexcept {
        slots.swap(other.slots);
        std::swap(size, other.size);
        std::swap(generation, other.generation);
    }

private:
    struct Slot {
        Key key{};
        Value value{};
        uint32_t generation = 0;
    };
    static size_t Start(const Key& key) {
        const uint64_t h = uint64_t(Hash{}(key)) * 0x9e3779b97f4a7c15ULL;
        return size_t(h ^ (h >> 29));
    }
    std::pair<Slot*, bool> Insert(const Key& key) {
        if ((size + 1) * 2 > slots.size()) {
            Grow();
        }
        const size_t mask = slots.size() - 1;
        for (size_t i = Start(key) & mask;; i = (i + 1) & mask) {
            Slot& slot = slots[i];
            if (slot.generation != generation) {
                slot = {key, Value{}, generation};
                ++size;
                return {&slot, true};
            }
            if (slot.key == key) {
                return {&slot, false};
            }
        }
    }
    void Grow() {
        std::vector<Slot> old(slots.size() * 2);
        old.swap(slots);
        const uint32_t live = generation;
        generation = 1;
        size = 0;
        for (const auto& slot : old) {
            if (slot.generation == live) {
                *Insert(slot.key).first = {slot.key, slot.value, generation};
            }
        }
    }

    std::vector<Slot> slots;
    size_t size = 0;
    uint32_t generation = 1;
};

inline uint32_t JitterPhases(uint32_t render_width, uint32_t output_width) {
    if (!render_width || !output_width) {
        return 8;
    }
    const double ratio = double(output_width) / render_width;
    return uint32_t(std::clamp(std::ceil(8.0 * ratio * ratio), 8.0, 256.0));
}

// Instanced draws (the game's foliage: up to 64 bushes or flowers a draw) also carry a hash of their
// per-instance matrices: culling changes which objects the instances are as the camera turns, often at the
// same instance count, and instance i then read another bush's positions (bbport 0.7, deadinside28: frame
// generation drew bushes where there are none; the upscalers smeared them).
struct Draw {
    uint64_t shader{}, geometry{}, indices{}, topology{}, instance_data{};
    uint32_t index_count{}, instances{}, first_instance{};
    VertexRange vertices;
    bool operator==(const Draw& rhs) const = default;
};

// Where a draw's object is (its first bone's translation, world space). Repeated draws of one mesh are
// matched by their order; a match whose object jumped further than MaxStep since last frame is another
// object (an earlier one was culled) and gets camera vectors instead (bbport 0.7).
struct Anchor {
    float x{}, y{}, z{};
    bool valid{};
    static constexpr float MaxStep = 0.5f;
    bool Near(const Anchor& other) const {
        if (!valid || !other.valid) {
            return true;
        }
        const float dx = x - other.x, dy = y - other.y, dz = z - other.z;
        return dx * dx + dy * dy + dz * dz <= MaxStep * MaxStep;
    }
};

// Why a draw's positions were stored (History::Prepare): a character-size skeleton, a small skeleton the GPU
// wrote (its CPU copy is stale: Lantern 0.9.38's rule), a small skeleton whose bones changed or that is held
// after a change, or every draw (BB_OBJECT_MOTION_ALL). Counted for the frame statistics' motion line.
enum class StoreReason : uint8_t { Character, GpuWritten, Moving, Held, All, Count };
inline const char* StoreReasonName(StoreReason reason) {
    static constexpr const char* names[] = {"characters", "GPU-written small", "moving small", "held small",
                                            "all"};
    return names[size_t(reason)];
}

// CPU bookkeeping shared with the regression test. Draw identity includes topology and
// offsets: a reused index buffer or another submesh must never read uninitialised history.
class History {
public:
    explicit History(uint32_t capacity) : capacity{capacity} {}
    /// The frames whose busiest ask a GPU-written small skeleton leaves room for (half a second at 60 FPS).
    static constexpr size_t ReserveFrames = 32;

    struct Allocation {
        uint32_t store{}, load{}, vertices{}, instances{}, first_vertex{}, first_instance{};
    };
    struct Stats {
        uint64_t draws{}, stored{}, loaded{}, unmatched{}, exhausted{}, invalid{}, still{},
            held{}, jumped{};
        /// By StoreReason: draws prepared, the vertices they asked for (stored or not), capacity skips.
        uint64_t reason_draws[size_t(StoreReason::Count)]{};
        uint64_t reason_vertices[size_t(StoreReason::Count)]{};
        uint64_t reason_skips[size_t(StoreReason::Count)]{};
        uint64_t peak_demand{}; ///< the most a finished frame asked for (Demand)
    } stats;

    /// The vertices a frame can store; before the first frame (the buffer's size is chosen then).
    void SetCapacity(uint32_t vertices) {
        capacity = vertices;
        used = demand = ahead = ahead_peak = 0;
        ahead_frames.fill(0);
    }
    uint32_t Capacity() const { return capacity; }

    // A small skeleton (weapon, prop; most are static world pieces) only needs history
    // while its bone palette changes: with last frame's bones its motion is the camera's.
    // Checked before the index scan of Prepare, with a cheap identity. The first frame
    // of a change stores positions (nothing to load yet), the next one loads them.
    //
    // bbport: a draw whose palette changed keeps its history for `hold` more frames
    // (SetHold, BB_OBJECT_MOTION_HOLD in vk_object_motion.cpp). Without it, bones that move
    // on some frames only never got a vector: a changed frame that follows an unchanged one
    // has nothing to load (that frame stored nothing; it counts as unmatched), and the frame
    // after it is "still" again. The palettes hold world-space bones (the G-buffer vertex
    // shaders multiply by the palette, then by the scene camera; the model constants only
    // give the first bone index), so an attached weapon changes its palette only on the
    // frames the game updates its attachment, which at 120 FPS need not be every rendered
    // frame. Ludwig's Holy Blade on the back showed it: orange (camera motion) in the debug
    // view while the body, a large skeleton stored every frame, was blue, and DLSS dropped
    // the blade's history while the player ran. A held frame stores and loads like a moving
    // one; for an object that did not move its vector equals the camera's, so holding costs
    // only the vertex stores of objects that moved within the last `hold` frames.
    struct GateKey {
        uint64_t shader{}, geometry{}, indices{};
        uint32_t index_count{}, instances{}, first_instance{}, occurrence{};
        bool operator==(const GateKey&) const = default;
    };
    void SetHold(uint32_t frames) { hold = frames; }
    // Another object in the draw's place (its anchor jumped: an earlier one was culled) has no previous
    // frame, its hold included.
    bool Moving(GateKey key, uint64_t palette, Anchor anchor = {}) {
        key.occurrence = 0;
        key.occurrence = gate_occurrences[key]++;
        const auto* prev = gate_previous.find(key);
        if (prev && !prev->anchor.Near(anchor)) {
            ++stats.jumped;
            prev = nullptr;
        }
        GateState state{palette, 0, anchor};
        bool moving = false;
        last_held = false;
        if (prev && prev->palette != palette) {
            moving = true;
            state.hold = hold;
        } else if (prev && prev->hold) {
            moving = true; // unchanged, but changed within the last `hold` frames
            state.hold = prev->hold - 1;
            ++stats.held;
            last_held = true;
        }
        gate_current.emplace(key, state);
        stats.still += moving ? 0 : 1;
        return moving;
    }

    void NextFrame() {
        previous.swap(current);
        current.clear();
        occurrences.clear();
        gate_previous.swap(gate_current);
        gate_current.clear();
        gate_occurrences.clear();
        ahead_frames[frame % ahead_frames.size()] = ahead;
        ahead_peak = *std::max_element(ahead_frames.begin(), ahead_frames.end());
        ahead = 0;
        ++frame;
        used = 0;
        stats.peak_demand = std::max<uint64_t>(stats.peak_demand, demand);
        last_demand = demand;
        demand = 0;
    }

    // `reason`: why the draw is prepared; Moving stands for the small-skeleton gate's answer (moving or held:
    // the last Moving call says which).
    //
    // When room runs short, a small skeleton the GPU wrote gives way: it is stored whether it moves or not (its
    // CPU copy is stale), and most are static props, whose vectors are the camera's anyway. It leaves the room the
    // other draws asked for in the busiest of the last ReserveFrames frames and have not asked for yet this frame,
    // so characters, foliage and small skeletons seen moving are stored first whatever the draw order, a crowd
    // coming into view included. (Yharnam, 0.4.3 beta 2: the GPU-written ones asked for 59% of the vertices; beta
    // 3, with only last frame's ask kept: characters still lost a draw a frame in the two busiest stretches.)
    Allocation Prepare(const Draw& draw, Anchor anchor = {}, StoreReason reason = StoreReason::All) {
        ++stats.draws;
        if (reason == StoreReason::Moving && last_held) {
            reason = StoreReason::Held;
        }
        Key key{draw, 0};
        key.occurrence = occurrences[key]++;
        const uint64_t count = uint64_t(draw.vertices.count) * draw.instances;
        if (!count || uint64_t(draw.vertices.first) + draw.vertices.count - 1 > INT32_MAX ||
            uint64_t(draw.first_instance) + draw.instances > UINT32_MAX) {
            ++stats.invalid;
            return {};
        }
        const auto* prev = previous.find(key);
        Allocation result{0, 0, draw.vertices.count, draw.instances,
                          draw.vertices.first, draw.first_instance};
        ++stats.reason_draws[size_t(reason)];
        stats.reason_vertices[size_t(reason)] += count;
        demand = uint32_t(std::min<uint64_t>(uint64_t(demand) + count, UINT32_MAX));
        const bool gives_way = reason == StoreReason::GpuWritten;
        if (!gives_way) {
            ahead = uint32_t(std::min<uint64_t>(uint64_t(ahead) + count, UINT32_MAX));
        }
        const uint32_t reserve = gives_way && ahead_peak > ahead ? ahead_peak - ahead : 0;
        if (prev && !prev->anchor.Near(anchor)) {
            ++stats.jumped;
            prev = nullptr;
        }
        if (prev) {
            result.load = prev->store;
            ++stats.loaded;
        } else {
            ++stats.unmatched;
        }
        if (count + reserve <= capacity - used) {
            result.store = 1 + uint32_t(frame & 1) * capacity + used;
            used += uint32_t(count);
            current.emplace(key, Stored{result.store, anchor});
            ++stats.stored;
        } else {
            ++stats.exhausted;
            ++stats.reason_skips[size_t(reason)];
        }
        return result;
    }

    uint32_t Used() const { return used; }
    /// The vertices this frame's draws asked for, stored or not (Used is what fit); the last frame's after NextFrame.
    uint32_t Demand() const { return demand; }
    uint32_t LastDemand() const { return last_demand; }

private:
    struct Key {
        Draw draw;
        uint32_t occurrence;
        bool operator==(const Key&) const = default;
    };
    struct Hash {
        size_t operator()(const Key& key) const {
            uint64_t h = 0;
            const auto mix = [&](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
            const auto& d = key.draw;
            mix(d.shader); mix(d.geometry); mix(d.indices); mix(d.topology); mix(d.instance_data);
            mix(d.index_count); mix(d.instances); mix(d.first_instance);
            mix(d.vertices.first); mix(d.vertices.count); mix(key.occurrence);
            return size_t(h);
        }
    };
    uint32_t capacity{}, used{};
    uint32_t demand{}, last_demand{}; ///< vertices asked for this frame and the last (stored or not)
    uint32_t ahead{};                 ///< the same for the draws a GPU-written small skeleton gives way to
    std::array<uint32_t, ReserveFrames> ahead_frames{}; ///< `ahead` of the last ReserveFrames frames
    uint32_t ahead_peak{};            ///< the most of them
    bool last_held{};                 ///< the last Moving call's answer came from a hold
    uint32_t hold{}; ///< frames a small skeleton keeps its history after a change (SetHold)
    uint64_t frame{};
    struct Stored {
        uint32_t store{};
        Anchor anchor;
    };
    FlatMap<Key, Stored, Hash> previous, current;
    FlatMap<Key, uint32_t, Hash> occurrences;
    struct GateHash {
        size_t operator()(const GateKey& key) const {
            uint64_t h = 0;
            const auto mix = [&](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
            mix(key.shader); mix(key.geometry); mix(key.indices); mix(key.index_count);
            mix(key.instances); mix(key.first_instance); mix(key.occurrence);
            return size_t(h);
        }
    };
    struct GateState {
        uint64_t palette{};
        uint32_t hold{}; ///< further frames this draw keeps its history (Moving)
        Anchor anchor;
    };
    FlatMap<GateKey, GateState, GateHash> gate_previous, gate_current;
    FlatMap<GateKey, uint32_t, GateHash> gate_occurrences;
};

} // namespace Vulkan::Motion
