#pragma once

#include "core/Types.h"
#include "core/LodService.h"
#include "core/WorldConstants.h"
#include "utils/Frustum.h"
#include "graphics/LightManager.h"
#include "graphics/DayNightCycle.h"
#include "graphics/CelestialBody.h"
#include "graphics/WindSystem.h"
#include "utils/PerformanceMonitor.h"
#include "utils/PerformanceProfiler.h"
#include "utils/GpuProfiler.h"
#include "scene/Entity.h"
#include "ui/HudDataContext.h"
#include "graphics/TreeLodMeshRegistry.h"
#include "graphics/TreeLodRenderPipeline.h"
#include "graphics/FarTerrainRenderPipeline.h"   // TileDraw (far-cascade caster cache)
#include <functional>
#include <future>
#include <memory>
#include <chrono>
#include <climits>
#include <tuple>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>

// Forward declarations
namespace Phyxel {
    namespace UI { 
        class WindowManager;
        class ImGuiRenderer;
        class UISystem;
    }
    namespace Input { class InputManager; }
    namespace Vulkan { 
        class VulkanDevice;
        class RenderPipeline;
    }
    namespace Utils {
        class PerformanceMonitor;
    }
    class ChunkManager;
    class VoxelTemplate;
    class PerformanceProfiler;
    class RaycastVisualizer;
    class ScriptingSystem;
    class VfxSystem;
    class VfxDirector;
    namespace Graphics {
        class DebrisRenderPipeline;
        class KinematicVoxelPipeline;
        class GrassRenderPipeline;
        class FoliageRenderPipeline;
        class FarTerrainRenderPipeline;
        class FarTreeRenderPipeline;
        class TreeLodRenderPipeline;
        class TreeLodMeshRegistry;
        class FarTerrainManager;
        class VfxRenderPipeline;
        class WaterRenderPipeline;
        class WaterCellRenderPipeline;
        class FireEmitterManager;
    }
    namespace Core {
        class NPCManager;
        class KinematicVoxelManager;
        class WaterManager;
    }
    class GpuParticlePhysics;
}

namespace Phyxel {
namespace Graphics {

class ShadowMap;
class PostProcessor;
class Camera;
class DebrisRenderPipeline;

/**
 * @brief Manages the rendering pipeline and frame rendering
 * 
 * Coordinates all rendering operations including:
 * - Frame synchronization and swapchain management
 * - Static geometry rendering with frustum culling
 * - Dynamic subcube rendering
 * - Command buffer recording and submission
 * - Performance statistics tracking
 */
class RenderCoordinator {
public:
    /// Capacity of the shared character instance buffer, in PARTS (not characters) —
    /// every character in the scene batches into this one buffer. Imported creature
    /// rigs are microcube-dense (3.0-4.7k parts each), so this must stay far above
    /// (densest rig x expected simultaneous creatures). Pinned by
    /// CharacterInstanceBudgetTest against the actual shipped .anim library: at 10000
    /// this silently stopped drawing creatures past ~3 on screen.
    static constexpr uint32_t kCharacterInstanceCapacity = 262144;

    /// Minimum number of copies of the DENSEST shipped rig the budget must hold.
    static constexpr uint32_t kMinSimultaneousDenseCreatures = 20;

    /// Which pass a character batch is being drawn for (selects the visibility set).
    enum class CharacterPassVisibility { Main, Shadow, All };

    /// Per-frame character culling/batching counters (surfaced via /api/render/stats).
    struct CharacterRenderStats {
        uint32_t considered   = 0;  ///< characters known to the scene
        uint32_t drawnMain    = 0;  ///< passed the camera frustum + distance test
        uint32_t drawnShadow  = 0;  ///< passed the light frustum + distance test
        uint32_t culled       = 0;  ///< skipped entirely (neither pass wanted them)
        uint32_t dropped      = 0;  ///< wanted, but did not fit the instance budget
        uint32_t partsBatched = 0;  ///< instances actually uploaded this frame
        uint32_t drawCallsMain   = 0;  ///< one per bone group per visible character
        uint32_t drawCallsShadow = 0;
        double   buildMs = 0.0;     ///< CPU: cull + batch + upload, once per frame
    };
    const CharacterRenderStats& getCharacterRenderStats() const { return m_charStats; }

    /// Character instance budget, in parts. Defaults to kCharacterInstanceCapacity;
    /// a project may raise it for crowd-heavy scenes. Applied at buffer creation.
    void setCharacterInstanceCapacity(uint32_t parts) { m_charInstanceCapacity = parts; }
    uint32_t getCharacterInstanceCapacity() const { return m_charInstanceCapacity; }

    /// Hand-tuned character LOD distances, in WORLD UNITS. Exposed as a struct so a
    /// characterization test can pin them without constructing a RenderCoordinator
    /// (which needs a live Vulkan device). These had ZERO test coverage before
    /// 2026-07-29. They are the thresholds C1 of docs/ContinuousLodPlan.md re-homes
    /// onto Core::LodService's screen-space metric — today they are blind to FOV and
    /// resolution, so the same character at the same distance decimates identically
    /// at 480p/100° and 4K/20°.
    /// C1: vegetation fade radius, screen-space corrected. Grass/foliage are pure render
    /// fades — a blade covering the same pixels should fade at the same point regardless
    /// of resolution. Exactly the base value at the reference config (viewScale == 1).
    ///
    /// STATIC and PUBLIC on purpose: as a private member it could not be called from a
    /// test, so the test re-derived the formula instead — and mutating this function to
    /// `return baseRadius` (disabling the feature) left all 32 tests green. Same defect
    /// as the character path had. Tests now call THIS.
    static float effectiveVegetationRadius(float baseRadius, float viewScale) {
        return baseRadius * viewScale;
    }

    /// A/B toggle for the screen-space correction across ALL re-homed systems
    /// (POST /api/debug/screen_space_lod). OFF reproduces the legacy world-unit behaviour
    /// byte-for-byte, which is what the characterization tests pin.
    static bool s_screenSpaceLod;

    /// C2.1: one multidraw per arena block in the SHADOW pass instead of one draw per chunk.
    /// DEFAULT OFF (POST /api/debug/gpu_driven_shadow). The shadow pass is 75% of the frame and
    /// ~131 draws (docs/RenderDensityPlan.md §2d), and chunks sharing an arena buffer can be
    /// submitted together — the per-chunk origin moves to ShadowMap's SSBO indexed by gl_DrawIDARB.
    static bool s_gpuDrivenShadow;

    /// C5 (docs/ContinuousLodPlan.md): DISTANCE-DRIVEN LOD. Joins C1's screen-space metric to
    /// C4's cut — each chunk is meshed at the level where its cells stop being worth their
    /// pixels. DEFAULT OFF — its working window is only ~136-352 units (residency bounds it), and
    /// that band is exactly where grass lives. Long-range view distance comes from far terrain +
    /// the far-LOD chunk path instead. Full reasoning at the definition in RenderCoordinator.cpp.
    static bool s_distanceDrivenLod;
    /// Target on-screen size (pixels) for one LOD cell. Larger = coarsen sooner/harder.
    static float s_lodTargetPixels;
    /// Coarsest LOD level distance selection may choose. Bounded below the ladder's max because
    /// of the fattening defect — see the definition in RenderCoordinator.cpp.
    static int s_lodMaxLevel;
    /// Chunk re-meshes allowed per frame. A full re-mesh is ~40-50 ms, so an unbounded
    /// budget would turn camera motion into a stutter storm.
    static int s_lodRebuildBudgetPerFrame;

    /// Per-frame: pick each chunk's level from the metric and re-mesh a bounded number of the
    /// chunks whose level changed. Returns how many were re-meshed.
    int updateChunkLod();

