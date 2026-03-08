#pragma once

#include "VoxelTemplate.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <deque>
#include <glm/glm.hpp>

namespace Phyxel {

class ChunkManager;
class DynamicObjectManager;

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

    // Spawn a template at a specific world position
    // isStatic: if true, merges into chunks. if false, creates dynamic objects.
    bool spawnTemplate(const std::string& name, const glm::vec3& worldPos, bool isStatic = true);

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

private:
    ChunkManager* m_chunkManager;
    DynamicObjectManager* m_dynamicObjectManager;
    std::unordered_map<std::string, std::unique_ptr<VoxelTemplate>> m_templates;

    // Sequential spawning queue
    std::deque<PendingSpawn> m_pendingSpawns;
    int m_voxelsPerFrame = 200; // Adjust based on performance needs

    // Helper to parse a line from the template file
    void parseLine(const std::string& line, VoxelTemplate& tmpl);
};

} // namespace Phyxel
