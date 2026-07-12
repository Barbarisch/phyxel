#pragma once

#include "core/Types.h"
#include "graphics/ChunkRenderBuffer.h"
#include <array>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_set>
#include <vulkan/vulkan.h>

namespace Phyxel {

// Forward declarations
class Cube;
class Subcube;
class Microcube;

namespace Graphics {

/**
 * ChunkRenderManager handles all rendering concerns for a chunk
 * Responsibilities:
 * - Face generation and culling (cube, subcube, microcube)
 * - Instance data management
 * - Vulkan buffer coordination via ChunkRenderBuffer
 * - Texture and color updates
 */
class ChunkRenderManager {
public:
    // Neighbor lookup function type for cross-chunk culling
    using NeighborLookupFunc = std::function<const Cube*(const glm::ivec3& worldPos)>;
    // Baked light at a cell: skylight + per-channel coloured block light (each 0-15).
    struct BakedLight { uint8_t sky = 0, r = 0, g = 0, b = 0; };
    // Cross-chunk baked-light lookup: fills `out` for the given WORLD cell from a neighbouring
    // chunk's already-baked light; returns false if there's no baked neighbour there.
    using NeighborLightFunc = std::function<bool(const glm::ivec3& worldPos, BakedLight& out)>;

    // --- Smooth-lighting controls (global; require a chunk re-bake to take effect) ---
    // s_smoothLighting OFF → flat per-face light (single cell, all corners equal) → faces greedy-
    // merge freely (fast, looks blocky). ON → per-corner smooth lighting + AO.
    // s_mergeTolerance: when smooth, a face whose 4 corners differ by <= this many light levels
    // (per channel) is snapped to its average and treated as uniform so it can still greedy-merge
    // (recovers most of the perf lost to smoothing; gentle gradients re-merge, imperceptibly).
    static bool s_smoothLighting;
    static int  s_mergeTolerance;
    // Billboarded foliage: when ON, the mesher skips solid faces for "billboarded" leaf subcubes
    // and collects foliage card instances instead (FoliageRenderPipeline draws them). Toggling
    // requires a chunk re-bake. Default ON.
    static bool s_foliageEnabled;
    static void setFoliageEnabled(bool on) { s_foliageEnabled = on; }
    static bool getFoliageEnabled()        { return s_foliageEnabled; }
    // Fine (sub/microcube) greedy-merge toggle. OFF (default) = the per-face path, byte-identical
    // to the pre-merge engine. Increment 1 uses it to emit ONE hand-forged merged subcube quad
    // (the encoding spike that proves extents-in-light-word rendering); later increments gate the
    // real fine mesher on it for live A/B. See docs/BinaryGreedyMeshingPlan.md.
    static bool s_fineGreedyMerge;
    static void setFineGreedyMerge(bool on) { s_fineGreedyMerge = on; }
    static bool getFineGreedyMerge()        { return s_fineGreedyMerge; }
    static void setSmoothLighting(bool on) { s_smoothLighting = on; }
    static void setMergeTolerance(int t)   { s_mergeTolerance = t < 0 ? 0 : t; }
    static bool getSmoothLighting()        { return s_smoothLighting; }
    static int  getMergeTolerance()        { return s_mergeTolerance; }

    // --- T0: mesh-cost instrumentation (docs/OffThreadMeshingPlan.md) ---
    // Process-wide wall-clock cost of rebuildAllFaces, the per-chunk mesh op. Aggregated with
    // atomics so the numbers stay correct if meshing ever runs on a worker. Read via the
    // get_render_stats MCP tool. T0 baseline (docs/evidence/offthread_baseline.txt): 2-4 ms typical,
    // 13 ms worst on the densest Mountains chunk — NOT the ~40-50 ms the off-thread plan assumed;
    // the real edit/stream hitch is GPU buffer (re)allocation, not this mesh.
    struct MeshTimingStats {
        uint64_t count  = 0;   // rebuildAllFaces calls measured since last reset
        double   lastMs = 0.0; // most recent call
        double   maxMs  = 0.0; // slowest call seen
        double   avgMs  = 0.0; // running mean over `count`
    };
    static MeshTimingStats getMeshTimingStats();
    static void            resetMeshTimingStats();

    ChunkRenderManager();
    ~ChunkRenderManager();