    // --- C3.3: the far-chunk draw path (docs/ContinuousLodPlan.md) ------------------------
    // Geometry for chunks that are NOT resident, served from the persisted LOD pyramid. This
    // is what breaks the R^2 residency wall measured in
    // docs/evidence/lod_residency_wall_20260730.txt: a far chunk costs its coarse face buffer
    // (~18.7 KB at lod 2) instead of ~1.28 MB of resident chunk.
    //
    // Default ON. This is the only tier that carries STRUCTURES and player EDITS past the
    // residency radius — far terrain is generator-only and structurally cannot show a building.
    // Sources, tried in order per chunk: (1) the in-memory EvictedLodCache — coarse LODs built
    // at EVICTION time, which is what lets unsaved generated content (trees, structures) keep a
    // far representation (world-look A1/A2); (2) the persisted pyramid in chunk_lod_blobs
    // (saved/edited chunks, and anything from previous sessions). Plain distant terrain remains
    // far terrain's job — pure-cube chunks are cached by neither source.
    static bool s_farLodChunks;
    static int  s_farLodBudgetPerFrame;   // buffers created per frame; creation is the hitch
    /// Per-instance tree level crossfade (2026-08-05). OFF = per-tile-centre level selection
    /// (single draw per (tile, species) — the pre-crossfade behavior, with visible tile pops
    /// at ladder boundaries). A/B knob to MEASURE the straddle double-vertex cost before
    /// committing to the M6 binning build (POST /api/debug/far_terrain
    /// {"per_instance_levels": bool}).
    static bool s_treePerInstanceLevels;
    // NOTE: there is deliberately no reach cap any more. The candidate set comes from
    // storage (chunks that actually HAVE pyramids), so coverage is bounded by the world's
    // real contents rather than by a constant somebody guessed.

    struct FarLodChunk {
        glm::ivec3 chunkCoord{0};
        glm::ivec3 worldOrigin{0};
        // unique_ptr because ChunkRenderBuffer takes the device in its constructor and has
        // no default ctor -- it cannot be a plain member of a default-constructed struct.
        std::unique_ptr<ChunkRenderBuffer> buffer;
        uint32_t instanceCount = 0;
        int level = 0;
    };

    /// Populate/evict far chunks around the camera. Returns how many were newly built.
    int updateFarLodChunks();
    /// Draw them with the same pipeline as resident LOD cells -- the InstanceData is identical,
    /// so no second pipeline and no second shader path.
    void drawFarLodChunks(uint32_t currentFrame);
    size_t farLodChunkCount() const { return m_farLod.size(); }
    size_t farLodInstanceCount() const;

    int getLodRebuiltLastFrame() const { return m_lodRebuiltLastFrame; }

    /// WRv2 M2: wire the template source for instanced far-tree LOD meshes (called by the
    /// application once its ObjectTemplateManager has loaded templates). Until wired, the
    /// mid band stays on impostor cards.
    void setTreeTemplateProvider(std::function<const VoxelTemplate*(const std::string&)> p);

    /// C2.1 guard, as a PURE function so it is testable without a Vulkan device.
    /// `vkCmdDrawIndexedIndirect`'s firstInstance addresses instances by STRIDE, so a chunk's
    /// arena span byte offset is only addressable when it is an exact multiple of the instance
    /// stride. Arena spans are kAlignment=256-aligned and the stride is 24 bytes; 256 % 24 == 16,
    /// so this is false for essentially every real span today. Extracted and made public after an
    /// audit noted the guard had ZERO regression coverage — deleting it would have left all 3110
    /// tests green while the GPU path silently rendered wrong geometry.
    static bool spanIsStrideAddressable(size_t byteOffset, size_t instanceStride) {
        return instanceStride != 0 && (byteOffset % instanceStride) == 0;
    }

    /// C1 (docs/ContinuousLodPlan.md): scales the world-unit character thresholds by how
    /// much bigger/smaller a character actually is ON SCREEN than at the config they were
    /// tuned at (1600x900, fovY 45deg). EXACTLY 1.0 at that config, so this is a no-op
    /// there and a correction everywhere else. Updated per frame from the swapchain.
    float lodViewScale() const {
        return m_lodViewScale > 0.0f ? m_lodViewScale : 1.0f;
    }

    /// Debug-only scale override. 0 = derive from the live view (normal operation).
    static float s_forcedViewScale;
    /// Shadow draw distance in world units (POST /api/debug/shadow {"distance": N}).
    /// Trades reach against texel density AND shadow-pass draw count — measure FPS.
    static float s_shadowDistance;
    /// Near shadow cascade (docs/NearShadowCascade.md): a second, tight map over the near
    /// field so sub-texel casters (grass blades: 0.080 u proxy vs the mid map's 0.1125 u
    /// texel) resolve instead of rasterizing as noise. POST /api/debug/shadow
    /// {"near_enabled": bool, "near_distance": N}.
    static bool  s_nearShadowEnabled;
    static float s_nearShadowDistance;
    /// Far shadow cascade: shadows for the LOD band (mid ends at 420 u; beyond it the far
    /// tiers rendered UNSHADOWED). 4096² fitted to ~1600 u; updated every
    /// s_farShadowCadence frames (coarse + distant = staleness invisible).
    /// POST /api/debug/shadow {"far_enabled", "far_distance", "far_cadence"}.
    static bool  s_farShadowEnabled;
    static float s_farShadowDistance;
    static int   s_farShadowCadence;
    /// M5 (docs/ContinuousLodPlan.md §7b): draw shadow chunks with the 6-index quad instead
    /// of the 36-index cube. The recorded front-cull justification for 36 was FALSE (the
    /// chunk shadow pipeline back-culls); D1's ~1.1% pixel break had an unknown cause. This
    /// toggle re-derives it empirically. POST /api/debug/shadow {"quad": bool}.
    static bool  s_shadowQuadDraw;

    struct CharacterLodDefaults {
        float lod1Distance = 35.0f;
        float lod2Distance = 80.0f;
        float cullDistance = 400.0f;
    };

    /// Characters beyond this distance from the camera are not drawn at all. Generous
    /// by default — this is a safety net for huge worlds, not an aesthetic LOD knob.
    void  setCharacterCullDistance(float d) { m_charCullDistance = d; }
    float getCharacterCullDistance() const { return m_charCullDistance; }

    /// Distances at which characters drop to a decimated part set. 0 disables LOD.
    void setCharacterLodDistances(float lod1, float lod2) {
        m_charLod1Distance = lod1; m_charLod2Distance = lod2;
    }
    float getCharacterLod1Distance() const { return m_charLod1Distance; }
    float getCharacterLod2Distance() const { return m_charLod2Distance; }

    /// Draw characters into the shadow map. Off = characters cast no shadows; exists to
    /// split the character render cost between the main and shadow passes, which the
    /// turn-the-camera-away test cannot do (that changes what terrain is in frame too).
    void setShadowCharactersEnabled(bool e) { m_shadowCharactersEnabled = e; }
    bool getShadowCharactersEnabled() const { return m_shadowCharactersEnabled; }

    RenderCoordinator(
        Vulkan::VulkanDevice* vulkanDevice,
        Vulkan::RenderPipeline* renderPipeline,
        Vulkan::RenderPipeline* dynamicRenderPipeline,
        UI::ImGuiRenderer* imguiRenderer,
        UI::WindowManager* windowManager,
        Input::InputManager* inputManager,
        Camera* camera,
        ChunkManager* chunkManager,
        Utils::PerformanceMonitor* performanceMonitor,
        PerformanceProfiler* performanceProfiler,
        RaycastVisualizer* raycastVisualizer,
        ScriptingSystem* scriptingSystem
    );
    ~RenderCoordinator();

    // Main rendering interface
    void render();
    void drawFrame();
    
    // Render distance management
    void setMaxChunkRenderDistance(float distance);
    void setChunkInclusionDistance(float distance) { chunkInclusionDistance = distance; }

    // Occlusion culling (chunk visibility graph). Default ON (Phase 3).
    void setOcclusionCullingEnabled(bool e) { m_occlusionCullingEnabled = e; }
    bool isOcclusionCullingEnabled() const { return m_occlusionCullingEnabled; }
    int  getLastOcclusionCulled() const { return m_lastOcclusionCulled; }
    
