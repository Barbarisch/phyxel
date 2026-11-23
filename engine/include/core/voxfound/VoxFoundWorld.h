#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
// docs/VoxelFoundationPlan.md step 7 D3-core -- the switched world (the 1/8 m region: boot, build,
// switch, stream, save / load, edits, undo, LOD, grass, water spans, structures, the bodies) in phyxel_core.
// Moved verbatim from the editor's Application (members and methods keep their names); the host subsystems are
// same-named pointers the host binds. Hosted by the editor (Application) and, next, by GameShell.
#include <functional>
#include "core/voxfound/WorldReads.h"
#include <array>
#include <chrono>
#include "core/Types.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "graphics/RenderCoordinator.h"
#include "graphics/RaycastVisualizer.h"
#include "graphics/Camera.h"
#include "graphics/CameraManager.h"
#include "core/GameplayCameraController.h"
#include "scene/VoxelInteractionSystem.h"
#include "physics/PhysicsWorld.h"
#include "utils/Timer.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Frustum.h"
#include "ui/ImGuiRenderer.h"
#include "ui/WindowManager.h"
#include "input/InputManager.h"
#include "core/ChunkManager.h"
#include "core/FaunaSpawner.h"
#include "core/ResidentSpawner.h"
#include "core/ObjectTemplateManager.h"
#include "core/RuntimeEntityStore.h"
#include "core/AudioSystem.h"
#include "core/SoundRegistry.h"
#include "core/AmbienceDirector.h"
#include "core/ChopManager.h"
#include "scripting/ScriptingSystem.h"
#include "ai/AIEnhancer.h"
#include "ai/AIConversationService.h"
#include "ui/GameMenuRenderer.h"
#include "core/EntityRegistry.h"
#include "core/APICommandQueue.h"
#include "core/CommandRegistry.h"
#include "core/EngineAPIServer.h"
#include "core/JobSystem.h"
#include "core/MainThreadJobs.h"
#include "core/Inventory.h"
#include "core/HealthComponent.h"
#include "core/RespawnSystem.h"
#include "core/MusicPlaylist.h"
#include "core/PlayerProfile.h"
#include "core/ObjectiveTracker.h"
#include "core/GameEventLog.h"
#include "core/TriggerSystem.h"
#include "core/SnapshotManager.h"
#include "core/PlacedObjectManager.h"
#include "core/CombatSystem.h"
#include "core/Party.h"
#include "core/InitiativeTracker.h"
#include "core/CombatDirector.h"
#include "core/CombatAISystem.h"
#include "core/PlayerTurnController.h"
#include "core/WorldClock.h"
#include "core/CampaignJournal.h"
#include "core/NPCManager.h"
#include "scene/IWorldEditor.h"
#include "core/InteractionManager.h"
#include "core/InteractionProfileManager.h"
#include "core/InteractionHandler.h"
#include "core/interactions/SeatInteractionHandler.h"
#include "core/interactions/DoorInteractionHandler.h"
#include "core/interactions/NPCInteractionHandler.h"
#include "core/interactions/PickupInteractionHandler.h"
#include "core/KinematicVoxelManager.h"
#include "core/KinematicAnimator.h"
#include "core/DoorManager.h"
#include "core/DynamicFurnitureManager.h"
#include "core/CoherentFragmentManager.h"
#include "core/ItemPropManager.h"
#include "core/ItemEffectSystem.h"
#include "core/NpcHeldItems.h"
#include "core/LocationRegistry.h"
#include "ui/DialogueSystem.h"
#include "ui/SpeechBubbleManager.h"
#include "ui/HudDataContext.h"
#include "story/StoryEngine.h"
#include "story/RuleBasedCharacterAgent.h"
#include "core/EngineConfig.h"
#include "core/EngineRuntime.h"
#include "core/SceneManager.h"
#include "core/GpuParticlePhysics.h"
#include "core/DebrisRuntime.h"
#include "core/WaterManager.h"
#include "core/water/WaterCoreManager.h"
#include "core/matsim/MatSimLab.h"
#include "core/voxfound/WorldFluid.h"
#include "core/voxfound/FaceBuilderGpu.h"
#include "core/voxfound/LodBuilderGpu.h"
#include "core/voxfound/Preview.h"
#include "core/voxfound/RegionEdit.h"
#include "core/voxfound/EditHistory.h"
#include "physics/VoxelOccupancyGrid.h"
#include "core/MicroCanvas.h"
#include <set>
#include "core/WorldGenerator.h"
#include "core/voxfound/BodyDraw.h"
#include "core/voxfound/BodySim.h"
#include "core/voxfound/BodySimGpu.h"
#include "core/voxfound/BodyRest.h"
#include "core/voxfound/FractureLab.h"
#include "core/water/WadeDrag.h"
#include "core/water/ShoreBand.h"
#include "scene/NPCEntity.h"
#include "scene/Entity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/CharacterTurnBody.h"
#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <thread>
#include <atomic>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Phyxel {

