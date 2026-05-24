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
    
    // Load a specific template file
    bool loadTemplate(const std::string& filePath);

    // Get a template by name
    const VoxelTemplate* getTemplate(const std::string& name) const;

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

    // Sequential spawning queue
    std::deque<PendingSpawn> m_pendingSpawns;
    int m_voxelsPerFrame = 200; // Adjust based on performance needs

    // Track kinematic objects emitted by the most recent spawnTemplate call so
    // callers can wire them into PlacedObject metadata.
    std::vector<std::string> m_lastSpawnedKinematicIds;

    // Helper to parse a line from the template file
    void parseLine(const std::string& line, VoxelTemplate& tmpl);
};

} // namespace Phyxel