    // Debug visualization
    void toggleDebugMode() { debugModeEnabled = !debugModeEnabled; }
    void setDebugMode(bool enabled) { debugModeEnabled = enabled; }
    bool isDebugModeEnabled() const { return debugModeEnabled; }
    void setDebugVisualizationMode(uint32_t mode) { debugVisualizationMode = mode; }
    uint32_t getDebugVisualizationMode() const { return debugVisualizationMode; }

    struct RenderStats {
        bool   mirrorPassRan         = false;
        int    reflectionDrawCalls   = 0;
        int    mirrorGeomDrawCalls   = 0;
        int    visibleChunkCount     = 0;
        int    totalVisibleFaces     = 0;   // per-face instances across visible chunks (greedy-merged cubes count as 1)
        int    farTilesResident      = 0;   // far-terrain LOD tiles resident on GPU
        int    farTilesDrawn         = 0;   // far-terrain tiles drawn last frame (post frustum cull)
        int    farTriangles          = 0;   // triangles across drawn far tiles
        int    farTrees              = 0;   // far-tree impostor instances drawn last frame
        // WRv2 M2 acceptance metric — tree instances drawn per 50u camera-distance annulus
        // (tile-center distance × range count; 20 buckets to 1000u), split by tier. THE
        // deadzone detector: a bare ring shows as an annulus collapsing vs its neighbors,
        // as a number instead of a squint (docs/WorldRenderV2Plan.md §6).
        static constexpr int kTreeAnnuli = 40;      // 50u each → 0..2000u (full tree range)
        int    farTreeMeshAnnuli[kTreeAnnuli] = {}; // instanced-mesh tier
        int    farTreeCardAnnuli[kTreeAnnuli] = {}; // card tier
        // LOD observability (/api/debug/lod_report): which chain level the tree mesh tier
        // actually drew this frame, and how many card draws covered instead. Index = chain
        // level (0 unused today, 1..5 live). Reset with the annuli above in renderFarTerrain.
        static constexpr int kTreeChainLevels = 6;
        int    farTreeMeshDrawsByLevel[kTreeChainLevels] = {};
        int    farTreeCardDraws       = 0;
        // D1 shadow-pass diagnosis (docs/RenderDensityPlan.md): the shadow pass distance-culls only
        // (no frustum), so it may draw far more than visibleChunkCount. These count what it drew.
        int    shadowChunksDrawn     = 0;
        long long shadowInstancesDrawn = 0;   // face instances (each drawn with 36 indices)
        int shadowMultidrawCalls = 0;  ///< C2.1: vkCmdDrawIndexedIndirect calls (one per arena buffer)
        int shadowGrassChunks    = 0;  ///< grass chunks submitted as shadow CASTERS
        int shadowFoliageChunks  = 0;  ///< foliage chunks submitted as shadow CASTERS
        float  mirrorPlaneX = 0, mirrorPlaneY = 0, mirrorPlaneZ = 0;
        float  mirrorNormalX = 0, mirrorNormalY = 0, mirrorNormalZ = 0;
        float  reflCamX = 0, reflCamY = 0, reflCamZ = 0;
    };
    const RenderStats& getLastFrameStats() const { return lastFrameStats; }

    // D1c (docs/RenderDensityPlan.md): cull the shadow pass against the LIGHT frustum (the fitted
    // ortho shadow volume in lightSpaceMatrix), not just the loose distance sphere. Correct — chunks
    // outside the shadow-map volume can't write to it. Default ON; toggle for A/B (138→fewer draws).
    static bool s_shadowFrustumCull;

    // Phase 3 face-direction bucketing (docs/LargeWorldScalePlan.md): chunk instance
    // buffers are direction-major; main + shadow passes submit only ranges the GPU
    // wouldn't cull. Default ON; POST /api/debug/face_dir_cull toggles for A/B.
    static bool s_faceDirCull;

    // Raycast visualization
    void toggleRaycastVisualization() { raycastVisualizationEnabled = !raycastVisualizationEnabled; }
    void setRaycastVisualization(bool enabled) { raycastVisualizationEnabled = enabled; }
    bool isRaycastVisualizationEnabled() const { return raycastVisualizationEnabled; }
    RaycastVisualizer* getRaycastVisualizer() const { return raycastVisualizer; }
    
    // Scripting Console
    void setShowScriptingConsole(bool show) { showScriptingConsole = show; }
    bool isScriptingConsoleVisible() const { return showScriptingConsole; }

    // Ambient Light Control
    void setAmbientLightStrength(float strength) { ambientLightStrength = glm::clamp(strength, 0.0f, 2.0f); }
    float getAmbientLightStrength() const { return ambientLightStrength; }
    void adjustAmbientLightStrength(float delta) { setAmbientLightStrength(ambientLightStrength + delta); }

    // Lighting Control
    glm::vec3& getSunDirection() { return sunDirection; }
    glm::vec3& getSunColor() { return sunColor; }
    float& getAmbientLightRef() { return ambientLightStrength; }
    float& getEmissiveMultiplierRef() { return emissiveMultiplier; }
    LightManager& getLightManager() { return lightManager; }

    // Day/Night Cycle
    DayNightCycle& getDayNightCycle() { return m_dayNightCycle; }
    const DayNightCycle& getDayNightCycle() const { return m_dayNightCycle; }

    GpuProfiler* getGpuProfiler() { return gpuProfiler.get(); }
    
    // Lighting Controls UI
    void toggleLightingControls() { showLightingControls = !showLightingControls; }
    bool isLightingControlsVisible() const { return showLightingControls; }

    // Profiler UI
    void toggleProfiler() { showProfiler = !showProfiler; }
    bool isProfilerVisible() const { return showProfiler; }

    // Entity rendering
    void setEntities(const std::vector<std::unique_ptr<Scene::Entity>>* entities) { this->entities = entities; }
    void setNPCManager(Core::NPCManager* npcManager) { m_npcManager = npcManager; }

    // GPU particle physics — must be set before the first drawFrame()
    void setGpuParticlePhysics(GpuParticlePhysics* gpp) { m_gpuParticles = gpp; }

    // Kinematic voxel objects (doors, platforms, etc.)
    void setKinematicVoxelManager(Core::KinematicVoxelManager* mgr) { m_kinematicObjects = mgr; }

    /// Wall-clock ms the app spent in physics THIS frame — surfaced in the
    /// perf overlay / render stats (historically hardcoded to 0; 2026-08-07).
    void setPhysicsFrameMs(double ms) { m_physicsFrameMs = ms; }

    // CPU water cellular-automaton sim — its surface cells are rendered per-cell.
    void setWaterManager(Core::WaterManager* mgr) { m_waterManager = mgr; }

    // Water (Phase 0: implicit sea-level surface — see docs/Water.md).
    // TODO: sea level + enabled should come from per-world config, not render state.
    void  setWaterEnabled(bool enabled) { m_waterEnabled = enabled; }
    bool  isWaterEnabled() const { return m_waterEnabled; }
    void  setSeaLevel(float y) { m_seaLevel = y; }
    float getSeaLevel() const { return m_seaLevel; }

    // Gerstner swell on the sea sheet (WaterSystemV3 Phase 2). Amplitude 0 = flat, which restores
    // the pre-Phase-2 look and is the A/B control for "the waves are what changed".
    void setWaves(float amplitude, float wavelength, float windDirectionRadians);
    // Current swell settings as {amplitude, wavelength, windDirection}; zeroes if no pipeline.
    glm::vec3 waveSettings() const;

    // Wind SPEED in m/s (v4 W3). Drives the per-body profile: fetch-limited wave energy (SMB/CERC)
    // and Cox-Munk ripple roughness. Setting it rebuilds the hydrology texture, since both are
    // baked per column there. Default 6.7 = Beaufort 4 mid-point = today's look.
    void setWindSpeed(float metresPerSecond);
    float windSpeed() const;