class VoxFoundWorld {
public:
    // ---- the host (bound by the hosting app every frame; null = absent)
    Core::EngineRuntime* runtime = nullptr;
    Vulkan::VulkanDevice* vulkanDevice = nullptr;
    Graphics::Camera* camera = nullptr;
    ChunkManager* chunkManager = nullptr;
    Physics::PhysicsWorld* physicsWorld = nullptr;
    Core::WaterManager* waterManager = nullptr;
    Graphics::RenderCoordinator* renderCoordinator = nullptr;
    VoxelInteractionSystem* voxelInteractionSystem = nullptr;
    Core::PlacedObjectManager* placedObjectManager = nullptr;
    Core::NPCManager* npcManager = nullptr;
    Core::ItemPropManager* itemPropManager = nullptr;
    std::function<void(int, int, int, bool)> applyWaterSpanEdit;   // the host's water-span edit hook (may be empty)
    std::function<void()> processAPICommands;                     // the host's API pump (long jobs keep it alive)
    void tick(float dt);                                          // D3-core: per frame (every host)
    bool preInit(const std::string& gameJsonPath);                 // D3-core: before EngineRuntime::initialize

    WorldEditHit voxFoundPick(const glm::vec3& origin, const glm::vec3& dir, float maxMetres) const;
    size_t voxFoundBrush(const glm::ivec3& voxel, int size, const std::string& material);
    std::unique_ptr<Core::VoxFound::FaceBuilderGpu> voxFoundBuilder;   // F2.2 1/8 m face preview builder (lazy)
    std::unique_ptr<Core::VoxFound::LodBuilderGpu> voxFoundLodBuilder;   // F2.14e GPU level build (lazy)
    Core::VoxFound::Preview voxFoundPreview;                           // F2.2 what the renderer draws (setVoxFoundPreview)
    Core::VoxFound::RegionEdit voxFoundEdit;                           // F2.16 editable region store ({"editable":true})
    std::vector<std::string> voxFoundPalette;                          // F2.16 the region palette (edits may append)
    std::vector<uint16_t> voxFoundLeafTex;                             // F2.16 sprig texture per palette material
    // F7.3: the world switch -- the region drawn AND ChunkManager's reads routed to its stores (WorldReads)
    std::unique_ptr<Core::VoxFound::WorldReads> voxFoundWorldReads;
    bool voxFoundWorldSwitchStub = false;                               // tests: hide + draw, do not route (the red)
    // F7.4: the region as the player's editor (IWorldEditor: RegionEdit::pick + the F2.16 / F3 brush edit path)
    std::unique_ptr<IWorldEditor> voxFoundWorldEditor;
    bool voxFoundEditStub = false;                                      // tests: switched, editor not set (the red)
    bool voxFoundEditVerify = false;                                    // F7.4: the player's brushes verified (edit_verify)
    nlohmann::json voxFoundLastEdit;                                    // the last brush's edit response
    // F7.3b: the switched world's light occupancy source (RenderCoordinator::setVoxFoundOccupancy) + per-chunk edit
    // revisions (bumped by every handleVoxFoundEdit for the chunks it rebuilt)
    Graphics::RenderCoordinator::VoxFoundOccupancySource voxFoundOccSource;
    std::map<std::tuple<int, int, int>, uint32_t> voxFoundOccRevision;
    bool voxFoundOccStub = false;
    bool voxFoundDamageStub = false;                                    // F7.5 (tests): the destruction override ignored
    bool voxFoundSaveStub = false;                                      // F7.6a (tests): save_world skips the region
    Core::VoxFound::EditHistory voxFoundHistory;                        // F7.6b: undo / redo of switched-world edits
    bool voxFoundUndoStub = false;                                      // F7.6b (tests): /api/undo ignores the history
    bool voxFoundApiStub = false;                                       // F7.6c (tests): the voxel API not routed
    // F7.6d: coarse levels 1..3 of an editable region kept current with its edits (the default variant only)
    int voxFoundLodEditLevels = 0;
    int voxFoundLodClusterBlocks = 4;
    bool voxFoundLodDirOctant = true;
    glm::ivec3 voxFoundLodRegionLo{0};
    std::set<size_t> voxFoundLodDirty;
    bool voxFoundLodStub = false;                                       // F7.6d (tests): edits mark nothing dirty
    // F7.8: the switched region's physics occupancy -- one grid per region chunk answering queryAABB from its store,
    // set as VoxelDynamicsWorld overrides while switched
    std::vector<std::unique_ptr<Physics::VoxelOccupancyGrid>> voxFoundPhysGrids;
    bool voxFoundPhysStub = false;                                      // F7.8 (tests): no overrides (the red)
    // F7.9b: the switched world follows the focus (the camera): when it leaves the region's inner chunks the region is
    // rebuilt around it; edited chunks that leave are kept (codec v3, by chunk) and load back when they re-enter
    std::map<std::tuple<int, int, int>, std::vector<uint8_t>> voxFoundStreamCache;
    nlohmann::json voxFoundRegionRequest;   // the last editable region build's request
    bool voxFoundStreamOn = false, voxFoundStreamStub = false;
    bool voxFoundSwitchLoadSaved = true;
    bool voxFoundTemplateStub = false;   // F7.10 (tests): spawn_template not routed
    bool voxFoundStructStub = false;     // F7.11 (tests): build_structure not routed
    nlohmann::json voxFoundCanvasLog = nlohmann::json::array();   // F7.12: every new-store placement's result
    bool voxFoundWriterStub = false;                             // F7.13 (tests): the old-grid writers not routed
    bool voxFoundTreeStub = false;                               // F7.14 (tests): tree-matter reads not routed
    nlohmann::json voxFoundLastPrevBake;                         // F8.2: the pre-bake's report
    nlohmann::json voxFoundLastBreak;                            // F8.2
    nlohmann::json voxFoundSettleRubble();                       // F8.2: a world rig's rubble into whole voxels
    // F8.4: the switched world's structures (box in world voxels, floor surface voxel) -- a new one clears those it
    // overlaps; saved in world_meta "voxfound_structures"
    struct VoxFoundStructure { glm::ivec3 lo{0}, hi{0}; int floor = 0; int id = 0; nlohmann::json lights = nlohmann::json::array();
                               nlohmann::json props = nlohmann::json::array();       // F7.21d: its item props' ids
                               nlohmann::json fixtures = nlohmann::json::array(); };  // F7.21e: its fixture records' ids
    void voxFoundStructureAttachLights(int structureId, const nlohmann::json& lights);   // F7.21c
    std::vector<VoxFoundStructure> voxFoundStructures;
    int voxFoundStructureNext = 1;
    bool voxFoundOverlapStub = false;
    nlohmann::json voxFoundStructuresJson() const;
    void voxFoundStructuresLoad();
    bool voxFoundBoundsStub = false;
    // F7.18: the water span store (step-7 port 1)
    bool voxFoundTodayStreamOff = false;   // the step-7 gate: today's streaming off while switched
    bool voxFoundSpanStoreStub = false;
    bool voxFoundStreamSwapping = false;
    void voxFoundSpansFill();
    void voxFoundGrassFill();   // F7.29
    // D1b: the DB's stored chunks overlaid on a generated region
    bool voxFoundOverlayDb = false;
    bool voxFoundTodayKeep = false;
    bool voxFoundLegacyTemplateStub = false;   // D3a (tests): legacy templates refused (the red)   // D2: game.json world.voxfound.today_world:true keeps today's world loading (A/B)
    size_t voxFoundOverlaidTotal = 0;
    nlohmann::json voxFoundOverlayStored(const std::vector<glm::ivec3>& coords);
    // F7.30: per-chunk grass, refreshed after edits
    std::vector<std::vector<GrassInstanceData>> voxFoundGrassPerChunk;
    std::set<size_t> voxFoundGrassDirty;
    std::chrono::steady_clock::time_point voxFoundGrassLastEdit{};
    bool voxFoundGrassRefreshStub = false;
    size_t voxFoundGrassRefreshes = 0;
    std::vector<GrassInstanceData> voxFoundGrassOf(size_t i, const std::vector<int16_t>& grassTex) const;
    std::vector<int16_t> voxFoundGrassTex() const;
    void voxFoundGrassUpload();
    void voxFoundGrassRefreshTick();
    bool voxFoundGrassStub = false;
    size_t voxFoundGrassCount = 0;
    // F7.24 (step-7 port: the API's units): batch / copy / paste / move / named snapshots on the switched world
    struct VoxFoundBox { glm::ivec3 lo{0}, size{0}; std::vector<uint8_t> mats; };   // 1/8 m voxels, x-major
    VoxFoundBox voxFoundClip;
    bool voxFoundClipSet = false;
    std::map<std::string, VoxFoundBox> voxFoundSnapshots;
    bool voxFoundApiRegion(const std::string& action, const nlohmann::json& p, nlohmann::json& response);
    // F7.19 (W2): span edits from region edits; edited spans saved / loaded
    bool voxFoundSpanEditStub = false;
    bool voxFoundSpansLoadPending = false, voxFoundSpansRestored = false;
    size_t voxFoundSpansLoaded = 0, voxFoundSpanEditCells = 0;
    void voxFoundSpanEdit(const Core::VoxFound::RegionEdit::Stats& st);                             // F8.3b (tests): the switched bounds not set
    bool voxFoundRapidStub = false;                              // F8.2 (tests): a new world rig drops the previous bodies
    bool voxFoundWaterSyncStub = false;                          // F7.17 (tests): the water sim's resyncs skipped
    // F7.16: game.json world.voxfound -- the switched world as a boot mode
    nlohmann::json voxFoundBootConfig;   // the world block (type, seed, flora) + its "voxfound" object
    bool voxFoundBootPending = false;
    int voxFoundBootFrames = 0;
    nlohmann::json voxFoundBootReport;
    void voxFoundBoot();
    void voxFoundReadBootConfig(const std::string& gameJsonPath);
    std::map<std::string, std::pair<size_t, size_t>> voxFoundWriterLog;   // F7.13: label -> (calls, voxels changed)
    std::set<std::tuple<int, int, int>> voxFoundLastCanvasCells;   // F7.11: the last shell's cells (struct_check)
    nlohmann::json voxFoundPlaceCanvas(const Core::MicroCanvas& canvas, int floorTopCell, int originX, int originZ);
    /// F7.10: one undoable edit of listed cells (world voxels, palette ids) on the region -- what placers write through
    nlohmann::json voxFoundPaintCells(const std::string& label, const std::vector<std::pair<glm::ivec3, uint8_t>>& cells);
    nlohmann::json voxFoundSpawnTemplate(const nlohmann::json& params);
    // F7.9c: background shifts -- the entering chunks generated off-thread, prepared one per frame, swapped in one
    struct VoxFoundStreamJob {
        glm::ivec3 lo{0}, hi{0};
        std::vector<glm::ivec3> entering;
        std::vector<Core::VoxFound::VoxelArea> areas;
        std::vector<Core::VoxFound::RegionEdit::Chunk> ready;
        std::thread worker;
        std::atomic<bool> done{false};
        std::string error;
        size_t prepared = 0;
        double genMs = 0.0, maxSliceMs = 0.0, swapMs = 0.0;
        std::chrono::steady_clock::time_point start;
    };
    std::unique_ptr<VoxFoundStreamJob> voxFoundStreamJob;
    std::shared_ptr<WorldGenerator> voxFoundRegionGen;   // a copy of the region build's generator (recipe applied)
    bool voxFoundStreamBg = true;
    double voxFoundStreamMaxSliceMs = 0.0;
    nlohmann::json voxFoundStreamLastBg;
    void voxFoundStreamBgTick(glm::ivec3 newLo);
    nlohmann::json voxFoundStreamSwap();   // the last switch-on's load_saved (a shift reuses it)
    size_t voxFoundStreamShifts = 0;
    double voxFoundStreamLastMs = 0.0, voxFoundStreamMaxMs = 0.0;
    nlohmann::json voxFoundStreamShift(glm::ivec3 newLo);
    void voxFoundStreamTick();
    void voxFoundPhysSwitch(bool on);
    double voxFoundLodMaxChunkMs = 0.0, voxFoundLodLastSelectMs = 0.0, voxFoundLodSumChunkMs = 0.0;
    size_t voxFoundLodRefreshed = 0;
    void voxFoundLodMark(const Core::VoxFound::RegionEdit::Stats& st);
    bool voxFoundLodChunkFaces(size_t i, std::vector<std::vector<Core::VoxFound::PackedFace>>& perLevel,
                               std::string* err);
    nlohmann::json voxFoundLodRefresh(size_t maxChunks);   // 0 = every dirty chunk now
    nlohmann::json voxFoundLodCheck();
    /// F7.6c: place_voxel / remove_voxel / place_subcube / fill_region / clear_region on the switched world (false:
    /// not switched -- today's path answers)
    bool voxFoundApiEdit(const std::string& action, const nlohmann::json& params, nlohmann::json& response);
    /// F7.6c: GET /api/world/voxel on the switched world -- the 8^3 block of cube (x, y, z)
    nlohmann::json voxFoundQuery(int x, int y, int z) const;
    /// F7.6b: undo (true) / redo (false) the switched world's last edit; false when the history has none
    bool voxFoundUndoRedo(bool undo, nlohmann::json& r);
    // F5.3 bodies lab (POST /api/debug/voxfound_bodies, editor/src/VoxFoundBodiesLab.cpp)
    struct VoxFoundBodiesLab {
        Core::VoxFound::VoxelGrid world;                    // what stays in the world (the sim holds a reference)
        std::vector<Core::VoxFound::FractureMaterial> mats;
        std::unique_ptr<Core::VoxFound::BodySimRef> sim;
        Core::VoxFound::BodyDrawList draw;                  // what the renderer draws (setVoxFoundBodies)
        glm::dvec3 areaOriginM{0.0};                        // world metres of grid voxel (0,0,0)
        std::string rigName;
        size_t crumbs = 0, refused = 0, previewFaces = 0;
        long tick = 0;
        double accum = 0.0, lastStepMs = 0.0;
        bool running = false, asWorld = false;
        // F5.4 rest / bake-in (D6): the policy runs after every tick when restOn (GPU path: after every read)
        // (each body's face range lives in draw.first / draw.count -- F5.5b's cull reads it; compacted on a bake)
        std::vector<size_t> crumbMat;                       // crumbs per material (the conservation ledger)
        Core::VoxFound::RestParams rest;
        bool restOn = false;
        Core::VoxFound::RestStats restTotal, restLast;
        double lastBakeMs = 0.0, lastRebuildMs = 0.0, lastCompactMs = 0.0;
        size_t bakePasses = 0;
        // F5.5b: the GPU solver (the shipped path, default) -- sim then only HOLDS the bodies + world for the policy and
        // the measuring functions; poses come from BodySimGpu one frame behind the solve (stepBegin / stepPoll)
        std::unique_ptr<Core::VoxFound::BodySimGpu> gpu;
        bool useGpu = true, gpuLoaded = false;
        size_t reloads = 0;
        double lastReloadMs = 0.0, maxReloadMs = 0.0, gpuMsPerTick = 0.0;
        std::vector<float> simCpuMs;                        // the solver path's CPU ms per frame, last 600 frames (ring)
        size_t simCpuNext = 0;
        std::chrono::steady_clock::time_point runStart{};   // wall clock + tick when "run" last turned on
        long runStartTick = 0;
        // F5.5c: the bake levers -- the preview kept as a RegionEdit and edited per baked body ({"preview":"region"},
        // default; "area" = F5.5b's whole-area build), the GPU solver edited instead of reloaded ({"gpu_edit":bool})
        bool regionPreview = true, gpuEdit = true, lastRebuildIncremental = false;
        glm::ivec3 originVoxel{0};                          // world 1/8 m voxel of grid voxel (0,0,0)
        std::vector<uint16_t> leafTex;                      // assemble's sprig texture per material (no sprigs here)
        size_t regionChunks = 0, fullRebuilds = 0, incrementalPasses = 0, gpuEdits = 0;
        double lastHitchMs = 0.0, maxHitchMs = 0.0, maxEditMs = 0.0, lastGpuEditMs = 0.0;
        std::vector<float> hitchMs;                         // every baking pass's whole rest call (since the rig)
        std::vector<float> frameMs;                         // the frame interval while running (ring of 3,600)
        size_t frameNext = 0;
        // F5.5d: the exit's measures
        bool ledgerEvery = false;                           // {"ledger_every":true}: the ledger after every rest call
        std::vector<size_t> ledger0;                        // per material at the rig's setup (world + bodies + crumbs)
        size_t ledgerChecks = 0, ledgerBad = 0, wakeAlls = 0;
        long ledgerFirstBadTick = -1;
        double ledgerMsMax = 0.0;                           // the check's own cost (not in the hitch)
        std::vector<float> restIdleMs;                      // rest calls that baked nothing (ring of 3,600)
        size_t restIdleNext = 0;
        size_t pollsDone = 0, pollsWaiting = 0;             // live GPU polls that found the step done / still running
        std::chrono::steady_clock::time_point stepSubmitAt{};
        std::vector<float> stepLatencyMs;                   // submit -> found done, wall ms (ring of 600)
        size_t stepLatencyNext = 0;
        double frameMaxMs = 0.0, frameMaxAtS = -1.0;        // the largest frame interval of the run and when (s)
        // F6.5: rubble (the crumbs, F6.3) and poured water in the lab's world (WorldFluid, F6.2, the CPU reference)
        std::unique_ptr<Core::VoxFound::WorldFluid> fluid;  // attached to `world`
        bool fluidDisplay = true;                           // false = the red: the fill steps, the preview is not edited
        double fluidAccum = 0.0;
        long fluidQuietTick = -1;                           // the first tick (since the run) the fluid was quiet twice
        bool fluidPrevQuiet = false;
        std::vector<uint8_t> fluidDrawn;                    // per lab voxel: the material drawn for its fill (0 = none)
        std::vector<float> fluidMs;                         // fluid step + display edit per frame that ticked (ring)
        size_t fluidNext = 0, fluidEdits = 0, fluidCellsChanged = 0;
        double fluidMaxMs = 0.0;
        // F7.5: the "world" rig -- the lab runs on a window of the switched region (no region rebuild; every change
        // goes into the region with applyCells at originVoxel)
        bool worldMode = false;
        glm::ivec3 worldWindowSize{0};
        size_t worldRemoved = 0;
    } voxFoundBodiesLab;
    std::vector<size_t> voxFoundBodiesLedger() const;   // F5.4/F5.5d: per material world + bodies + crumbs
    bool voxFoundBodiesBuildRegion(const Core::VoxFound::VoxelGrid& g, std::string* err);
    bool voxFoundBodiesGpuLoad(std::string* err);
    void voxFoundBodiesGpuStep(float dt);
    void voxFoundFluidStep(float dt);                         // F6.5
    size_t voxFoundFluidEdit(bool all);                       // F6.5: the display rule into the preview; cells changed
    size_t voxFoundFluidCheck() const;                        // F6.5: drawn vs the rule, mismatching voxels
    void voxFoundOccBump(const std::vector<size_t>& rebuiltChunks);   // F7.3b/F7.5: light occupancy revisions
    bool voxFoundWorldBreak(const glm::vec3& centre, float radius, float energy, const glm::vec3& dir,
                            nlohmann::json* report);           // F7.5: DamageSystem's override on the switched world
    void voxFoundBodiesGpuDone(int ticks);
    bool voxFoundBodiesRest(bool force, const Core::VoxFound::RestParams* override, std::string* err);
    bool voxFoundBodiesRestPass(bool force, const Core::VoxFound::RestParams* override, std::string* err);
    void handleVoxFoundBodies(const Core::APICommand& cmd, nlohmann::json& response);
    void stepVoxFoundBodies(float dt);
    void voxFoundBodiesPublish();
    bool voxFoundBodiesRebuildWorld(std::string* err);
    void handleVoxFoundPreview(const Core::APICommand& cmd, nlohmann::json& response);   // F2.2 1/8 m preview
    void handleVoxFoundRegion(const Core::APICommand& cmd, int generationType, const std::string& typeName,
                              uint32_t seed, nlohmann::json& response);   // F2.8 (type = WorldGenerator::GenerationType)
    void handleVoxFoundEdit(const nlohmann::json& edit, nlohmann::json& response);   // F2.16 incremental rebuild
    /// F3.3: the region palette index of a material (appended with its textures and card / see-through sets when
    /// new); -1 + `err` for an unknown material or a full palette. "air" / "" = 0.
    int voxFoundMaterial(const std::string& name, std::string* err);
    void handleVoxFoundSave(nlohmann::json& response);   // F3.3: the edited chunks, codec v3, world.db
    nlohmann::json loadVoxFoundSaved(bool fromDb = true);   // F7.9b: false = the stream cache only                  // F3.3: saved chunks into the editable region
};

}  // namespace Phyxel
