#pragma once

#include "graphics/FarTerrainTypes.h"
#include "graphics/FarTerrainRenderPipeline.h"

#include <vulkan/vulkan.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Phyxel { class WorldGenerator; }

namespace Phyxel {
namespace Graphics {

class FarTerrainMesher;

/// Owns far-terrain LOD tiles: camera-follow ring bookkeeping, CPU meshing on a
/// dedicated worker thread (FarTerrainMesher over a private WorldGenerator copy),
/// per-tile GPU vertex/index buffers with frame-deferred deletion, and the per-frame
/// draw list. No Chunk objects, no physics, no light bake — render-only.
///
/// Ring scheme (clipmap-style): every tile is 64 columns per side; ring i uses LOD
/// step ringSteps[i] (world units per column), so its tile size is 64*step. A tile
/// position belongs to the ring whose annulus contains the tile CENTER. Rings start
/// at distance 0 — tiles under-lap real chunks harmlessly (floor-quantized below the
/// real surface, so real geometry always wins the depth test), which means the far
/// field has no hole even when the camera is far from the streamed-chunk region.
/// Tiles whose footprint is FULLY covered by loaded chunk columns are skipped: there
/// the real chunks render the truth, and a tile would show through player-dug holes
/// and caves. Cross-ring overlaps are de-fought by a per-ring downward Y bias
/// (see FarTerrainMesher).
class FarTerrainManager {
public:
    struct Params {
        /// DEFAULT ON (2026-08-01): this is the tier that makes a world render as a WORLD rather
        /// than a disc of streamed chunks ending in sky. Terrain past the residency radius is
        /// served from the generator's own heightmap at ring LOD instead of being loaded.
        /// Measured on PhyxelProjects/LodTest (streaming Perlin, Release) at (16,140,180) with the
        /// far plane actually raised: **57 tiles drawn / 71,630 triangles, terrain to the horizon,
        /// 319 FPS.**
        ///
        /// ⚠️ IT IS USELESS WITHOUT A FAR PLANE THAT REACHES IT, and that is a SEPARATE knob —
        /// `Application::maxChunkRenderDistance` (editor) / `EngineConfig` / `GameSettings`. At the
        /// editor's old 192-unit default every tile past 192u was frustum-clipped, giving 9-14
        /// tiles drawn and an empty horizon. On 2026-08-01 I misread exactly that as "far terrain
        /// is regressed" and briefly reverted this flag. It was never regressed. If this tier looks
        /// dead, CHECK THE FAR PLANE FIRST — `POST /api/debug/render_distance {"distance":4096}`.
        ///
        /// Known limitation (real, unchanged): this tier is 2.5-D. It knows only GENERATOR terrain,
        /// so it cannot show overhangs, player edits or structures — those are the separate far-LOD
        /// chunk path (C3.3, RenderCoordinator::s_farLodChunks).
        ///
        /// ⚠️ Also found and NOT fixed: a `maxDistance` change does not invalidate the wanted set,
        /// so tiles stay FROZEN until the camera moves past kRefreshDistance. Same defect class C1
        /// fixed for `viewScale` (m_lastRefreshViewScale); it silently returns byte-identical stats
        /// and reads as "no effect". Force a camera move when A/B-ing this system.
        bool  enabled     = true;
        float maxDistance = 4096.0f;        ///< outer far-terrain radius (world units)
        /// 4 rings for a 4096 horizon: bands 0-512-1024-2048-4096, tiles 128/256/512/1024u.
        /// ⚠️ Ring 4's individual contribution has NOT been isolated — the 57-tile / 71.6k-triangle
        /// measurement above is the composed result, not a per-ring breakdown.
        std::vector<int> ringSteps{2, 4, 8, 16};///< LOD step per ring; tileSize = 64*step
        int   maxResidentTiles    = 768;    ///< LRU cap on resident tiles
        /// C1 (docs/ContinuousLodPlan.md): screen-space correction applied to maxDistance
        /// and the per-ring band edges. 1.0 == the reference config (1600x900, fovY 45), so
        /// the default is an exact no-op; RenderCoordinator overwrites it each frame from
        /// Core::LodService. Without this the horizon sat at a fixed WORLD distance and so
        /// covered a different number of pixels at every resolution.
        float viewScale           = 1.0f;
        int   uploadBudgetPerFrame = 4;     ///< GPU uploads per frame
        bool  threaded            = true;   ///< false = build 1 tile/frame on main thread (bisect aid)
    };