    // ── Water Appearance v4, W1 (docs/Water.md) ───────────────────────────────────
    // Force a turbidity/roughness profile onto every wet column, bypassing per-body derivation.
    // THE POSITIVE CONTROL for the profile pipe: derivation is neutral in W1, so a measurable pixel
    // change under an override is what proves body → texture → shader → frame actually carries the
    // value. Deliberately routed through the REAL production path (it re-uploads the hydrology
    // texture) rather than a side channel, so it cannot pass while the shipped path is broken.
    // `active = false` restores derivation. Only affects worlds with a hydrology bake — a flat-sea
    // world has no bodies and therefore no profiles, by construction.
    void setWaterLook(bool active, float turbidity, float roughness);
    // {active ? 1 : 0, turbidity, roughness}.
    glm::vec3 waterLook() const;

    // ── Screen-space reflection on water (v4 W4) ──────────────────────────────────────────────
    // Marches the scene depth buffer the water pass already binds, falling back to the procedural
    // sky wherever the ray leaves the screen or finds nothing. OFF reproduces the pre-W4 look
    // exactly (sky reflection only), which makes it the A/B control for before/after captures and
    // the escape hatch if it misbehaves. Water can cover the whole screen, so this is the one
    // v4 item with real per-fragment cost — measure in Release.
    void setWaterSsr(bool on) { m_waterSsrEnabled = on; }

    // ── GROUNDED WATER GRID (docs/Water.md) ────────────────────────────────────────
    // Upload a per-voxel-column water grid built from LIVE terrain, for worlds with NO hydrology
    // bake. rgba is the same packing as the bake path (R = level or the <-1e5 dry sentinel,
    // G = wave energy, B = turbidity, A = roughness), cellSize is 1 voxel, and it is passed to the
    // pipeline NEGATIVE — the sign tells the shaders that off-grid columns are DRY, because a
    // bounded world has no implicit ocean beyond its edges. This replaces the implicit flat sea
    // for such worlds: with it bound, a column with no terrain under it cannot draw water.
    void uploadGroundedWaterGrid(const std::vector<float>& rgba, int cellsX, int cellsZ,
                                 float originX, float originZ);

    // THE SANE BASELINE (docs/Water.md, user order 2026-08-04): streaming baked worlds render
    // water from CHUNK SPANS over resident chunks, off-grid dry — one placement rule, coverage
    // identical to terrain residency, content viewer-independent. Replaces the bake as the
    // placement source on screen. Called per frame; rebuilds on chunk arrivals, rate-limited.
    void updateSpanWaterGrid();
    bool waterSsr() const { return m_waterSsrEnabled; }

    // Is the camera under water, and how far? Returns 0 above the surface, 1 fully submerged, and
    // fades across a short band so breaking the surface doesn't pop. `depthBelow` receives how far
    // under the surface the eye is (world units, 0 when above). Drives the underwater fog overlay.
    // (WaterSystemV3 Phase 1 item 5.)
    float cameraSubmergence(float& depthBelow) const;

    // Lightweight VFX particle system (spell bursts, etc.).
    VfxSystem* getVfxSystem() { return vfxSystem.get(); }
    VfxDirector* getVfxDirector() { return vfxDirector.get(); }
    void updateVfx(float dt); // tick VFX director + integrate particles (call once per frame)

    // Custom UI system (non-ImGui menus)
    /// Create and initialize the UISystem. Must be called after construction.
    bool initUISystem();
    UI::UISystem* getUISystem() { return m_uiSystem.get(); }

    /// Shared HUD data-binding context. Hosts (editor Application, standalone game)
    /// register named providers here; the render loop applies them to the "hud"
    /// screen each frame before drawing (single source of truth). See docs/HudSystem.md.
    UI::HudDataContext& hudData() { return m_hudData; }
    
    // Render UI elements (must be called between ImGui::NewFrame and ImGui::Render)
    void renderUI();

    // Viewport texture for editor docking (offscreen scene image)
    VkImageView getViewportImageView() const;
    VkSampler getViewportSampler() const;

    // Frame state accessors
    void setFrameStartTime(std::chrono::high_resolution_clock::time_point time) { frameStartTime = time; }
    /// Draw the atmospheric sky + sun + moon. Called FIRST inside the scene pass, with depth test
    /// and write disabled, so it fills the frame and geometry draws over it. Sets its own viewport
    /// and scissor because it runs before the pass's main dynamic-state setup.
    void drawSky(VkCommandBuffer cmd);

    // ---- Exposure + tone curve (live-tunable; POST /api/debug/tonemap) --------------------------
    // The atmosphere returns physical RADIANCE, so exposure is the unit conversion that makes it
    // visible at all rather than an optional grade. Live-settable because calibrating it against a
    // rebuild cycle would be unbearable: the whole point is to measure, adjust, measure.
    /// The sky's celestial bodies. Replacing the list takes effect on the next frame -- the
    /// bodies are placed and uploaded per frame, so nothing needs a rebuild or a relight.
    void setSkyBodies(const SkyBodies& s) { m_skyBodies = s; }
    const SkyBodies& getSkyBodies() const { return m_skyBodies; }
    SkyBodies& getSkyBodiesMutable() { return m_skyBodies; }

    void setExposure(float e) { m_exposure = (e > 0.0f) ? e : 1.0f; }
    float getExposure() const { return m_exposure; }
    void setTonemapCurve(int c) { m_tonemapCurve = c; }
    int  getTonemapCurve() const { return m_tonemapCurve; }

    void setCachedViewMatrix(const glm::mat4& view) { cachedViewMatrix = view; }
    const glm::mat4& getCachedViewMatrix() const { return cachedViewMatrix; }
    const glm::mat4& getCachedProjectionMatrix() const { return cachedProjectionMatrix; }
    void setProjectionMatrixNeedsUpdate(bool needsUpdate) { projectionMatrixNeedsUpdate = needsUpdate; }
    
    uint32_t getCurrentFrame() const { return currentFrame; }

    /// Get the swapchain image index that was last rendered and presented.
    uint32_t getLastImageIndex() const { return m_lastImageIndex; }

    /// Capture the most recently presented swapchain image as RGBA pixel data.
    /// Returns an empty vector on failure. Must be called from the main thread.
    /// Output: width*height*4 bytes of RGBA data, top-to-bottom row order.
    std::vector<uint8_t> captureScreenshot();

private:
    const std::vector<std::unique_ptr<Scene::Entity>>* entities = nullptr;
    Core::NPCManager* m_npcManager = nullptr;

    // Grass interaction displacers (docs/VegetationWindPlan.md Phase 4 v1). Per-character
    // state so the push has an eased attack/release envelope — stateless on/off popped the
    // grass upright the frame a character left. Keyed by the character pointer; entries fade
    // out and self-erase once the envelope decays, so despawns can't leak.
    struct GrassDisplacerState {
        glm::vec3 pos{0.0f};
        float     envelope = 0.0f;   // 0..1 eased strength
        bool      present  = false;  // seen this frame
    };
    std::unordered_map<const void*, GrassDisplacerState> m_grassDispStates;
    float m_grassDispLastTime = -1.0f;   // elapsedTime of the previous displacer tick
    std::unique_ptr<UI::UISystem> m_uiSystem;
    UI::HudDataContext m_hudData;

