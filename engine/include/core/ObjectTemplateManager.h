#pragma once

#include "VoxelTemplate.h"
#include "PlacedObjectManager.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <deque>
#include <glm/glm.hpp>

namespace Phyxel {

class ChunkManager;
class DynamicObjectManager;
class WorldGenerator;
class Chunk;
namespace Core { class KinematicVoxelManager; }
namespace Core { class KinematicAnimator; }

struct PendingSpawn {
    std::string templateName;
    glm::vec3 worldPos;
    bool isStatic;
    
    // Progress tracking
    size_t currentCubeIndex = 0;
    size_t currentSubcubeIndex = 0;
    size_t currentMicrocubeIndex = 0;
    
    // To avoid looking up the template every frame
    const VoxelTemplate* templatePtr = nullptr; 
};

class ObjectTemplateManager {
public:
    ObjectTemplateManager(ChunkManager* chunkMgr, DynamicObjectManager* dynamicMgr);
    ObjectTemplateManager(const ObjectTemplateManager&) = delete;
    ObjectTemplateManager& operator=(const ObjectTemplateManager&) = delete;
    ~ObjectTemplateManager() = default;

    // Load all templates from resources/templates directory
    void loadTemplates(const std::string& directoryPath);
    
    // Load a specific template file. registryName overrides the registry key
    // (default: the file stem). Item templates register under their relative
    // path ("items/torch") so a root-level template with the same stem can
    // never shadow them — the silent-substitution bug of 2026-08-06.
    bool loadTemplate(const std::string& filePath, const std::string& registryName = "");

    // Get a template by name
    const VoxelTemplate* getTemplate(const std::string& name) const;

    /// True when the template may be baked into static chunk geometry.
    /// Fine-grid templates (finer than microcube) are kinematic-only: the
    /// chunk store's 9-per-cube micro grid cannot represent their cells, so
    /// every static-bake entry point must consult this and refuse loudly
    /// rather than silently downsampling.
    static bool canBakeStatic(const VoxelTemplate& tmpl);

    // Get all loaded template names
    std::vector<std::string> getTemplateNames() const;

    /// Get the canonical facing yaw (radians) for a template.
    /// Returns 0.0f if the template is not loaded or has no facing_yaw header.
    float getTemplateFacingYaw(const std::string& name) const;

    // Spawn a template at a specific world position
    // isStatic: if true, merges into chunks. if false, creates dynamic objects.
    // rotation: 0, 90, 180, or 270 degrees clockwise around Y axis
    bool spawnTemplate(const std::string& name, const glm::vec3& worldPos, bool isStatic = true, int rotation = 0);

    /**
     * @brief Bake a STATIC template at a MICRO-precise world position (sub-cube), re-rasterizing
     *        every voxel to microcubes shifted by the sub-cube remainder.
     *
     * Furniture is positioned on the micro grid (1 cube = 9 micro), so a piece can sit FLUSH against
     * a thin sub-cube wall (the outer ~3 micro of a perimeter cube) and exactly on the mid-cube
     * walkable surface — instead of clipping the wall / sinking into the floor as whole-cube
     * `spawnTemplate` does (it `round()`s to a cube). `worldMicro` = cube*9 + micro (per axis).
     * Whole-microcube shifts are exact (no resampling). Rotation is in 90-deg steps, applied in micro
     * space. STATIC bake only (no kinematic parts) — for furniture/fixtures. Trees/props keep
     * `spawnTemplate` (cheaper, cube-aligned). Returns false if the template is missing.
     */
    bool spawnTemplateMicro(const std::string& name, const glm::ivec3& worldMicro, int rotation = 0);

    // Flora decoration: ask the generator to plan biome-appropriate vegetation across a world-
    // column rectangle, then stamp each plant (centering its footprint on the sampled column).
    // All region chunks must already exist so overhang routes into the correct neighbor (no
    // seams). Returns the number of plants placed. See WorldGenerator::planFlora.
    int decorateFlora(WorldGenerator& generator, int colMinX, int colMinZ, int colMaxX, int colMaxZ);

    // Per-chunk flora decoration for streaming/procedural worlds: stamps only the voxels that
    // fall within THIS chunk (a tree rooted in a neighbor that overhangs in is clipped, and the
    // neighbor stamps its own share — placement is order-independent so they agree, no seams,
    // no phantom neighbor chunks). Call right after a newly generated chunk's terrain is filled.
    void decorateChunk(Chunk& chunk, const glm::ivec3& chunkCoord, WorldGenerator& generator);