    struct RingSpec {
        int   ring;      // 1-based ring index
        int   step;      // world units per column
        int   tileSize;  // world units per side
        float startR;    // annulus inner radius (tile-center distance)
        float endR;      // annulus outer radius
    };

    /// Pure ring layout for a given Params. STATIC and PUBLIC so a test can exercise the
    /// SHIPPED math rather than re-deriving it (the instance ctor needs a VkDevice).
    /// computeRings() delegates here.
    static std::vector<RingSpec> computeRingsFor(const Params& p);

    /// A resident tile's draw data + cull AABB. `treeBuffer`/`treeCount` carry the tile's
    /// far-tree impostor instances (FarTreeInstance array; VK_NULL_HANDLE/0 when the tile is
    /// beyond impostor range or treeless) — drawn by FarTreeRenderPipeline.
    struct TileDraw {
        FarTerrainRenderPipeline::TileDraw draw;
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
        VkBuffer  treeBuffer = VK_NULL_HANDLE;
        uint32_t  treeCount  = 0;
        float     treeMaxHeight = 0.0f;   ///< tallest impostor (extends the cull AABB upward)
        std::vector<TreeSpeciesRange> treeRanges;   ///< per-species runs (value copy — a
                                                    ///< pointer into m_tiles would dangle
                                                    ///< between eviction and rebuild)
        /// Interior tile fully covered by real chunks: skip the TERRAIN draw (dug-hole
        /// protection) but keep the tile resident and its TREES drawable. Interior tiles
        /// used to be dropped from the wanted set entirely — then a zoom-out had NO tree
        /// instances to fade in until the worker rebuilt the tile, exactly the "trees
        /// fade out and nothing is there" gap (user-reported 2026-08-02).
        bool terrainHidden = false;
    };

    /// Returns true when EVERY 32x32 world column in [minXZ, maxXZ] (world units,
    /// inclusive-exclusive) has at least one loaded chunk. Wired by RenderCoordinator.
    using ChunkCoverageFn = std::function<bool(const glm::ivec2& minXZ, const glm::ivec2& maxXZ)>;

    FarTerrainManager(VkDevice device, VkPhysicalDevice physicalDevice);
    ~FarTerrainManager();

    FarTerrainManager(const FarTerrainManager&) = delete;
    FarTerrainManager& operator=(const FarTerrainManager&) = delete;

    /// Configure meshing from the world's procedural generator (COPIED — sampleColumn
    /// is non-const; the worker must never share the streaming generator). Starts the
    /// worker thread. Texture indices resolve through MaterialRegistry (read-only
    /// after startup, so cross-thread reads are safe).
    void configure(const WorldGenerator& generator);
    bool isConfigured() const { return m_mesher != nullptr; }

    void setChunkCoverageFn(ChunkCoverageFn fn) { m_chunkCoverage = std::move(fn); }

    /// Radius (world units) within which the near chunk field is guaranteed complete —
    /// the streaming load distance. Covered-tile suppression (dug-hole protection) only
    /// applies to tiles wholly inside this minus a margin; frontier tiles always draw
    /// (dropping them there opened sky holes at the near/far seam). 0 = never suppress.
    void setNearFieldRadius(float r) { m_nearFieldRadius = r; }

    /// Per-frame driver (main thread, call before rendering): refresh the wanted tile
    /// set when the camera has moved, drain finished meshes (budgeted uploads), evict
    /// out-of-range tiles (frame-deferred buffer deletion), and tick the graveyard.
    void update(const glm::vec3& cameraPos);

    /// Debug: synchronously build + upload the ring-1 tile containing worldPos at the
    /// given step (bypasses rings/worker). Returns the tile's vertex count.
    size_t debugBuildTile(const glm::vec3& worldPos, int step);

    /// Destroy all resident tiles immediately. Only safe when the GPU is idle.
    void clearTiles();