    // Non-copyable
    ChunkRenderManager(const ChunkRenderManager&) = delete;
    ChunkRenderManager& operator=(const ChunkRenderManager&) = delete;

    // Movable
    ChunkRenderManager(ChunkRenderManager&& other) noexcept;
    ChunkRenderManager& operator=(ChunkRenderManager&& other) noexcept;

    // Initialization
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);

    // Face rebuilding - split into focused methods
    // columnOpenMask (optional): a 32x32 byte grid (index x*32+z, 1 = open to sky) precomputed by
    // the caller from the chunks ABOVE, so the skylight bake can seed sky columns without the slow
    // per-cell roof probe. nullptr → fall back to the in-bake getNeighborCube probe.
    void rebuildAllFaces(
        const std::vector<std::unique_ptr<Cube>>& cubes,
        const std::vector<std::unique_ptr<Subcube>>& subcubes,
        const std::vector<std::unique_ptr<Microcube>>& microcubes,
        const glm::ivec3& worldOrigin,
        const NeighborLookupFunc& getNeighborCube = nullptr,
        const NeighborLightFunc& getNeighborLight = nullptr,
        const std::vector<uint8_t>* columnOpenMask = nullptr
    );

    void rebuildCubeFaces(
        const std::vector<std::unique_ptr<Cube>>& cubes,
        const std::vector<std::unique_ptr<Subcube>>& subcubes,      // for emissive/flaming block-light seeding
        const std::vector<std::unique_ptr<Microcube>>& microcubes,  // (sub/micro sources seed at their parent cube cell)
        const glm::ivec3& worldOrigin,
        const NeighborLookupFunc& getNeighborCube = nullptr,
        const std::vector<uint8_t>* columnOpenMask = nullptr
    );

    // True if this chunk's boundary light (what neighbours sample) changed on the last rebuild —
    // the caller re-meshes neighbours so cross-chunk light bleed converges.
    bool lightBordersChanged() const { return m_lightBordersChanged; }
    // Read this chunk's baked light at a local cell (for neighbours). Returns false if not baked.
    bool bakedLightAt(int x, int y, int z, BakedLight& out) const;

    // World positions of this chunk's state=flaming leaf voxels, collected on the
    // last rebuild. The fire VFX manager reads these to spawn a flame tongue per
    // ember (see Graphics::FireEmitterManager). Empty for chunks with no fire.
    const std::vector<glm::vec3>& getFlamingVoxels() const { return m_flamingVoxels; }

    void rebuildSubcubeFaces(
        const std::vector<std::unique_ptr<Subcube>>& subcubes,
        const glm::ivec3& worldOrigin
    );

    // Increment 3: greedy-merged subcube path (within-cube same-appearance rectangles), selected by
    // s_fineGreedyMerge. Same algorithm as rebuildMicrocubeFacesMerged at the 3x3 subcube grid.
    void rebuildSubcubeFacesMerged(
        const std::vector<std::unique_ptr<Subcube>>& subcubes,
        const glm::ivec3& worldOrigin
    );

    void rebuildMicrocubeFaces(
        const std::vector<std::unique_ptr<Microcube>>& microcubes,
        const glm::ivec3& worldOrigin
    );

    // Increment 2: greedy-merged microcube path (within-cube same-appearance rectangles), selected
    // by s_fineGreedyMerge. See docs/BinaryGreedyMeshingPlan.md.
    void rebuildMicrocubeFacesMerged(
        const std::vector<std::unique_ptr<Microcube>>& microcubes,
        const glm::ivec3& worldOrigin
    );

    // Vulkan buffer management
    void createVulkanBuffer();
    void updateVulkanBuffer();
    void cleanupVulkanResources();
    void ensureBufferCapacity(size_t requiredInstances);

    // Partial updates for hover effects
    void updateSingleCubeTexture(
        const glm::ivec3& localPos,
        uint16_t textureIndex,
        const std::vector<std::unique_ptr<Cube>>& cubes
    );

    void updateSingleSubcubeTexture(
        const glm::ivec3& parentLocalPos,
        const glm::ivec3& subcubePos,
        uint16_t textureIndex,
        const std::vector<std::unique_ptr<Subcube>>& subcubes,
        const glm::ivec3& worldOrigin
    );

    void updateSingleCubeColor(
        const glm::ivec3& localPos,
        const glm::vec3& newColor,
        const std::vector<std::unique_ptr<Cube>>& cubes
    );