    // Rendering subsystems
    size_t renderStaticGeometry();
    void renderTransparentGeometryOIT(uint32_t frameIndex);
    void renderMirrorGeometry(uint32_t frameIndex);
    void renderReflectionPass(uint32_t frameIndex);
    bool scanForMirrorVoxels(); // Returns true if any mirror voxels found in visible chunks
    void renderDynamicSubcubes();
    void renderEntities(VkCommandBuffer commandBuffer);
    // Draw all instanced characters (player + animated NPCs) with the given view-projection
    // and pipeline. Used both for the main pass and the mirror reflection pass (which passes
    // the reflected view-projection + the FRONT_BIT reflection pipeline). Consumes the
    // per-frame batch list built ONCE by buildCharacterFrameData() — see that method.
    void renderInstancedCharacters(VkCommandBuffer commandBuffer, const glm::mat4& viewProj,
                                   VkPipeline pipeline, CharacterPassVisibility visibility);
    /// One cascade's caster pass. `map` = which shadow map to render into; `cascade`
    /// selects the caster policy: 0 = MID (chunks + characters + kinematic + dynamic +
    /// foliage; GPU-driven multidraw; D1 stats), 1 = NEAR (tight margin; grass casts ONLY
    /// here), 2 = FAR (chunks via multidraw + the cached far-tile and tree-mesh caster
    /// lists; recorded on a cadence — see the drawFrame call site).
    static constexpr int kCascadeMid = 0, kCascadeNear = 1, kCascadeFar = 2;
    void renderShadowPass(VkCommandBuffer commandBuffer, ShadowMap& map,
                          const glm::mat4& lightSpaceMatrix,
                          const glm::vec3& cullCenter, float cullRadius, int cascade);
    /// Last frame's assembled far-field draw lists, replayed by the far cascade's caster
    /// pass (the shadow pass records BEFORE renderFarTerrain assembles this frame's lists;
    /// one frame of staleness is invisible at 0.9 u/texel).
    std::vector<FarTerrainRenderPipeline::TileDraw> m_cachedFarTileDraws;
    std::vector<TreeLodRenderPipeline::MeshDraw>    m_cachedTreeMeshDraws;

    // ---- Character batching (docs/CharacterPipelineScaling.md Tier 1) --------------
    // Cull, sort and batch every character ONCE per frame, before the shadow pass.
    // Previously each pass re-walked every character in the world with no visibility
    // test and re-uploaded a byte-identical instance buffer. Now: characters outside
    // both frusta (or past the distance limit) are skipped entirely, the survivors are
    // batched nearest-first so a budget overrun drops the FAR ones, and the buffer is
    // uploaded once. Each pass then draws only the subset flagged visible for it.
    void buildCharacterFrameData(const glm::mat4& cameraViewProj,
                                 const glm::mat4& lightSpaceMatrix);

    struct CharacterBatch {
        glm::mat4 model;
        uint32_t  firstInstance = 0;
        uint32_t  instanceCount = 0;
        glm::vec4 bakedLight{1.0f};
        int       charIndex = -1;   ///< index into m_charVisibleMain / m_charVisibleShadow
    };
    std::vector<CharacterBatch> m_charBatches;   ///< per BONE GROUP — shadow pass

    /// Per CHARACTER — main pass. All of a character's parts are contiguous in the
    /// instance buffer and each carries its own bone index, so the whole character is
    /// one draw. The shadow pass still needs the per-group list above: its pipeline has
    /// no descriptor sets, and giving it the shared set would bind the shadow map as a
    /// sampler while rendering into it. Collapsing the shadow pass needs its own
    /// bone-only descriptor set — a follow-up (CharacterPipelineScaling P2.2b).
    struct CharacterDraw {
        uint32_t  firstInstance = 0;
        uint32_t  instanceCount = 0;
        glm::vec4 bakedLight{1.0f};
        int       charIndex = -1;
        uint32_t  boneBase = 0;   ///< added to each instance's local bone index
    };
    std::vector<CharacterDraw> m_charDrawsMain;
    std::vector<glm::mat4>     m_charBoneTransforms;

    // Per-character instance blob cache. A part's offset/scale/color never change —
    // animation only writes worldPos/worldRot — so the whole instance payload is static
    // and was being rebuilt every frame by gathering from the 112-byte-stride
    // RagdollPart array (measured 9.5 ms at 1030 characters). Cached here and memcpy'd
    // instead. boneIndex in the blob is a LOCAL group ordinal; the shader adds a
    // per-draw boneBase, which keeps the blob valid even as the frame's bone-SSBO
    // layout changes.
    struct CharacterBlob {
        uint32_t version = 0;      ///< RagdollCharacter::partsVersion() it was built from
        int      lod = -1;
        std::vector<CharacterInstanceData> instances;
        std::vector<int> groupOrder;   ///< boneGroupId per local bone index
        /// {start,count} into `instances` per local bone index. Precomputed because
        /// re-deriving it per frame means scanning every instance, which is exactly the
        /// O(parts) work the blob exists to avoid.
        std::vector<std::pair<uint32_t, uint32_t>> groupSpans;
    };
    std::unordered_map<const Scene::RagdollCharacter*, CharacterBlob> m_charBlobs;
    const CharacterBlob& getCharacterBlob(const Scene::RagdollCharacter* ch, int lod);
    std::vector<uint8_t> m_charVisibleMain;    ///< per character: in the camera frustum
    std::vector<uint8_t> m_charVisibleShadow;  ///< per character: in the light frustum
    CharacterRenderStats m_charStats;
    // Reused across frames so the per-part instance vector is not reallocated every
    // frame (100 characters = 102,400 entries = ~4 MB).
    std::vector<CharacterInstanceData> m_charInstanceScratch;
    size_t   m_charInstanceHighWater = 0;
    uint32_t m_charInstanceCapacity = kCharacterInstanceCapacity;
    float    m_charCullDistance     = CharacterLodDefaults{}.cullDistance;
    bool     m_shadowCharactersEnabled = true;
    float    m_charLod1Distance = CharacterLodDefaults{}.lod1Distance;
    float    m_charLod2Distance = CharacterLodDefaults{}.lod2Distance;
    float    m_lodViewScale = 1.0f;   ///< C1: screen-space correction, 1.0 at reference config

    /// LOD level for a squared camera distance. 0 = full part set.
    /// Screen-space aware: thresholds scale with resolution/FOV so a character that covers
    /// the same number of pixels gets the same LOD level regardless of the view. The legacy
    /// world-unit path is kept for A/B (s_screenSpaceLod=false) and is what the
    /// characterization tests pin.
    int lodForDistanceSq(float distSq) const {
        return Core::LodService::characterLodLevel(
            distSq, m_charLod1Distance, m_charLod2Distance,
            s_screenSpaceLod ? lodViewScale() : 1.0f);
    }

    /// Effective cull distance after the same screen-space correction.
    float effectiveCharacterCullDistance() const {
        return m_charCullDistance * (s_screenSpaceLod ? lodViewScale() : 1.0f);
    }

    /// Recompute the view scale. `fovYDegrees` is 45 today (Camera.h) but is a parameter so
    /// a configurable FOV changes LOD correctly for free.
    void updateLodView(float viewportHeight, float fovYDegrees) {
        // s_forcedViewScale (> 0) pins the scale for live verification: at the reference
        // config the correction is inert by construction, so a runtime capture there proves
        // nothing about the scaling path. POST /api/debug/screen_space_lod {"force_scale":2.0}.
        m_lodViewScale = s_forcedViewScale > 0.0f
            ? s_forcedViewScale
            : Core::LodService::viewScaleVsReference(viewportHeight, fovYDegrees);
    }



    /// The scale the render path should apply to any re-homed world-unit radius this frame.
    /// 1.0 when the correction is disabled or when at the reference config.
    float screenSpaceLodScale() const { return s_screenSpaceLod ? lodViewScale() : 1.0f; }
    // Conservative model-space bound used for the per-character cull sphere. Cheap and
    // O(1): a real per-frame AABB would mean walking every part, which is the work the
    // cull exists to avoid. Oversized on purpose — it can only cull too little.
    static constexpr float kCharacterCullRadius = 6.0f;