    /// World-XZ rectangles (minX, minZ, maxX, maxZ) where far-tree instances must NOT be
    /// planned — placed-structure footprints. planFlora is the PRISTINE generator plan;
    /// settlement builds edit chunks (persisted in world.db) so the near field has no trees
    /// there, but the far tier re-derived from the plan and grew phantom trees through
    /// buildings (user-reported 2026-08-02). Retires all resident tiles so existing tree
    /// buffers rebuild against the new zones (rare event: world load / structure build).
    void setTreeExclusions(std::vector<glm::vec4> rects);

    const std::vector<TileDraw>& tileDraws() const { return m_draws; }
    size_t residentTiles() const { return m_tiles.size(); }
    size_t pendingTiles() const { return m_pending.size(); }
    Params& params() { return m_params; }
    const Params& params() const { return m_params; }

private:
    struct GpuTile {
        VkBuffer       vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer       indexBuffer  = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory  = VK_NULL_HANDLE;
        VkBuffer       treeBuffer   = VK_NULL_HANDLE;  ///< FarTreeInstance array (impostors)
        VkDeviceMemory treeMemory   = VK_NULL_HANDLE;
        uint32_t       treeCount    = 0;
        float          treeMaxHeight = 0.0f;
        std::vector<TreeSpeciesRange> treeRanges;      ///< per-species runs into treeBuffer
        uint32_t       indexCount   = 0;
        glm::vec2      origin{0.0f};
        glm::vec3      aabbMin{0.0f};
        glm::vec3      aabbMax{0.0f};
    };
    struct TileRequest {
        FarTileKey key;
        int   step;
        float dist;      // camera distance at request time (nearest-first ordering)
    };

    std::vector<RingSpec> computeRings() const;
    void refreshWantedSet(const glm::vec3& cameraPos);
    void drainResults();
    void evictTiles(const glm::vec3& cameraPos);
    void tickGraveyard();
    void workerLoop();
    void stopWorker();

    bool uploadTile(const FarTileKey& key, const FarTileMesh& mesh);
    void retireTile(GpuTile& tile);   // frame-deferred destruction
    void destroyTile(GpuTile& tile);  // immediate destruction
    void rebuildDrawList();
    bool createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                          VkBuffer& buffer, VkDeviceMemory& memory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkDevice         m_device;
    VkPhysicalDevice m_physicalDevice;
    Params           m_params;
    ChunkCoverageFn  m_chunkCoverage;
    float            m_nearFieldRadius = 0.0f;  // see setNearFieldRadius

    // Worker thread (owns m_mesher after configure()).
    std::unique_ptr<FarTerrainMesher> m_mesher;
    std::thread             m_worker;
    std::mutex              m_requestMutex;
    std::condition_variable m_requestCv;
    std::deque<TileRequest> m_requests;       // nearest-first; guarded by m_requestMutex
    std::mutex                m_resultMutex;
    std::vector<FarTileMesh>  m_results;      // guarded by m_resultMutex
    std::atomic<bool>       m_stopWorker{false};

    // Main-thread tile state.
    std::unordered_map<FarTileKey, GpuTile, FarTileKeyHash> m_tiles;
    std::unordered_set<FarTileKey, FarTileKeyHash> m_wanted;   // current wanted set
    std::unordered_set<FarTileKey, FarTileKeyHash> m_keep;     // wanted + hysteresis margin
    std::unordered_set<FarTileKey, FarTileKeyHash> m_pending;  // requested, not yet resident
    std::unordered_set<FarTileKey, FarTileKeyHash> m_terrainHidden; // resident, trees-only
    std::vector<glm::vec4> m_treeExclusions;   // structure footprints (world XZ rects)
    std::vector<std::pair<int, GpuTile>> m_graveyard;          // frames-left, retired buffers
    std::vector<TileDraw> m_draws;
    glm::vec2 m_lastRefreshPos{0.0f};
    /// C1: the viewScale the wanted-set was last built at. A scale change moves the whole
    /// horizon, so it MUST invalidate the wanted set — otherwise the rings only rebuild once
    /// the camera happens to travel kRefreshDistance, and a resolution change silently does
    /// nothing (found by a live force_scale sweep: far-tile counts were byte-identical at
    /// 0.5/1.0/2.0 because refreshWantedSet never re-ran).
    float m_lastRefreshViewScale = -1.0f;
    bool      m_hasRefreshed = false;
    bool      m_wasEnabled   = false;
};

} // namespace Graphics
} // namespace Phyxel
