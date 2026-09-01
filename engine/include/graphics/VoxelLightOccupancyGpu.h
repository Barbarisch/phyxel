#pragma once

// ============================================================================
// VoxelLightOccupancyGpu — the packed occupancy, resident on the GPU.
//
// M1b of the lighting rebuild. Deliberately thin: all the addressing lives in
// VoxelLightOccupancy.h as pure functions with unit tests, so this class only owns Vulkan
// resources and copies two word arrays into them. Nothing here decides anything about geometry.
//
// TWO BUFFERS, matching packOccupancyPool():
//   directory — 2048 uint32, one per chunk in the covered world box (8 KB)
//   pool      — chunk blobs back to back
// Both host-coherent and persistently mapped, the same contract GpuParticlePhysics' occupancy
// bitfield uses: the CPU writes straight into GPU-visible memory with no staging copy and no
// barrier, which is what keeps a chunk re-mesh from costing an upload.
//
// REPACK-ON-DIRTY, not incremental. When a chunk changes, the whole pool is rebuilt and re-copied
// at most once per frame. That is O(total resident occupancy) per change and it is the honest
// starting point: correctness first, per the agreed "quality first, optimise after" stance. The
// obvious optimisations — slab allocation, incremental patching, compaction — are deferred until
// the cost is MEASURED rather than guessed at. `stats()` exists to make that measurement possible.
//
// ⚠️ Pool overflow does not truncate silently. Chunks that do not fit are DROPPED and counted in
// stats().droppedChunks, because a half-written blob would read as arbitrary geometry — phantom
// walls carved out of nothing, which is far harder to diagnose than a missing chunk.
// ============================================================================

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "graphics/VoxelLightOccupancy.h"

namespace Phyxel {
namespace Graphics {

class VoxelLightOccupancyGpu {
public:
    struct Stats {
        size_t residentChunks = 0;
        size_t mixedCubes = 0;      ///< cells carrying sub-voxel detail
        size_t poolWords = 0;
        size_t poolCapacityWords = 0;
        size_t droppedChunks = 0;   ///< did not fit; NOT silently truncated
        double lastPackMs = 0.0;
        glm::ivec3 boxMinChunk{0};  ///< min corner of the covered box, in chunk coords
    };

    /// Recentre the covered box on the viewer. Chunk-quantised, so ordinary camera motion does not
    /// repack; crossing a chunk boundary does. Chunks that fall outside the new box are forgotten,
    /// which is correct — they are too far away to occlude anything this system lights.
    void setViewCentre(const glm::vec3& worldPos);
    /// The CURRENT box, which moves the instant setViewCentre is called. `stats().boxMinChunk`
    /// reflects the last FLUSH instead, so the two differ for one frame after a move — read this
    /// one when deciding whether a chunk is addressable now.
    glm::ivec3 boxMinChunk() const { return m_boxMinChunk; }

    /// `poolBytes` caps total blob storage. Default 64 MB: solid terrain costs 8 KB per chunk, so
    /// this holds thousands of ordinary chunks and a large amount of sub-voxel structure.
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkDeviceSize poolBytes = 64ull * 1024 * 1024);
    void cleanup();

    /// Register or replace one chunk's occupancy. Returns false if the chunk lies outside the
    /// covered world box (its lighting then degrades to "no occlusion", never to wrong geometry).
    bool setChunk(const glm::ivec3& chunkWorldOrigin, ChunkLightOccupancy blob);
    void removeChunk(const glm::ivec3& chunkWorldOrigin);
    void clear();

    /// Repack and copy if anything changed. Cheap no-op when clean; call once per frame.
    void flushIfDirty();
    bool dirty() const { return m_dirty; }

    /// Chunks currently held, updated immediately by setChunk/removeChunk/setViewCentre — unlike
    /// stats().residentChunks, which reflects the last flush. Lets residency and eviction be tested
    /// without a Vulkan device (none of those three methods touch Vulkan).
    size_t residentChunks() const { return m_chunks.size(); }