    // Dependencies (non-owning pointers)
    Vulkan::VulkanDevice* vulkanDevice;
    Vulkan::RenderPipeline* renderPipeline;
    Vulkan::RenderPipeline* dynamicRenderPipeline;
    std::unique_ptr<ShadowMap> shadowMap;
    /// Near shadow cascade map (4096² over ~40 u — 0.0195 u/texel). Same ShadowMap class,
    /// second instance; its light matrix is fitted per frame alongside the mid map's.
    std::unique_ptr<ShadowMap> shadowMapNear;
    /// One fitted shadow volume (the fit used to live inline in drawFrame; extracted so the
    /// near cascade reuses the identical math — sphere fit, caster margin, texel snap, guard).
    struct ShadowFit {
        glm::mat4 lightSpaceMatrix{1.0f};
        glm::vec3 cullCenterAbs{0.0f};   ///< absolute world (chunk culling)
        float     cullRadius = 1.0f;
        float     depthRange = 1.0f;     ///< ortho depth span (world-unit bias conversion)
        float     texelWorld = 0.0f;     ///< world size of one map texel
    };
    ShadowFit fitShadowVolume(float maxShadowDist, float mapSize, const glm::dvec3& camWorld,
                              const glm::vec3& sunDirection) const;
    glm::mat4 m_nearLightSpaceMatrix{1.0f};
    glm::vec3 m_nearShadowCullCenter{0.0f};
    float     m_nearShadowCullRadius = 1.0f;
    float     m_nearShadowTexelWorld = 0.0f;
    /// Far cascade map + per-frame state. The caster pass records only every
    /// s_farShadowCadence frames (m_farShadowFrameCounter); the map persists between.
    std::unique_ptr<ShadowMap> shadowMapFar;
    glm::mat4 m_farLightSpaceMatrix{1.0f};
    glm::vec3 m_farShadowCullCenter{0.0f};
    float     m_farShadowCullRadius = 1.0f;
    int       m_farShadowFrameCounter = 0;
    std::unique_ptr<PostProcessor> postProcessor;
    std::unique_ptr<GpuProfiler> gpuProfiler;
    // D1 shadow-pass diagnosis: chunks/instances drawn by the shadow pass this frame (stashed here
    // because lastFrameStats is reset after the shadow pass runs). See docs/RenderDensityPlan.md.
    int m_shadowChunksDrawn = 0;
    long long m_shadowInstancesDrawn = 0;
    int m_shadowMultidrawCalls = 0;
    // Near-cascade caster counts (docs/NearShadowCascade.md) — kept separate so the mid
    // cascade's D1 diagnostics stay comparable with their historical record.
    int m_nearShadowChunksDrawn = 0;
    long long m_nearShadowInstancesDrawn = 0;
    // Far-cascade debug counters (/api/debug/lod_report far_shadow block): -1 = the far
    // pass has never recorded. Written on recording frames only (cadence).
    int m_farShadowChunksDrawn = -1;
    int m_farShadowTileCasters = -1;
    int m_farShadowTreeCasters = -1;
    // Vegetation shadow CASTER counts. Must live here, not written straight into
    // lastFrameStats: drawFrame() clears lastFrameStats AFTER the shadow pass and
    // repopulates it from these members (see the note in renderShadowPass).
    float m_shadowTexelWorld = 0.0f;  ///< 2*fittedRadius/mapWidth — drives the grass shadow clamp
    int m_shadowGrassChunks = 0;
    int m_shadowFoliageChunks = 0;
    int m_lodRebuiltLastFrame = 0;   ///< C5: chunks re-meshed for LOD last frame   ///< C2: vkCmdDrawIndexedIndirect calls issued this frame
    glm::ivec3 m_farLodLastScanChunk{INT_MIN};   ///< C3.3: rescan only on a chunk crossing
    bool m_farLodScanIncomplete = false;          ///< budget was hit; more may remain
    uint64_t m_farLodCacheRevision = ~0ull;       ///< EvictedLodCache::revision at last scan (also rescan when it moves — a chunk can evict while the camera is stationary)
    std::vector<glm::ivec3> m_farLodCandidates;   ///< chunks with persisted pyramids OR in-memory evicted LODs
    std::vector<std::unique_ptr<FarLodChunk>> m_farLod;   ///< C3.3: non-resident chunks served from the persisted pyramid
    UI::ImGuiRenderer* imguiRenderer;
    UI::WindowManager* windowManager;
    Input::InputManager* inputManager;
    Camera* camera;
    ChunkManager* chunkManager;
    // Water-layer P1: identity of the last hydrology bake uploaded to the sea pipeline. Starts
    // at a sentinel (not nullptr) so the FIRST frame always uploads — the no-bake form binds the
    // 1×1 dry dummy that keeps the sea drawing in flat mode on non-procedural worlds.
    const void* m_lastHydroUploaded = reinterpret_cast<const void*>(~uintptr_t(0));
    size_t m_spanGridChunkCount = 0;   // rebuild the span water grid when residency changes
    int    m_spanGridCooldown = 0;
    // v4 W1 water-look override (see setWaterLook). Neutral values = today's look exactly.
    bool  m_waterLookActive = false;
    float m_waterLookTurbidity = 0.0f;
    float m_waterLookRoughness = 1.0f;
    bool  m_waterSsrEnabled = true;   // v4 W4; POST /api/debug/water_ssr toggles it
    Utils::PerformanceMonitor* performanceMonitor;
    PerformanceProfiler* performanceProfiler;
    RaycastVisualizer* raycastVisualizer;
    ScriptingSystem* scriptingSystem;
    
    // Render state
    bool showScriptingConsole = false;
    bool showLightingControls = false;
    bool showProfiler = false;
    uint32_t currentFrame = 0;
    float maxChunkRenderDistance = 1000.0f;
    float chunkInclusionDistance = 2000.0f;
    bool debugModeEnabled = false;  // Toggle for debug visualization
    uint32_t debugVisualizationMode = 0;  // 0=wireframe, 1=normals, 2=hierarchy, 3=uv, 4=emissive
    bool raycastVisualizationEnabled = false;  // Toggle for raycast visualization
    float ambientLightStrength = 1.0f; // Default brightness multiplier
    // Exposure default: the atmosphere's noon sunlit diffuse radiance lands near 0.1, so roughly a
    // 6x scale puts a lit surface in the middle of the display range before AgX rolls the top off.
    // Calibrated by measurement (tools/lighting_stats.py), not by eye.
    // The sky's suns and moons. Defaults to one sun + one moon, so an unconfigured world is
    // unchanged; extra bodies are configuration (graphics/CelestialBody.h).
    SkyBodies m_skyBodies = SkyBodies::defaultSky();
    // Direction TOWARD the primary star. The sky pass scatters against THIS, not against
    // sunDirection -- which now tracks whichever body owns the shadow cascades and becomes
    // the moon at night. Using the latter renders a daylight sky at midnight.
    glm::vec3 m_skyStarDir{0.0f, 1.0f, 0.0f};
    float m_exposure = 8.0f;   // calibrated: puts a noon lit surface near 0.16-0.19
                               // linear with 0.00% clipped (measured, exposure sweep)
    int   m_tonemapCurve = 1;   // 1 = AgX, 0 = none (the pre-tonemap look, for A/B)
    glm::vec3 sunDirection = glm::normalize(glm::vec3(-0.6f, -0.7f, -0.45f)); // ~43 deg elevation — angled so structures cast clear shadows (used when day/night is off)
    glm::vec3 sunColor = glm::vec3(1.0f, 1.0f, 1.0f);
    float emissiveMultiplier = 2.0f;
    
    // Day/Night cycle
    DayNightCycle m_dayNightCycle;

    // Cached matrices
    glm::mat4 cachedViewMatrix;
    glm::mat4 cachedProjectionMatrix;
    bool projectionMatrixNeedsUpdate = true;
    
    // Timing
    std::chrono::high_resolution_clock::time_point frameStartTime;
    
    // GPU culling results (for future GPU frustum culling)
    uint32_t lastVisibleInstances = 0;
    uint32_t lastCulledInstances = 0;

    // Last rendered swapchain image index (for screenshot capture)
    uint32_t m_lastImageIndex = 0;

    // Preallocated to avoid per-frame heap allocation in renderStaticGeometry()
    std::vector<size_t> visibleChunkIndices;