    void updateSingleSubcubeColor(
        const glm::ivec3& localPos,
        const glm::ivec3& subcubePos,
        const glm::vec3& newColor,
        const std::vector<std::unique_ptr<Subcube>>& subcubes,
        const glm::ivec3& worldOrigin
    );

    // Accessors
    const std::vector<InstanceData>& getFaces() const { return faces; }
    uint32_t getNumInstances() const { return numInstances; }

    /// Face-direction ranges (Phase 3 bucketing): after rebuildAllFaces the instance
    /// buffer is direction-major; [d] = first instance of faceID d (0=+Z 1=-Z 2=+X
    /// 3=-X 4=+Y 5=-Y), [6] = total. Consumers must verify [6] == getNumInstances()
    /// before trusting the ranges (a chunk meshed before this feature reads all-zero).
    const std::array<uint32_t, 7>& getFaceDirRanges() const { return m_dirRangeOffsets; }

    // --- Grass (lightweight blade layer) ---
    // Grass blade instances collected on the last rebuild — one per exposed grass-topped voxel
    // (see rebuildCubeFaces). The GPU grass buffer is created/updated alongside the face buffer.
    const std::vector<GrassInstanceData>& getGrassInstances() const { return m_grassInstances; }
    VkBuffer getGrassBuffer() const { return grassBuffer.getBuffer(); }
    uint32_t getGrassCount() const { return static_cast<uint32_t>(m_grassInstances.size()); }

    // --- Foliage (leaf billboard cards) ---
    // Foliage instances collected on the last rebuild — one per exposed billboarded-leaf subcube.
    // The GPU foliage buffer is created/updated alongside the face buffer (like grass).
    const std::vector<FoliageInstanceData>& getFoliageInstances() const { return m_foliageInstances; }
    VkBuffer getFoliageBuffer() const { return foliageBuffer.getBuffer(); }
    uint32_t getFoliageCount() const { return static_cast<uint32_t>(m_foliageInstances.size()); }
    bool getNeedsUpdate() const { return needsUpdate; }
    void setNeedsUpdate(bool update) { needsUpdate = update; }

    // Buffer info
    VkBuffer getInstanceBuffer() const { return renderBuffer.getBuffer(); }
    void* getMappedMemory() const { return renderBuffer.getMappedMemory(); }
    size_t getBufferCapacity() const { return renderBuffer.getCapacity(); }
    size_t getMaxInstancesUsed() const { return renderBuffer.getMaxInstancesUsed(); }
    float getBufferUtilization() const {
        return renderBuffer.getCapacity() > 0 
            ? float(faces.size()) / float(renderBuffer.getCapacity()) * 100.0f 
            : 0.0f;
    }

    // Logging
    void logBufferUtilization() const;

private:
    // Helper methods for face visibility checking
    bool isCubeFaceVisible(
        const glm::ivec3& cubePos,
        int faceID,
        const std::vector<std::unique_ptr<Cube>>& cubes,
        const glm::ivec3& worldOrigin,
        const NeighborLookupFunc& getNeighborCube
    ) const;

    // CRITICAL: Assumes cubes vector is indexed by position using formula:
    // index = z + y*32 + x*32*32 (X-major order)
    // This matches Chunk::localToIndex() and enables O(1) lookup.
    // DO NOT use linear search - it causes O(n²) performance!
    const Cube* getCubeAtPosition(
        const glm::ivec3& localPos,
        const std::vector<std::unique_ptr<Cube>>& cubes
    ) const;

    // Baked per-cell skylight for this chunk (32x32x32, value 0-15 per air cell; solid=0).
    // Computed in rebuildCubeFaces from chunk occupancy; consumed by all three face builders
    // to set per-face light. Phase 1: per-chunk only (boundaries fall back to open sky).
    std::vector<uint8_t> m_skyLight;
    // Skylight of the air cell at local (x,y,z); 15 (open sky) if out of chunk bounds.
    uint8_t skyLightAt(int x, int y, int z) const;

    // Baked per-cell COLORED block light (32x32x32, each channel 0-15): flood-filled from emissive
    // voxels in their material colour (physics.colorTint), so a torch glows warm, a crystal blue,
    // etc. Three channels propagate independently (correct colour blending where lights overlap).
    std::vector<uint8_t> m_blockR, m_blockG, m_blockB;
    // Block light colour of the air cell at local (x,y,z); 0 if out of chunk bounds (no source).
    void blockLightAt(int x, int y, int z, uint8_t& r, uint8_t& g, uint8_t& b) const;

