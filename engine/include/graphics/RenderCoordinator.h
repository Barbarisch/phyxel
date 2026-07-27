#pragma once

#include "core/Types.h"
#include "core/WorldConstants.h"
#include "utils/Frustum.h"
#include "graphics/LightManager.h"
#include "graphics/DayNightCycle.h"
#include "graphics/WindSystem.h"
#include "utils/PerformanceMonitor.h"
#include "utils/PerformanceProfiler.h"
#include "utils/GpuProfiler.h"
#include "scene/Entity.h"
#include "ui/HudDataContext.h"
#include <memory>
#include <chrono>
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

    /// Characters beyond this distance from the camera are not drawn at all. Generous
    /// by default — this is a safety net for huge worlds, not an aesthetic LOD knob.
    void  setCharacterCullDistance(float d) { m_charCullDistance = d; }
    float getCharacterCullDistance() const { return m_charCullDistance; }

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
    void setMaxChunkRenderDistance(float distance) { maxChunkRenderDistance = distance; }
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
        // D1 shadow-pass diagnosis (docs/RenderDensityPlan.md): the shadow pass distance-culls only
        // (no frustum), so it may draw far more than visibleChunkCount. These count what it drew.
        int    shadowChunksDrawn     = 0;
        long long shadowInstancesDrawn = 0;   // face instances (each drawn with 36 indices)
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

    // CPU water cellular-automaton sim — its surface cells are rendered per-cell.
    void setWaterManager(Core::WaterManager* mgr) { m_waterManager = mgr; }

    // Water (Phase 0: implicit sea-level surface — see docs/WaterSystem.md).
    // TODO: sea level + enabled should come from per-world config, not render state.
    void  setWaterEnabled(bool enabled) { m_waterEnabled = enabled; }
    bool  isWaterEnabled() const { return m_waterEnabled; }
    void  setSeaLevel(float y) { m_seaLevel = y; }
    float getSeaLevel() const { return m_seaLevel; }

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
    void renderShadowPass(VkCommandBuffer commandBuffer, const glm::mat4& lightSpaceMatrix,
                          const glm::vec3& cullCenter, float cullRadius);

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
    std::vector<CharacterBatch> m_charBatches;
    std::vector<uint8_t> m_charVisibleMain;    ///< per character: in the camera frustum
    std::vector<uint8_t> m_charVisibleShadow;  ///< per character: in the light frustum
    CharacterRenderStats m_charStats;
    // Reused across frames so the per-part instance vector is not reallocated every
    // frame (100 characters = 102,400 entries = ~4 MB).
    std::vector<CharacterInstanceData> m_charInstanceScratch;
    size_t   m_charInstanceHighWater = 0;
    uint32_t m_charInstanceCapacity = kCharacterInstanceCapacity;
    float    m_charCullDistance     = 400.0f;
    bool     m_shadowCharactersEnabled = true;
    // Conservative model-space bound used for the per-character cull sphere. Cheap and
    // O(1): a real per-frame AABB would mean walking every part, which is the work the
    // cull exists to avoid. Oversized on purpose — it can only cull too little.
    static constexpr float kCharacterCullRadius = 6.0f;

    // Dependencies (non-owning pointers)
    Vulkan::VulkanDevice* vulkanDevice;
    Vulkan::RenderPipeline* renderPipeline;
    Vulkan::RenderPipeline* dynamicRenderPipeline;
    std::unique_ptr<ShadowMap> shadowMap;
    std::unique_ptr<PostProcessor> postProcessor;
    std::unique_ptr<GpuProfiler> gpuProfiler;
    // D1 shadow-pass diagnosis: chunks/instances drawn by the shadow pass this frame (stashed here
    // because lastFrameStats is reset after the shadow pass runs). See docs/RenderDensityPlan.md.
    int m_shadowChunksDrawn = 0;
    long long m_shadowInstancesDrawn = 0;
    UI::ImGuiRenderer* imguiRenderer;
    UI::WindowManager* windowManager;
    Input::InputManager* inputManager;
    Camera* camera;
    ChunkManager* chunkManager;
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

    // Lightweight grass-blade layer on grass-topped terrain (distance-limited, cutout).
    std::unique_ptr<GrassRenderPipeline> grassPipeline;
    void renderGrass();  // draws grass for the currently-visible chunks within grass radius

    // Leaf foliage card layer (cutout leaf cards replacing solid leaf voxels).
    std::unique_ptr<FoliageRenderPipeline> foliagePipeline;
    void renderFoliage();  // draws leaf cards for the currently-visible chunks

    // Far-terrain LOD tiles (blocky heightmap columns beyond the real-chunk radius).
    std::unique_ptr<FarTerrainRenderPipeline> farTerrainPipeline;
    std::unique_ptr<FarTerrainManager> farTerrainManager;
    void renderFarTerrain();  // draws frustum-visible far tiles (after static geometry)
public:
    /// Far-terrain manager (debug tile building, params). Null if init failed.
    FarTerrainManager* getFarTerrainManager() { return farTerrainManager.get(); }
    // Runtime grass knobs (see /api/debug/grass). Negative/absent values leave a field unchanged.
    // bladeStyle: 1 = boxy rectangle blades (default), 0 = smooth tapered ribbon.
    void setGrassEnabled(bool on);
    void setGrassParams(float radius, float bladeHeight, float windStrength, int bladesPerVoxel,
                        int bladeStyle = -1, float pushStrength = -1.0f);
    bool isGrassEnabled() const;
    // Runtime foliage knobs (see /api/debug/foliage). Negative/absent values leave a field unchanged.
    void setFoliageEnabled(bool on);
    void setFoliageParams(float cardSize, float windStrength, int cardsPerVoxel, float radius);
    bool isFoliageEnabled() const;
    // Shared wind knobs (see /api/debug/wind). Settings are drift targets; State is the
    // per-frame derived field the shaders consume (read-only, useful for round-trip checks).
    WindSystem::Settings&    windSettings()      { return windSystem.settings(); }
    const WindSystem::State& windState() const   { return windSystem.state(); }
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