    // Occlusion culling (chunk visibility graph). ON by default (Phase 3,
    // docs/LargeWorldScalePlan.md): applyOcclusionCulling() filters
    // visibleChunkIndices to chunks reachable from the camera chunk through
    // air (absent coords are pass-through, bounded by the view frustum) and
    // air-connected chunks. Conservative — no false holes. PHYXEL_OCCLUSION=0
    // env var / POST /api/debug/occlusion disables for A/B.
    bool m_occlusionCullingEnabled = true;
    int  m_lastOcclusionCulled = 0;   // chunks removed by occlusion last frame (debug stat)
    void applyOcclusionCulling(const glm::vec3& cameraPos, const Utils::Frustum& cameraFrustum);
    // Scratch containers reused across applyOcclusionCulling() calls (cleared, not
    // reallocated, each frame — .clear() retains bucket/capacity). Avoids per-frame heap churn.
    std::unordered_map<int64_t, size_t> m_occCoordToIdx;
    std::unordered_set<int64_t> m_occReached;
    std::vector<size_t> m_occKept;

    // Mirror reflection state (updated per-frame when mirror voxels are visible)
    bool hasMirrorVoxels = false;
    glm::vec3 mirrorPlaneNormal{0.0f, 0.0f, 1.0f};
    glm::vec3 mirrorPlanePoint{0.0f};
    glm::mat4 cachedReflectedViewProj{1.0f};

    RenderStats lastFrameStats;

    // Light management
    LightManager lightManager;

    // Debris Rendering
    std::unique_ptr<DebrisRenderPipeline> debrisPipeline;

    // Lightweight VFX particle system + its instanced-cube renderer + composition runtime.
    std::unique_ptr<VfxSystem> vfxSystem;
    std::unique_ptr<VfxDirector> vfxDirector;
    std::unique_ptr<FireEmitterManager> fireEmitters;  // continuous flame VFX from state=flaming voxels
    std::unique_ptr<VfxRenderPipeline> vfxPipeline;

    // Kinematic Voxel Rendering (doors, rotating platforms, etc.)
    std::unique_ptr<KinematicVoxelPipeline> kinematicPipeline;
    Core::KinematicVoxelManager* m_kinematicObjects = nullptr;
    double m_physicsFrameMs = 0.0;   ///< app-provided physics wall time (perf overlay)

    // Kinematic-object culling (2026-08-07): per-frame visibility sets built
    // once (like buildCharacterFrameData) and consumed by the main pass,
    // reflection pass (reuses main), and the shadow kinematic loop.
    void buildKinematicVisibility(const glm::mat4& cameraViewProj,
                                  const glm::mat4& lightSpaceMatrix);
    std::unordered_set<std::string> m_kinVisibleMain;
    std::unordered_set<std::string> m_kinVisibleShadow;
    static constexpr float kKinematicCullDistance = 128.0f;
public:
    struct KinematicCullStats { uint32_t considered = 0, mainVisible = 0,
                                shadowVisible = 0, culled = 0; };
    const KinematicCullStats& kinematicCullStats() const { return m_kinCullStats; }
private:
    KinematicCullStats m_kinCullStats;

    // Lightweight grass-blade layer on grass-topped terrain (distance-limited, cutout).
    std::unique_ptr<GrassRenderPipeline> grassPipeline;
    void renderGrass();  // draws grass for the currently-visible chunks within grass radius

    // Leaf foliage card layer (cutout leaf cards replacing solid leaf voxels).
    std::unique_ptr<FoliageRenderPipeline> foliagePipeline;
    void renderFoliage();  // draws leaf cards for the currently-visible chunks

    // Far-terrain LOD tiles (blocky heightmap columns beyond the real-chunk radius).
    std::unique_ptr<FarTerrainRenderPipeline> farTerrainPipeline;
    std::unique_ptr<FarTreeRenderPipeline> farTreePipeline;   ///< far-tree impostor cards (tail)
    std::unique_ptr<TreeLodRenderPipeline> treeLodPipeline;   ///< instanced tree LOD meshes (mid band)
    /// Per-tile smoothed residency for the LOD→real tree handoff (key: tile origin). Distance
    /// alone can't drive the fade-out — streaming is async, so the fade band's chunks may not
    /// exist yet when the camera arrives; this ramps each tile's fade only after its chunks do.
    std::unordered_map<uint64_t, float> treeHandoffReadiness;

    /// Structure LOD (WRv2 §8, rendering half): per placed structure, a shell-protected
    /// TemplateLodChain mesh set drawn through the tree pipeline when its chunks are not
    /// resident — distant settlements degrade like trees instead of vanishing.
    struct StructureLod {
        glm::ivec3 mn{0}, mx{0};                       ///< world AABB (voxel, inclusive)
        int state = 0;                                 ///< 0 await-extract, 1 building, 2 ready, 3 dead
        std::future<std::array<TreeLodMeshRegistry::CpuMesh,
                               Core::TemplateLodChain::kLevelCount>> job;
        std::array<TreeLodMeshRegistry::GpuLevel, Core::TemplateLodChain::kLevelCount> lv{};
        VkBuffer inst = VK_NULL_HANDLE;
        VkDeviceMemory instMem = VK_NULL_HANDLE;
        float readiness = 1.0f;                        ///< smoothed chunk residency under it
        // Observability (/api/debug/lod_report) — written by tickStructureLod every frame.
        float lastDist    = -1.0f;  ///< camera distance last frame (-1 = beyond bandEnd/not ticked)
        int   lastLevel   = -1;     ///< chain level drawn last frame (-1 = not drawn)
        float lastMinFade = 0.0f;   ///< residency floor pushed to the shader last frame
    };
    std::unordered_map<std::string, StructureLod> structureLod;   // key: structure UUID
    /// Frame-deferred GPU destruction for removed/edited structure entries (a buffer may be
    /// referenced by a command buffer still in flight — same discipline as FarTerrainManager's
    /// tile graveyard).
    struct StructureLodGrave {
        int framesLeft = 4;
        std::vector<std::pair<VkBuffer, VkDeviceMemory>> bufs;
    };
    std::vector<StructureLodGrave> structureLodGraveyard;
    /// Removed/replaced entries whose async chain build may still be running: erasing them
    /// outright would destroy a std::async future mid-flight and BLOCK the main thread in its
    /// destructor. They wait here until the job lands, then their GPU buffers join the
    /// graveyard.
    std::vector<StructureLod> structureLodRetiring;
    void retireStructureLodEntry(StructureLod& e);   ///< move the entry's GPU buffers to the graveyard
    void tickStructureLodGraveyard();                ///< called once per frame from tickStructureLod
public:
    /// Wire from the Application's placed-object poll (uuid, aabbMin, aabbMax).
    void setStructureLodTargets(
        const std::vector<std::tuple<std::string, glm::ivec3, glm::ivec3>>& targets);

    // ---- LOD observability (/api/debug/lod_report + lod_probe) --------------------------
    /// Distance ladders as data, not magic numbers, so the selection code and the debug
    /// report cannot disagree — and densifying a ladder is a one-line change here.
    /// Tree mesh tier: chain level i+1 is used below kTreeMeshLevelDist[i]; L5 beyond the
    /// last entry, cards past bandEnd. Structures: chain level i (0-based — L0's ⅓-voxel
    /// cells ARE selected, right at the handoff) below kStructureLevelDist[i]; L5 beyond.
    /// Densified 2026-08-05 with the full 6-level structure chain (was 3 levels of 4).
    /// RUNTIME-TUNABLE (2026-08-06) so ladder sweeps need no rebuild — the remaining
    /// dense-world tree cost (~8 ms of vertex volume) trades against how long fine levels
    /// hold, and that is a LOOK decision to sweep live: POST /api/debug/far_terrain
    /// {"tree_ladder": [d1, d2, d3, d4]} (ascending; level i+1 below entry i, L5 beyond).
    static float s_treeMeshLevelDist[4];
    static constexpr float kStructureLevelDist[5] = {360.0f, 500.0f, 700.0f, 900.0f, 1200.0f};

