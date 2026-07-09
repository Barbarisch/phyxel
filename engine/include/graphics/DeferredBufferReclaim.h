#pragma once

// --- B1: deferred buffer reclaim (docs/ChunkUpdateHitchPlan.md) ---
// ChunkRenderBuffer::reallocateBuffer used to free the old VkBuffer/VkDeviceMemory INLINE, even
// though that buffer may still be bound as a vertex buffer in a frame the GPU has not finished
// (MAX_FRAMES_IN_FLIGHT = 2) — a use-after-free BY INSPECTION. NOTE (measured 2026-07-09, Debug +
// PHYXEL_VALIDATION): 346 inline-free reallocs produced 0 in-use VUIDs, so the UAF is LATENT under
// the current frame pacing (fence waits before the mesh/realloc phase → the buffer is GPU-complete by
// free time). This queue is therefore DEFENSIVE HARDENING (+ future-proofs off-thread meshing, where
// that fence guarantee would not hold): it defers the free by > MAX_FRAMES_IN_FLIGHT rendered frames,
// so by drain time the GPU can no longer reference the buffer.
//
// The streaming eviction path already uses this pattern at whole-Chunk granularity
// (ChunkStreamingManager m_pendingDeletion, one pump >= frames-in-flight). This is the same idea at
// single-buffer granularity, for a chunk that stays resident but outgrew its buffer.
//
// Threading: all Vulkan destroy calls happen on the main thread via tickDeferredBufferReclaim() /
// flushDeferredBufferReclaim(). deferBufferFree() may be called from any thread (guarded) so this
// stays correct if meshing/upload ever moves to a worker.

#include <vulkan/vulkan.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdint>

namespace Phyxel {
namespace Graphics {

namespace detail {
    struct DeferredFree {
        VkDevice       device = VK_NULL_HANDLE;
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void*          mapped = nullptr;      // unmap before free if non-null
        uint64_t       retireTick = 0;        // free once the frame tick reaches this
    };
    inline std::mutex& reclaimMutex() { static std::mutex m; return m; }
    inline std::vector<DeferredFree>& reclaimQueue() { static std::vector<DeferredFree> q; return q; }
    inline std::atomic<uint64_t>& reclaimTick() { static std::atomic<uint64_t> t{0}; return t; }

    inline void destroyNow(const DeferredFree& f) {
        if (f.device == VK_NULL_HANDLE) return;
        if (f.mapped) vkUnmapMemory(f.device, f.memory);
        if (f.buffer != VK_NULL_HANDLE) vkDestroyBuffer(f.device, f.buffer, nullptr);
        if (f.memory != VK_NULL_HANDLE) vkFreeMemory(f.device, f.memory, nullptr);
    }
}

// Retire margin past MAX_FRAMES_IN_FLIGHT (=2). +3 ticks means a buffer freed at tick T is destroyed
// no earlier than tick T+3, i.e. after at least 3 subsequent rendered frames.
inline constexpr uint64_t kReclaimRetireMargin = 3;

// Hand an old buffer+memory to the reclaim queue instead of freeing it inline. Safe from any thread.
inline void deferBufferFree(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, void* mapped) {
    if (device == VK_NULL_HANDLE || (buffer == VK_NULL_HANDLE && memory == VK_NULL_HANDLE)) return;
    detail::DeferredFree f;
    f.device = device; f.buffer = buffer; f.memory = memory; f.mapped = mapped;
    f.retireTick = detail::reclaimTick().load(std::memory_order_relaxed) + kReclaimRetireMargin;
    std::lock_guard<std::mutex> lk(detail::reclaimMutex());
    detail::reclaimQueue().push_back(f);
}

// Call ONCE per rendered frame on the main thread. Advances the tick and frees anything now safe.
inline void tickDeferredBufferReclaim() {
    const uint64_t now = detail::reclaimTick().fetch_add(1, std::memory_order_relaxed) + 1;
    std::vector<detail::DeferredFree> ready;
    {
        std::lock_guard<std::mutex> lk(detail::reclaimMutex());
        auto& q = detail::reclaimQueue();
        for (size_t i = 0; i < q.size();) {
            if (q[i].retireTick <= now) { ready.push_back(q[i]); q[i] = q.back(); q.pop_back(); }
            else ++i;
        }
    }
    for (const auto& f : ready) detail::destroyNow(f);
}

// Free EVERYTHING immediately. Call on shutdown AFTER vkDeviceWaitIdle and BEFORE device destruction
// (the queued buffers are then guaranteed idle). Prevents leaks / validation errors at teardown.
inline void flushDeferredBufferReclaim() {
    std::vector<detail::DeferredFree> all;
    {
        std::lock_guard<std::mutex> lk(detail::reclaimMutex());
        all.swap(detail::reclaimQueue());
    }
    for (const auto& f : all) detail::destroyNow(f);
}

} // namespace Graphics
} // namespace Phyxel
