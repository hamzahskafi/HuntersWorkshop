// SPDX-License-Identifier: GPL-2.0-or-later
// 0.4.6 beta 21 (docs/gpu-fences.md, the plan): the GPU's progress in one word, the fences written
// as the GPU passes them, and the hand-over to the GPU as a queue.
//
// The recording numbers the end of each of the game's submissions and there has the GPU write that
// number into one word of host memory (VK_AMD_buffer_marker at the bottom of the pipe: after all
// earlier work of the queue, inside or outside a pass, holding up nothing after it). The word then
// reads "the game's submissions up to N are done". A fence carries the number of the game's
// submission it is in, and is written on the CPU once the word has passed it (at the end of each of
// the game's submissions, by the GPU command thread) or once its whole engine submission is done
// (the signal thread, as before), whichever comes first, in order and once each.
//
// The same word says when the GPU has started the last unit it was given: the work waiting for it
// goes then, so it never waits for work that is ready, and while it has more ahead the units collect
// into one submission (with the GPU as the limit, a whole frame).
//
// The parts without Vulkan are in vk_gpu_queue_logic.h (tests/test_gpu_queue.cpp checks them).
#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_gpu_queue_logic.h"

namespace Vulkan {

class Instance;

/// The progress word: one host-visible, coherent word the GPU writes with buffer markers.
class GpuProgress {
public:
    /// False when the device has no VK_AMD_buffer_marker or no host-visible memory for it.
    bool Init(const Instance& instance);
    [[nodiscard]] bool Active() const noexcept {
        return word != nullptr;
    }
    /// Recorded at the bottom of the pipe: written once all earlier work of the queue is done.
    static void Write(vk::CommandBuffer cmdbuf, vk::Buffer buffer, u32 value);
    [[nodiscard]] vk::Buffer Buffer() const noexcept {
        return buffer;
    }
    /// The last mark the GPU wrote (0 before the first).
    [[nodiscard]] u32 Read() const noexcept {
        return *word;
    }

private:
    vk::Buffer buffer{};
    volatile u32* word = nullptr;
};

} // namespace Vulkan
