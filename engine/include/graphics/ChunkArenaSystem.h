#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "graphics/ChunkArenaAllocator.h"

namespace Phyxel {
namespace Graphics {

/**
 * Process-wide owner of the chunk GPU arena (docs/RegionArenaPlan.md, Phase 4.3 A1).
 *
 * Production: one persistently-mapped HOST_VISIBLE|COHERENT VkBuffer per allocator
 * block (created lazily on first arena allocation; freed on shutdown() before device
 * destruction). Tests: initializeForTests() injects a heap-backed mock so
 * ChunkRenderBuffer's arena paths run headless.
 *
 * Main-thread only, like all chunk GPU lifecycle. tick() once per rendered frame
 * (next to tickDeferredBufferReclaim) advances the span retire clock.
 */
class ChunkArenaSystem {
public:
    static ChunkArenaSystem& instance();

    /// Production init (idempotent). Called lazily by ChunkRenderBuffer on the first
    /// arena-mode allocation once real device handles exist.
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);

    /// Test init: heap-backed blocks, no Vulkan. Replaces any previous state.
    void initializeForTests(size_t blockSize = 1 << 20);

    /// Free every block (production: unmap/destroy/free). Safe to call repeatedly.
    /// MUST run before the VkDevice is destroyed.
    void shutdown();

    bool initialized() const { return m_allocator != nullptr; }

    /// Per-frame retire clock (span reuse margin + empty-block release).
    void tick();

    ChunkArenaAllocator* allocator() { return m_allocator.get(); }

    /// VkBuffer backing an allocator block (VK_NULL_HANDLE in test mode / dead block).
    VkBuffer blockBuffer(uint32_t blockId) const;

    /// Persistently mapped base pointer of a block (heap pointer in test mode).
    void* blockMapped(uint32_t blockId) const;

    /// Region key for a chunk world origin: chunk coord >> (3,2,3) — 8x4x8 chunks
    /// per region (Sodium's shape), packed into 21-bit signed fields.
    static uint64_t regionKeyForChunkOrigin(const glm::ivec3& worldOrigin);

private:
    ChunkArenaSystem() = default;

    std::unique_ptr<ChunkArenaAllocator> m_allocator;
    std::unique_ptr<ArenaBlockBackend> m_backend;
    VkDevice m_device = VK_NULL_HANDLE;
};

} // namespace Graphics
} // namespace Phyxel