    /**
     * @brief Spawns a template sequentially over multiple frames to avoid frame drops.
     * 
     * This method queues the spawn operation. The actual voxel placement happens in the update() loop,
     * processing a limited number of voxels per frame defined by m_voxelsPerFrame.
     * 
     * @param name Name of the template to spawn
     * @param worldPos World position to spawn at
     * @param isStatic If true, merges into chunks (creating them if needed). If false, creates dynamic physics objects.
     */
    void spawnTemplateSequentially(const std::string& name, const glm::vec3& worldPos, bool isStatic = true);

    /**
     * @brief Updates the pending spawn queue.
     * 
     * Should be called once per frame. Processes a batch of voxels for the current pending spawn.
     * 
     * @param deltaTime Time since last frame
     */
    void update(float deltaTime);

    // Configuration
    void setSpawnSpeed(int voxelsPerFrame) { m_voxelsPerFrame = voxelsPerFrame; }
    int getSpawnSpeed() const { return m_voxelsPerFrame; }

    /// Phase C0b: wire the kinematic voxel manager so spawnTemplate can route
    /// voxels belonging to movable parts (declared via `# part: ... hinge=...`)
    /// into KinematicVoxelObjects instead of baking them into chunks. Nullptr
    /// is allowed and preserves the legacy behavior (everything baked).
    void setKinematicVoxelManager(Core::KinematicVoxelManager* mgr) { m_kinematicManager = mgr; }
    Core::KinematicVoxelManager* getKinematicVoxelManager() const { return m_kinematicManager; }

    /// Phase C: optional animator. When wired, every movable part emitted by
    /// spawnTemplate is auto-registered with hinge/axis/baseRotation metadata
    /// so callers can drive open/close (or slide) without manual bookkeeping.
    /// Nullptr leaves Phase C0b behavior intact (spawn-only, no animation).
    void setKinematicAnimator(Core::KinematicAnimator* anim) { m_animator = anim; }
    Core::KinematicAnimator* getKinematicAnimator() const { return m_animator; }

    /// IDs returned by the most recent spawnTemplate() call for each movable
    /// part that was routed to the kinematic manager. Empty when the template
    /// has no movable parts. Caller is expected to consume this immediately
    /// after spawnTemplate() (e.g. to write the IDs into PlacedObject::metadata).
    const std::vector<std::string>& lastSpawnedKinematicIds() const { return m_lastSpawnedKinematicIds; }

    /// Get the absolute file path for a loaded template (empty if not found).
    std::string getTemplatePath(const std::string& name) const;

    /// Column margin decorateChunk inflates its planFlora window by, so a plant rooted in a
    /// neighbor chunk still stamps its overhang into this one. Driven by the widest loaded
    /// template's half-footprint (clamped to [12, kFloraMarginCap]) — a fixed 12 silently clipped
    /// any canopy wider than 12 cubes at chunk seams. See docs/ProceduralTreeExpansionPlan.md B.
    int floraMarginColumns() const { return m_floraMarginColumns; }
    // Canopy width ceiling (columns). A wider template's canopy would clip at chunk seams. Raised
    // to 40 for broad enchanted-forest world-trees (was 24). Cost: decorateChunk's planFlora window
    // inflates to chunk±40 — hash-cheap, no correctness impact.
    static constexpr int kFloraMarginCap = 40;

    /// Save interaction point definitions back to the template's .txt file.
    /// Replaces or appends "# interaction:" metadata lines.
    bool saveInteractionDefs(const std::string& templateName,
                             const std::vector<Core::InteractionPointDef>& defs);

private:
    ChunkManager* m_chunkManager;
    DynamicObjectManager* m_dynamicObjectManager;
    Core::KinematicVoxelManager* m_kinematicManager = nullptr;  // optional, Phase C0b
    Core::KinematicAnimator*     m_animator         = nullptr;  // optional, Phase C
    std::unordered_map<std::string, std::unique_ptr<VoxelTemplate>> m_templates;
    /// Relative-path aliases from the recursive library scan
    /// ("items/torch" -> "torch") so path-qualified references resolve
    /// without a second lazy load. See loadTemplates.
    std::unordered_map<std::string, std::string> m_aliases;

    // Widest half-footprint (columns) over all loaded templates, clamped [12, kFloraMarginCap].
    // Updated in loadTemplate; consumed by decorateChunk as the planFlora window inflation.
    int m_floraMarginColumns = 12;

    // Sequential spawning queue
    std::deque<PendingSpawn> m_pendingSpawns;
    int m_voxelsPerFrame = 200; // Adjust based on performance needs

    // Track kinematic objects emitted by the most recent spawnTemplate call so
    // callers can wire them into PlacedObject metadata.
    std::vector<std::string> m_lastSpawnedKinematicIds;

    // Helper to parse a line from the template file
    void parseLine(const std::string& line, VoxelTemplate& tmpl);

    // Max column overhang of a template from its stamp anchor (see .cpp). Drives the flora margin.
    static int templateFootprintRadius(const VoxelTemplate& t);
};

} // namespace Phyxel