    /// Up to `maxN` WORLD cube positions that carry sub-voxel detail. Exists so a partial-cell
    /// check can be AIMED: sweeping blindly for mixed cells over HTTP costs seconds per cell and
    /// found none, which proved nothing about the case this whole layer exists for.
    std::vector<glm::ivec3> sampleMixedCubes(size_t maxN) const;

    bool         ready()          const { return m_poolMapped != nullptr; }
    VkBuffer     directoryBuffer() const { return m_dirBuffer; }
    VkBuffer     poolBuffer()      const { return m_poolBuffer; }
    VkDeviceSize directoryBytes()  const {
        return static_cast<VkDeviceSize>(PackedOccupancyPool::kDirEntries) * sizeof(uint32_t);
    }
    VkDeviceSize poolBytes()       const { return m_poolBytes; }

    Stats stats() const { return m_stats; }

    /// CPU-side query against the LAST FLUSHED pool, using the same addressing the shader uses.
    /// Exists so a test or a debug endpoint can ask what the GPU can currently see without a
    /// readback — the alternative is inferring it from a screenshot, which has burned this work
    /// repeatedly.
    bool solidAtMicro(const glm::ivec3& worldMicro) const {
        return packedPoolSolidAt(m_packed, worldMicro);
    }

    /// M3 sky access against the last flushed pool — the CPU mirror of phxSkyVisibility.
    /// `reach` and `rays` are exposed because the BAKE (M3-REDESIGN) has a different cost/quality
    /// point than a one-off probe: it runs per cell at chunk-build time, so ray count and reach
    /// are the two knobs that decide whether streaming hitches.
    float skyVisibility(const glm::vec3& surfaceWorld, const glm::vec3& geomNormal,
                        float reach = 24.0f, int rays = 9) const {
        return packedPoolSkyVisibility(m_packed, surfaceWorld, geomNormal, reach, rays);
    }

    /// Cube-level occupancy against the last flushed pool, for the M3 bake's cell gather (D21).
    /// The gather must see SUB-VOXEL walls; the mesher's own `m_solidVis` is cube-only and reports
    /// a 2-micro wall as empty, which left every generated interior untraced.
    CubeOccupancy cubeOccupancy(const glm::ivec3& worldCube) const {
        return packedPoolCubeOccupancy(m_packed, worldCube);
    }

    /// The M2 visibility march against the last flushed pool — the CPU mirror of the shader's.
    LightVisibility visibility(const glm::vec3& surfaceWorld, const glm::vec3& geomNormal,
                               const glm::vec3& lightWorld) const {
        return packedPoolLightVisibility(m_packed, surfaceWorld, geomNormal, lightWorld);
    }

private:
    VkDevice       m_device     = VK_NULL_HANDLE;
    VkBuffer       m_dirBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory m_dirMemory  = VK_NULL_HANDLE;
    void*          m_dirMapped  = nullptr;
    VkBuffer       m_poolBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_poolMemory = VK_NULL_HANDLE;
    void*          m_poolMapped = nullptr;
    VkDeviceSize   m_poolBytes  = 0;

    /// Keyed by chunk WORLD ORIGIN, not by directory slot: the box moves, so a slot is not a stable
    /// identity. Keying by slot silently remapped every resident chunk the moment the camera
    /// crossed a chunk boundary.
    struct IVec3Hash {
        size_t operator()(const glm::ivec3& v) const noexcept {
            return (static_cast<size_t>(v.x) * 73856093u) ^ (static_cast<size_t>(v.y) * 19349663u)
                 ^ (static_cast<size_t>(v.z) * 83492791u);
        }
    };
    std::unordered_map<glm::ivec3, ChunkLightOccupancy, IVec3Hash> m_chunks;
    glm::ivec3 m_boxMinChunk{-PackedOccupancyPool::kDirChunksX / 2,
                             -PackedOccupancyPool::kDirChunksY / 2,
                             -PackedOccupancyPool::kDirChunksZ / 2};
    PackedOccupancyPool m_packed;
    bool  m_dirty = false;
    Stats m_stats;

    bool createHostBuffer(VkPhysicalDevice phys, VkDeviceSize size, VkBuffer& buf,
                          VkDeviceMemory& mem, void*& mapped, const char* what);
};

}  // namespace Graphics
}  // namespace Phyxel
