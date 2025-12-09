#pragma once

#include "VoxelTemplate.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>

namespace VulkanCube {

class ChunkManager;
class DynamicObjectManager;

class ObjectTemplateManager {
public:
    ObjectTemplateManager(ChunkManager* chunkMgr, DynamicObjectManager* dynamicMgr);
    ~ObjectTemplateManager() = default;

    // Load all templates from resources/templates directory
    void loadTemplates(const std::string& directoryPath);
    
    // Load a specific template file
    bool loadTemplate(const std::string& filePath);

    // Get a template by name
    const VoxelTemplate* getTemplate(const std::string& name) const;

    // Spawn a template at a specific world position
    // isStatic: if true, merges into chunks. if false, creates dynamic objects.
    bool spawnTemplate(const std::string& name, const glm::vec3& worldPos, bool isStatic = true);

private:
    ChunkManager* m_chunkManager;
    DynamicObjectManager* m_dynamicObjectManager;
    std::unordered_map<std::string, std::unique_ptr<VoxelTemplate>> m_templates;

    // Helper to parse a line from the template file
    void parseLine(const std::string& line, VoxelTemplate& tmpl);
};

} // namespace VulkanCube
