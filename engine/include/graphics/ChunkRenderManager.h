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
class ChunkVoxelStore;

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
    // Neighbor probe for cross-chunk culling: is there a VISIBLE SOLID cube at this WORLD cell?
    // (4.2b: was `const Cube*(worldPos)` — a bool answer lets the provider read the neighbour
    // chunk's palette store instead of materializing border Cubes. Both consumers only ever
    // asked `nc && nc->isVisible()`.)
    using NeighborLookupFunc = std::function<bool(const glm::ivec3& worldPos)>;
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
    // M3-REDESIGN. The alias must be declared BEFORE the member that uses it (see the accessors
    // further down for what this is and why it is injected).
    using SkyVisibilityFn = std::function<float(const glm::vec3& worldPos, const glm::vec3& normal)>;
    static SkyVisibilityFn s_skyVisibility;


    // Billboarded foliage: when ON, the mesher skips solid faces for "billboarded" leaf subcubes
    // and collects foliage card instances instead (FoliageRenderPipeline draws them). Toggling
    // requires a chunk re-bake. Default ON.
    static bool s_foliageEnabled;
    static void setFoliageEnabled(bool on) { s_foliageEnabled = on; }
    static bool getFoliageEnabled()        { return s_foliageEnabled; }
    /// Fraction of exposed leaf cells that emit a canopy card (1.0 = every one, the old
    /// behaviour). Default 0.5: solid-looking canopies read as opaque blobs and swallow all
    /// light; thinning them lets sky and sun break through, which is what makes a canopy
    /// look like foliage rather than a green mass. Deterministic per world cell, so the
    /// thinning is stable across rebuilds and identical on every client.
    static float s_foliageDensity;
    static void  setFoliageDensity(float d) { s_foliageDensity = d; }
    static float getFoliageDensity()        { return s_foliageDensity; }
    // Fine (sub/microcube) greedy-merge toggle. OFF (default) = the per-face path, byte-identical
    // to the pre-merge engine. Increment 1 uses it to emit ONE hand-forged merged subcube quad
    // (the encoding spike that proves extents-in-light-word rendering); later increments gate the
    // real fine mesher on it for live A/B. See docs/BinaryGreedyMeshingPlan.md.
    // --- FLOOD BYPASS (2026-08-29) -------------------------------------------------------------
    // Turn the Minecraft-style per-cell flood OFF and see whether an artifact survives. If it
    // vanishes the flood owns it; if it persists the flood is innocent and the cause is elsewhere.
    // That is a direct experiment, and it replaces arguing from screenshots about which of three
    // lighting systems produced a given band.
    //
    // Bit 1 = skylight forced UNIFORM 15 (removes the flood's spatial variation without making
    //         the world black — zeroing skylight would gate off the sun and ambient too, which
    //         tells you nothing).
    // Bit 2 = block light forced to ZERO (removes emissive-voxel flood entirely).
    // Requires a re-bake to take effect, like s_smoothLighting; the API triggers one.
    static int s_floodBypass;
    static void setFloodBypass(int mask) { s_floodBypass = mask; }
    static int  getFloodBypass()         { return s_floodBypass; }

    static bool s_fineGreedyMerge;
    static void setFineGreedyMerge(bool on) { s_fineGreedyMerge = on; }
    static bool getFineGreedyMerge()        { return s_fineGreedyMerge; }
    // ---- M3-REDESIGN: BAKED sky visibility ------------------------------------------------
    // Per-fragment sky tracing was correct and unshippable: 24.6 ms/frame on a generated town,
    // 275 -> 35 fps (D1). The cost is per fragment per frame for a quantity that only changes when
    // the WORLD changes, so it moves to chunk-bake time and the shader reads one interpolated
    // value again — which is what the deleted flood's storage was for.
    //
    // The query is INJECTED rather than reached for: the occupancy pool lives in the renderer and
    // spans chunks (a wall in the next chunk must occlude), while this class is the mesher. The
    // callback takes a world position and a normal and returns 0..1 sky access.
    static void setSkyVisibilityFn(SkyVisibilityFn fn) { s_skyVisibility = std::move(fn); }

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
    // voxelStore (4.2b): the chunk's palette store — the authority for static voxels. The hybrid
    // read is per cell: a materialized Cube in `cubes` wins (physics/damage state), else the
    // store answers. nullptr = pure-Cube path (unit tests that hand-build cube vectors).
    void rebuildAllFaces(
        const std::vector<std::unique_ptr<Cube>>& cubes,
        const std::vector<std::unique_ptr<Subcube>>& subcubes,
        const std::vector<std::unique_ptr<Microcube>>& microcubes,
        const glm::ivec3& worldOrigin,
        const NeighborLookupFunc& getNeighborCube = nullptr,
        const NeighborLightFunc& getNeighborLight = nullptr,
        const std::vector<uint8_t>* columnOpenMask = nullptr,
        const ChunkVoxelStore* voxelStore = nullptr
    );

    void rebuildCubeFaces(
        const std::vector<std::unique_ptr<Cube>>& cubes,
        const std::vector<std::unique_ptr<Subcube>>& subcubes,      // for emissive/flaming block-light seeding
        const std::vector<std::unique_ptr<Microcube>>& microcubes,  // (sub/micro sources seed at their parent cube cell)
        const glm::ivec3& worldOrigin,
        const NeighborLookupFunc& getNeighborCube = nullptr,
        const std::vector<uint8_t>* columnOpenMask = nullptr,
        const ChunkVoxelStore* voxelStore = nullptr
    );

    // Phase 4.4: reset render state for a UNIFORM chunk (sealed solid or pure air) WITHOUT
    // running the mesh/bake scans — clears faces and instance counts and RELEASES the per-chunk
    // mesh/light scratch (~364 KB) plus grass/foliage instances. The GPU buffers (if any were
    // created before the chunk sealed) stay allocated but draw nothing (numInstances == 0).
    void clearForUniform();

    // True if this chunk's boundary light (what neighbours sample) changed on the last rebuild —
    // the caller re-meshes neighbours so cross-chunk light bleed converges.
    bool lightBordersChanged() const { return m_lightBordersChanged; }
    // Read this chunk's baked light at a local cell (for neighbours). Returns false if not baked.
    bool bakedLightAt(int x, int y, int z, BakedLight& out) const;

    /// Diagnostic: the per-cell light-opacity AXIS MASK the bake and the dynamic-light volume
    /// both read (bit 0 = blocks along X, 1 = Y, 2 = Z). Returns false if this chunk has no
    /// baked opacity (never meshed, or released as a uniform chunk). Exists so a test can ask
    /// "does the engine think THIS cell is a wall" directly, instead of inferring it from how
    /// light behaved — the two are different questions and conflating them hides bugs.
    bool lightOpaqueAt(int x, int y, int z, uint8_t& mask) const {
        if (m_lightOpaque.empty()) return false;
        if (x < 0 || x >= 32 || y < 0 || y >= 32 || z < 0 || z >= 32) return false;
        mask = m_lightOpaque[static_cast<size_t>(z + y * 32 + x * 1024)];
        return true;
    }

    // World positions of this chunk's state=flaming leaf voxels, collected on the
    // last rebuild. The fire VFX manager reads these to spawn a flame tongue per
    // ember (see Graphics::FireEmitterManager). Empty for chunks with no fire.
    const std::vector<glm::vec3>& getFlamingVoxels() const { return m_flamingVoxels; }

    /// U3.2 — an emissive voxel IS a light. One emitter model: radiance x visibility.
    ///
    /// The deleted flood already enumerated these; it seeded a per-cell BFS with them. This
    /// collects the same voxels and hands them to LightManager as real point lights, so a glow
    /// block occludes correctly (M2's visibility term), obeys inverse-square, and depends on the
    /// receiving normal -- none of which a flood could do.
    struct EmissiveLight {
        glm::vec3 worldPos;   ///< centre of the emitting voxel, in world units
        glm::vec3 color;      ///< hue: material colorTint, or the per-voxel tint when burning
        float     intensity;  ///< 0..1, from the material's emissive strength
        float     radius;     ///< reach, scaled with intensity
    };
    const std::vector<EmissiveLight>& getEmissiveLights() const { return m_emissiveLights; }

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

    // Phase 4.3 (docs/RegionArenaPlan.md A1): stamp the spatial region key onto all
    // three buffers before creation so same-region chunks share arena blocks.
    // Cheap + idempotent; a no-op for buffers already created.
    void setArenaRegionKey(const glm::ivec3& worldOrigin);

    // Phase 4.3 A2: byte offsets to bind each buffer at (span offset in arena mode,
    // 0 in legacy mode — the draw sites pass these unconditionally).
    VkDeviceSize getFaceBindOffset() const { return renderBuffer.getBindOffsetBytes(); }
    VkDeviceSize getGrassBindOffset() const { return grassBuffer.getBindOffsetBytes(); }
    VkDeviceSize getFoliageBindOffset() const { return foliageBuffer.getBindOffsetBytes(); }

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

    /// C4 (docs/ContinuousLodPlan.md) — THE CUT. Replace this chunk's face set with a COARSE
    /// LOD-cell mesh built by LodChunkMesh at `level`. The faces carry scaleLevel 3 + lodLevel,
    /// which the vertex shaders expand to 2^level-cube quads. Call updateVulkanBuffer() after.
    /// Level 0 is NOT this path: the fine mesh includes sub/microcubes and greedy merging, so
    /// returning to full detail means a normal rebuildFaces(), not buildForLevel(0).
    void setFacesFromLod(std::vector<InstanceData>&& lodFaces) {
        faces = std::move(lodFaces);
        // The DRAW COUNT must follow the swap. numInstances is otherwise assigned only at the
        // end of the fine rebuild, so omitting it here left the renderer drawing the FINE mesh's
        // count against the coarse buffer: too few (truncated tail => see-through stripes, worst
        // on flat terrain, which greedy-merges to very few fine faces) or too many (instances
        // read from stale memory past the valid data). Pinned by
        // LodChunkMeshTest.SetLodFacesUpdatesDrawCount.
        numInstances = static_cast<uint32_t>(faces.size());
        needsUpdate = true;
        // The coarse set is a single homogeneous run: face-direction bucketing indexes into
        // faces[] by faceID range, and the LOD set is not sorted that way, so disable the
        // fast path for this chunk by making the ranges degenerate (renderer falls back to a
        // full draw -- see RenderCoordinator's dirRanges check).
        for (auto& r : m_dirRangeOffsets) r = 0;
        m_dirRangeOffsets[6] = static_cast<uint32_t>(faces.size() + 1);   // != numInstances => full draw
    }

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
    // Skylight of the air cell at local (x,y,z); 15 (open sky) if out of chunk bounds.
    uint8_t skyLightAt(int x, int y, int z) const;

    // Baked per-cell COLORED block light (32x32x32, each channel 0-15): flood-filled from emissive
    // voxels in their material colour (physics.colorTint), so a torch glows warm, a crystal blue,
    // etc. Three channels propagate independently (correct colour blending where lights overlap).
    // Block light colour of the air cell at local (x,y,z); 0 if out of chunk bounds (no source).
    void blockLightAt(int x, int y, int z, uint8_t& r, uint8_t& g, uint8_t& b) const;

    // Baked light at a cell WITHOUT inventing an answer: true only when the value is real (in this
    // chunk, or resolved from a baked neighbour). skyLightAt() deliberately guesses "open sky" for
    // an unresolved cell, which is the right optimism for a face's own air cell — a chunk-edge
    // exterior face must not go black just because the neighbour has not streamed in. It is the
    // WRONG answer for the per-corner AO average, which samples cells in the air cell's plane:
    // those guesses pulled full daylight onto interior faces at a chunk seam (measured 11/15 on a
    // floor whose bake was 0), inverting AO into a bright halo. The corner average therefore uses
    // this and skips what it cannot resolve.
    bool bakedLightResolvable(int x, int y, int z, BakedLight& out) const;

    // Reused scratch buffers for rebuildCubeFaces occupancy (32x32x32). Promoted from per-call
    // locals to members + .assign() (like m_skyLight) to avoid a heap alloc/free of ~190KB on
    // every chunk rebuild — meaningful on streaming/edit-heavy scenes.
    std::vector<uint8_t> m_solidVis;    // 1 = a visible cube occupies the cell
    std::vector<int>     m_cellMat;     // index into the per-rebuild matFaces table (-1 = none)
    std::vector<uint8_t> m_cellDamage;  // quantized 0-15 voxel damage (roughness driver)

    // --- What blocks LIGHT (deliberately not m_solidVis) ---
    // m_solidVis answers "is there a visible cube here" and drives face culling + material lookup;
    // it must not be repurposed. Light opacity differs from it in two directions:
    //   * sub-voxel geometry OCCLUDES. m_solidVis is cube-only, so a wall or roof built at subcube
    //     or microcube resolution used to be invisible to the light flood fill — a subcube-roofed
    //     room baked as if it had no roof. Structure generation builds heavily at sub-voxel
    //     resolution, so this leaked daylight through geometry the player sees as solid.
    //   * TRANSPARENT materials do NOT occlude. Glass was blocking skylight completely, so a glazed
    //     room baked pitch dark.
    // Pinned by tests/graphics/LightBakeOcclusionTest.cpp.
    // Per-cell light opacity as a 3-BIT AXIS MASK: bit 0 blocks travel along X, bit 1 along Y,
    // bit 2 along Z. Non-zero means "blocks something"; kOpaqueAllAxes means "blocks everything".
    // It became per-axis on 2026-08-28: opacity is a question about the cross-section a ray
    // crosses, and the scalar fill fraction it replaced could not tell a thin full-face WALL from
    // a scattered handful of decorative subcubes. See the coverage block in rebuildCubeFaces.
    std::vector<uint8_t>  m_lightOpaque;
    static constexpr uint8_t kOpaqueAllAxes = 0x7;
    /// Does cell `cell` block light travelling along `axis` (0=X, 1=Y, 2=Z)?
    bool blocksAxis(int cell, int axis) const {
        return (m_lightOpaque[cell] & (1u << axis)) != 0u;
    }
    // Sub-voxel fill per cell in MICRO-equivalents (a subcube = 27, a microcube = 1, full = 729).
    // A cell counts as light-opaque at or above kLightOpaqueFill: the volume of one full
    // subcube-thick slab (243 = 729/3), which is what a 1-subcube wall or roof actually occupies.
    // Chosen over a slab-shape test because it is one counter per cell instead of a 27-bit mask,
    // and it errs toward DARKER interiors (a scattered third-full cell blocks) — the safe direction.
    // A single decorative subcube (27) or a 1-microcube-thick trim course (81) stays transparent.
    std::vector<uint16_t> m_subFill;
    static constexpr uint16_t kLightOpaqueFill = 243;  // 729 / 3 = one subcube-thick slab

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
    std::vector<EmissiveLight> m_emissiveLights;   // U3.2: emissive voxels, as real lights

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