    // Reused scratch buffers for rebuildCubeFaces occupancy (32x32x32). Promoted from per-call
    // locals to members + .assign() (like m_skyLight) to avoid a heap alloc/free of ~190KB on
    // every chunk rebuild — meaningful on streaming/edit-heavy scenes.
    std::vector<uint8_t> m_solidVis;    // 1 = a visible cube occupies the cell
    std::vector<int>     m_cellMat;     // index into the per-rebuild matFaces table (-1 = none)
    std::vector<uint8_t> m_cellDamage;  // quantized 0-15 voxel damage (roughness driver)

    // --- Sub/microcube hidden-face culling (Phase 1) ---
    // Occupancy of the chunk's leaf subcubes/microcubes, rebuilt once per rebuildAllFaces straight
    // from the voxel hierarchy (the source of truth) and consumed by the sub/micro face builders to
    // skip faces whose neighbour cell is fully solid. Cube-level occupancy reuses m_solidVis (built
    // first in rebuildCubeFaces). Keys: subKey = cubeIdx*27 + subLocalIdx (cubeIdx = z+y*32+x*1024,
    // local index = z+y*3+x*9); microKey = subKey*27 + microLocalIdx.
    std::unordered_set<uint32_t> m_subOcc;
    std::unordered_set<uint32_t> m_microOcc;
    void buildSubMicroOccupancy(
        const std::vector<std::unique_ptr<Subcube>>& subcubes,
        const std::vector<std::unique_ptr<Microcube>>& microcubes,
        const glm::ivec3& worldOrigin);
    // Is the cell at the given resolution fully solid (so a face against it is hidden)? Coarser
    // fills roll up: a micro/sub cell is solid if its parent sub/cube is solid. Out-of-chunk → false.
    bool cubeCellSolid(int lx, int ly, int lz) const;
    bool subCellSolid(int lx, int ly, int lz, int sx, int sy, int sz) const;
    bool microCellSolid(int lx, int ly, int lz, int sx, int sy, int sz, int mx, int my, int mz) const;

    // Cross-chunk light bleed state. During a rebuild, these hold the neighbour-light lookup and
    // this chunk's world origin so skyLightAt/blockLightAt can read across chunk boundaries.
    NeighborLightFunc m_neighborLight;
    glm::ivec3 m_lightWorldOrigin{0};
    // Previous boundary light (6 faces, packed sky|block<<4) to detect changes that require
    // neighbour re-meshing; m_lightBordersChanged is set when it differs after a rebuild.
    std::vector<uint8_t> m_prevBorderLight;
    bool m_lightBordersChanged = false;

    // World positions of state=flaming leaf voxels found on the last rebuild (fire VFX seeds).
    std::vector<glm::vec3> m_flamingVoxels;

    // Member variables
    std::vector<InstanceData> faces;           // Visible faces (CPU pre-filtered)
    uint32_t numInstances;                     // Count of visible faces
    bool needsUpdate;                          // Flag for buffer updates

    // Face-direction bucketing (Phase 3): direction-major reorder of `faces` +
    // prefix offsets, rebuilt at the end of every rebuildAllFaces.
    std::array<uint32_t, 7> m_dirRangeOffsets{};
    std::vector<InstanceData> m_dirScratch;    // reused scatter buffer
    void reorderFacesByDirection();

    // Grass blade instances (one per exposed grass-topped voxel), rebuilt with the faces and
    // uploaded to grassBuffer. Empty for chunks with no grass surface.
    std::vector<GrassInstanceData> m_grassInstances;
    // Foliage card instances (one per exposed billboarded-leaf subcube), uploaded to foliageBuffer.
    std::vector<FoliageInstanceData> m_foliageInstances;

    ChunkRenderBuffer renderBuffer;            // Vulkan buffer management (cube/sub/micro faces)
    ChunkRenderBuffer grassBuffer;             // Parallel buffer for grass blade instances
    ChunkRenderBuffer foliageBuffer;           // Parallel buffer for foliage leaf-card instances

    VkDevice device;
    VkPhysicalDevice physicalDevice;
};

} // namespace Graphics
} // namespace Phyxel