    /// Pure residency-gate probe for a structure proxy — decides whether the REAL chunks own
    /// this structure's ground (ready → the proxy may dissolve) and how many probe columns
    /// were close enough to vote. Mirrors the tree tile gate's escapes (tileHandoffMinFade):
    /// out-of-band columns don't vote; zero votes ⇒ distance fade governs; each voting column
    /// probes the AABB's FULL Y-span, not a single mid plane. Static + callback-injected so
    /// tests exercise the shipped rule headlessly (LodServiceTest pattern).
    struct StructureGateResult {
        bool ready = true;   ///< every voting column has rendered geometry
        int  votes = 0;      ///< columns close enough to the camera to vote
    };
    static StructureGateResult structureGateProbe(
        const glm::ivec3& mn, const glm::ivec3& mx, const glm::vec3& cameraPos,
        float fadeGateEnd,
        const std::function<bool(const glm::ivec3&)>& hasRenderedGeometryAt);

    /// Plain-struct snapshot of one structure-LOD entry (no Vulkan/future members).
    struct StructureLodInfo {
        std::string uuid;
        glm::ivec3 mn{0}, mx{0};
        int   state = 0;
        float readiness = 1.0f;
        float lastDist = -1.0f;
        int   lastLevel = -1;
        float lastMinFade = 0.0f;
    };
    std::vector<StructureLodInfo> structureLodReport() const;

    /// Loading observability (/api/debug/load_state): far-tree species mesh builds still
    /// queued/landing, and far-terrain tiles wanted but not yet resident.
    size_t treeLodPendingBuilds() { return treeLodMeshes ? treeLodMeshes->pendingBuilds() : 0; }
    size_t farTilesPending() const;   // body in .cpp (FarTerrainManager is fwd-declared here)
    /// Far-cascade debug counters (-1 = the far pass never recorded).
    int farShadowChunksDrawn() const { return m_farShadowChunksDrawn; }
    int farShadowTileCasters() const { return m_farShadowTileCasters; }
    int farShadowTreeCasters() const { return m_farShadowTreeCasters; }

    /// Live distance thresholds of the pipeline-owned tiers (values the running frame uses —
    /// several are overwritten per frame from streaming config, so header defaults lie).
    struct LodTierThresholds {
        float treeFadeNear0 = 0, treeFadeNear1 = 0, treeBandEnd = 0;
        float cardFadeFar0 = 0, cardFadeFar1 = 0;
        float grassRadius = 0, grassFadeRange = 0;
        float foliageRadius = 0;
        float shadowDistance = 0;
    };
    LodTierThresholds lodTierThresholds() const;
private:
    void tickStructureLod(std::vector<TreeLodRenderPipeline::MeshDraw>& meshDraws,
                          const glm::vec3& cameraPos);
    std::unique_ptr<TreeLodMeshRegistry> treeLodMeshes;       ///< per-species LOD mesh sets
    std::unique_ptr<FarTerrainManager> farTerrainManager;
    void renderFarTerrain();  // draws frustum-visible far tiles (after static geometry)
public:
    /// Far-terrain manager (debug tile building, params). Null if init failed.
    FarTerrainManager* getFarTerrainManager() { return farTerrainManager.get(); }
    /// A/B attribution knob (set_far_terrain {"trees": bool}): disables BOTH far-tree tiers
    /// (instanced meshes + cards) without touching the terrain tiles, so an artifact can be
    /// pinned to trees vs tile geometry in two screenshots.
    void setFarTreesEnabled(bool on);

    /// World size of one shadow-map texel this frame (2*fittedRadius / mapWidth). Diagnostic:
    /// this is what the grass shadow pass clamps blade width against, so if blades stop
    /// casting at some shadow distance, check this value FIRST.
    float getShadowTexelWorld() const { return m_shadowTexelWorld; }
    // Runtime grass knobs (see /api/debug/grass). Negative/absent values leave a field unchanged.
    // bladeStyle: 1 = boxy rectangle blades (default), 0 = smooth tapered ribbon.
    void setGrassEnabled(bool on);
    /// bladeWidthScale <= 0 leaves the width unchanged (same sentinel convention as the rest).
    void setGrassParams(float radius, float bladeHeight, float windStrength, int bladesPerVoxel,
                        int bladeStyle = -1, float pushStrength = -1.0f,
                        float bladeWidthScale = -1.0f);
    /// Meadow height field (the plain-scale height modifier) + edge taper. Negative = unchanged.
    /// Scales are WORLD UNITS; meadowScale decides how large a "plain" reads as. The field is
    /// evaluated in absolute world space, so it is identical either side of any chunk boundary —
    /// appearance must never depend on which chunk a voxel sits in (docs/FeatureDesignKeys.md).
    void setGrassMeadowParams(float meadowScale, float meadowDetailScale, float meadowDetailWeight,
                              float heightMin, float heightMax, float edgeTaperFloor,
                              float edgeTaperCurve);
    /// Snapshot of the grass knobs for API read-back, so a caller can assert a setting actually
    /// took effect rather than trusting it (a knob silently doing nothing against a stale binary
    /// has cost real debugging time here).
    /// A plain struct rather than GrassRenderPipeline::Params on purpose: this header is included
    /// widely and forward-declares its pipelines, so pulling GrassRenderPipeline.h in would cost
    /// compile time across the engine.
    struct GrassParamSnapshot {
        bool     enabled            = false;
        uint32_t bladesPerVoxel     = 0;
        float    bladeHeight        = 0.0f;
        float    bladeWidthScale    = 0.0f;
        float    radius             = 0.0f;
        float    meadowScale        = 0.0f;   ///< world units — how large a "plain" reads as
        float    meadowDetailScale  = 0.0f;
        float    meadowDetailWeight = 0.0f;
        float    heightMin          = 0.0f;
        float    heightMax          = 0.0f;
        float    edgeTaperFloor     = 0.0f;
        float    edgeTaperCurve     = 0.0f;
    };
    GrassParamSnapshot grassParams() const;
    bool isGrassEnabled() const;
    // Runtime foliage knobs (see /api/debug/foliage). Negative/absent values leave a field unchanged.
    void setFoliageEnabled(bool on);
    void setFoliageParams(float cardSize, float windStrength, int cardsPerVoxel, float radius);
    bool isFoliageEnabled() const;
    // Shared wind knobs (see /api/debug/wind). Settings are drift targets; State is the
    // per-frame derived field the shaders consume (read-only, useful for round-trip checks).
    WindSystem::Settings&    windSettings()      { return windSystem.settings(); }
    const WindSystem::State& windState() const   { return windSystem.state(); }
    /// Mutable wind state, for the /api/debug/wind probe knobs (aniso / gustScale / gustSpeed).
    /// ⚑gustScale and gustSpeed are DERIVED from speed+gustiness on every WindSystem update, so
    /// writing them here is sticky only until the next update — they are tuning probes, not
    /// settings. `aniso` is NOT derived, so it persists.
    WindSystem::State& windState()               { return windSystem.mutableState(); }
private:
    // Global procedural wind — ticked once per drawFrame, then copied into BOTH vegetation
    // pipelines' params so grass and foliage can never see diverging wind.
    WindSystem windSystem;

    // Water surface. Default OFF; enabled + sea level come from the per-world game
    // definition ("water": { "enabled": true, "seaLevel": N }), applied on load.
    std::unique_ptr<WaterRenderPipeline> waterPipeline;
    bool  m_waterEnabled = false;
    float m_seaLevel = Core::kSeaLevelY; // shared default (WorldConstants.h) — must match the
                                         // water sim or the plane draws where no water is

    // Per-cell water surface rendering (the CPU sim's actual field).
    std::unique_ptr<WaterCellRenderPipeline> waterCellPipeline;
    Core::WaterManager* m_waterManager = nullptr;
    // True for frames where the reflection pass was rendered for the water plane
    // (decided before the scene pass, consumed when the water surface is drawn).
    bool  m_waterReflectionActive = false;

    // GPU particle physics (non-owning — owned by Application)
    GpuParticlePhysics* m_gpuParticles = nullptr;
};

} // namespace Graphics
} // namespace Phyxel
