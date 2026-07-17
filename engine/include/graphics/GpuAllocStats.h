#pragma once

// ── Chunk GPU allocation counter (docs/LargeWorldScalePlan.md §0, blocker D) ────────────
//
// The engine makes ONE bare vkAllocateMemory per chunk render buffer, and a chunk carries
// THREE of them (faces + grass + foliage), each with a ~586 KB host-visible MAPPED floor and
// no suballocator behind it. Vulkan caps LIVE allocations at
// VkPhysicalDeviceLimits::maxMemoryAllocationCount (commonly 4096 on AMD/Intel; effectively
// unbounded on some NVIDIA drivers), so ~1365 resident chunks can exhaust a 4096 ceiling.
//
// When that allocation fails, ChunkRenderBuffer THROWS — and the throw is uncaught on the main
// thread (the only catch handlers are on the generation worker) → std::terminate → the process
// dies with NO log line. That is exactly the Middle-earth 1:1 benchmark crash signature.
//
// This counter exists to ATTRIBUTE that crash: an allocation-count ceiling (blocker D) vs host
// memory exhaustion (blocker C). It is DIAGNOSTIC ONLY — it changes no allocation behaviour.
// Function-local statics keep it header-safe with no ODR/init-order surprises.

#include <atomic>
#include <cstdint>

namespace Phyxel {
namespace Graphics {
namespace gpualloc {

inline std::atomic<uint32_t>& live() { static std::atomic<uint32_t> v{0}; return v; }
inline std::atomic<uint32_t>& peak() { static std::atomic<uint32_t> v{0}; return v; }

// Record a successful vkAllocateMemory for a chunk render buffer. Returns the new live count.
inline uint32_t note() {
    const uint32_t n = ++live();
    uint32_t p = peak().load(std::memory_order_relaxed);
    while (n > p && !peak().compare_exchange_weak(p, n)) {}
    return n;
}

// Record a vkFreeMemory — from either free path: ChunkRenderBuffer::cleanup() (inline) or the
// DeferredBufferReclaim drain (B1). Both must call this or the count drifts.
inline void release() { --live(); }

} // namespace gpualloc
} // namespace Graphics
} // namespace Phyxel
