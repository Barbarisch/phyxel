#include "core/ObjectTemplateManager.h"
#include "core/ChunkManager.h"
#include "core/DynamicObjectManager.h"
#include "core/PlacedObjectManager.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "physics/PhysicsWorld.h"
#include "utils/CoordinateUtils.h"
#include "utils/Logger.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <unordered_set>

namespace Phyxel {

namespace fs = std::filesystem;

ObjectTemplateManager::ObjectTemplateManager(ChunkManager* chunkMgr, DynamicObjectManager* dynamicMgr)
    : m_chunkManager(chunkMgr), m_dynamicObjectManager(dynamicMgr) {
}

void ObjectTemplateManager::loadTemplates(const std::string& directoryPath) {
    if (!fs::exists(directoryPath)) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template directory does not exist: " << directoryPath);
        return;
    }

    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".voxel") {
                loadTemplate(entry.path().string());
            } else if (ext == ".txt") {
                // Backward compatibility: load legacy .txt templates with deprecation warning
                LOG_WARN_FMT("ObjectTemplateManager",
                    "Template '" << entry.path().filename().string()
                    << "' uses legacy .txt extension — rename to .voxel");
                loadTemplate(entry.path().string());
            }
        }
    }
}

bool ObjectTemplateManager::loadTemplate(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Failed to open template file: " << filePath);
        return false;
    }

    auto tmpl = std::make_unique<VoxelTemplate>();
    tmpl->name = fs::path(filePath).stem().string();
    tmpl->sourceFilePath = fs::absolute(filePath).string();

    std::string line;
    while (std::getline(file, line)) {
        parseLine(line, *tmpl);
    }

    LOG_INFO_FMT("ObjectTemplateManager", "Loaded template: " << tmpl->name << " with " 
                 << tmpl->cubes.size() << " cubes, " 
                 << tmpl->subcubes.size() << " subcubes, " 
                 << tmpl->microcubes.size() << " microcubes, "
                 << tmpl->interactionPoints.size() << " interaction points");

    m_templates[tmpl->name] = std::move(tmpl);
    return true;
}

float ObjectTemplateManager::getTemplateFacingYaw(const std::string& name) const {
    auto it = m_templates.find(name);
    if (it == m_templates.end()) return 0.0f;
    return it->second->facingYaw;
}

void ObjectTemplateManager::parseLine(const std::string& line, VoxelTemplate& tmpl) {
    if (line.empty()) return;

    // Parse metadata headers
    if (line[0] == '#') {
        // Check for "# facing_yaw: X.XXX"
        const std::string facingKey = "# facing_yaw:";
        if (line.compare(0, facingKey.size(), facingKey) == 0) {
            try {
                tmpl.facingYaw = std::stof(line.substr(facingKey.size()));
            } catch (...) {}
            return;
        }

        // New format: "# interaction_point: pointId type localX localY localZ facingYaw group1,group2,..."
        // Optional trailing fields: radius promptText viewAngle
        // Only asset-level data; per-archetype offsets live in JSON profiles.
        const std::string interactionPointKey = "# interaction_point:";
        if (line.compare(0, interactionPointKey.size(), interactionPointKey) == 0) {
            std::istringstream iss(line.substr(interactionPointKey.size()));
            Core::InteractionPointDef def;
            std::string groupsStr;
            iss >> def.pointId >> def.type
                >> def.localOffset.x >> def.localOffset.y >> def.localOffset.z
                >> def.facingYaw >> groupsStr;
            if (!iss.fail()) {
                // Parse comma-separated supported groups
                if (!groupsStr.empty() && groupsStr != "*") {
                    std::istringstream groupStream(groupsStr);
                    std::string group;
                    while (std::getline(groupStream, group, ',')) {
                        if (!group.empty()) def.supportedGroups.push_back(group);
                    }
                }
                // Optional: radius
                float radius = 0.0f;
                if (iss >> radius) {
                    def.interactionRadius = radius;
                }
                // Optional: promptText (quoted string)
                std::string prompt;
                if (iss >> std::ws && iss.peek() == '"') {
                    iss.get(); // consume opening quote
                    std::getline(iss, prompt, '"');
                    def.promptText = prompt;
                }
                // Optional: viewAngle
                float viewAngle = 0.0f;
                if (iss >> viewAngle) {
                    def.viewAngleHalf = viewAngle;
                }
                tmpl.interactionPoints.push_back(def);
            } else {
                LOG_WARN_FMT("ObjectTemplateManager", "Failed to parse interaction_point line: " << line);
            }
            return;
        }

        // Legacy format: "# interaction: pointId type localX localY localZ facingYaw sitDownXYZ sittingIdleXYZ sitStandUpXYZ blendDuration seatHeightOffset"
        const std::string interactionKey = "# interaction:";
        if (line.compare(0, interactionKey.size(), interactionKey) == 0) {
            std::istringstream iss(line.substr(interactionKey.size()));
            Core::InteractionPointDef def;
            iss >> def.pointId >> def.type
                >> def.localOffset.x >> def.localOffset.y >> def.localOffset.z
                >> def.facingYaw
                >> def.sitDownOffset.x >> def.sitDownOffset.y >> def.sitDownOffset.z
                >> def.sittingIdleOffset.x >> def.sittingIdleOffset.y >> def.sittingIdleOffset.z
                >> def.sitStandUpOffset.x >> def.sitStandUpOffset.y >> def.sitStandUpOffset.z
                >> def.sitBlendDuration >> def.seatHeightOffset;
            if (!iss.fail()) {
                tmpl.interactionPoints.push_back(def);
            } else {
                LOG_WARN_FMT("ObjectTemplateManager", "Failed to parse interaction line: " << line);
            }
            return;
        }

        return;
    }

    std::stringstream ss(line);
    char type;
    ss >> type;

    if (type == 'C') {
        int x, y, z;
        std::string mat;
        ss >> x >> y >> z >> mat;
        tmpl.addCube({x, y, z}, mat);
    } else if (type == 'S') {
        int px, py, pz, sx, sy, sz;
        std::string mat;
        ss >> px >> py >> pz >> sx >> sy >> sz >> mat;
        tmpl.addSubcube({px, py, pz}, {sx, sy, sz}, mat);
    } else if (type == 'M') {
        int px, py, pz, sx, sy, sz, mx, my, mz;
        std::string mat;
        ss >> px >> py >> pz >> sx >> sy >> sz >> mx >> my >> mz >> mat;
        tmpl.addMicrocube({px, py, pz}, {sx, sy, sz}, {mx, my, mz}, mat);
    }
}

const VoxelTemplate* ObjectTemplateManager::getTemplate(const std::string& name) const {
    auto it = m_templates.find(name);
    if (it != m_templates.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<std::string> ObjectTemplateManager::getTemplateNames() const {
    std::vector<std::string> names;
    names.reserve(m_templates.size());
    for (const auto& [name, tmpl] : m_templates) {
        names.push_back(name);
    }
    return names;
}

bool ObjectTemplateManager::spawnTemplate(const std::string& name, const glm::vec3& worldPos, bool isStatic, int rotation) {
    const VoxelTemplate* tmpl = getTemplate(name);
    if (!tmpl) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template not found: " << name);
        return false;
    }

    // Normalize rotation to number of 90° steps
    int rotSteps = ((rotation % 360) + 360) % 360 / 90;

    // Compute bounding box of template for rotation pivot
    glm::ivec3 maxExtent(0);
    if (rotSteps > 0) {
        for (const auto& c : tmpl->cubes) {
            maxExtent = glm::max(maxExtent, c.relativePos);
        }
        for (const auto& s : tmpl->subcubes) {
            maxExtent = glm::max(maxExtent, s.parentRelativePos);
        }
        for (const auto& m : tmpl->microcubes) {
            maxExtent = glm::max(maxExtent, m.parentRelativePos);
        }
    }

    // Rotate a block-level offset around Y axis (keeps all offsets non-negative)
    auto rotateOffset = [&](glm::ivec3 pos) -> glm::ivec3 {
        switch (rotSteps) {
            case 1: return glm::ivec3(maxExtent.z - pos.z, pos.y, pos.x);                           // 90° CW
            case 2: return glm::ivec3(maxExtent.x - pos.x, pos.y, maxExtent.z - pos.z);             // 180°
            case 3: return glm::ivec3(pos.z, pos.y, maxExtent.x - pos.x);                           // 270° CW
            default: return pos;
        }
    };

    // Rotate a sub-grid local position (0-2 range) around Y axis
    auto rotateLocal = [&](glm::ivec3 lp) -> glm::ivec3 {
        switch (rotSteps) {
            case 1: return glm::ivec3(2 - lp.z, lp.y, lp.x);
            case 2: return glm::ivec3(2 - lp.x, lp.y, 2 - lp.z);
            case 3: return glm::ivec3(lp.z, lp.y, 2 - lp.x);
            default: return lp;
        }
    };

    glm::ivec3 basePos = glm::round(worldPos);
    std::unordered_set<Chunk*> modifiedChunks;

    // Helper: get-or-create chunk and enable bulk physics mode on first touch
    auto getOrCreateChunk = [&](const glm::ivec3& worldBlockPos) -> Chunk* {
        glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(worldBlockPos);
        auto it = m_chunkManager->chunkMap.find(chunkCoord);
        if (it == m_chunkManager->chunkMap.end()) {
            glm::ivec3 origin = chunkCoord * 32;
            m_chunkManager->createChunk(origin, false);
            it = m_chunkManager->chunkMap.find(chunkCoord);
        }
        if (it == m_chunkManager->chunkMap.end()) return nullptr;
        Chunk* chunk = it->second;
        if (modifiedChunks.insert(chunk).second) {
            chunk->setPhysicsBulkMode(true);
        }
        return chunk;
    };

    // Spawn Cubes
    for (const auto& tCube : tmpl->cubes) {
        glm::ivec3 pos = basePos + rotateOffset(tCube.relativePos);
        glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(pos);
        if (Chunk* chunk = getOrCreateChunk(pos)) {
            chunk->addCube(localPos);
        }
    }

    // Spawn Subcubes
    for (const auto& tSub : tmpl->subcubes) {
        glm::ivec3 parentPos = basePos + rotateOffset(tSub.parentRelativePos);
        glm::ivec3 subPos = rotateLocal(tSub.subcubePos);
        glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
        if (Chunk* chunk = getOrCreateChunk(parentPos)) {
            chunk->addSubcube(localPos, subPos, tSub.material);
        }
    }

    // Spawn Microcubes
    for (const auto& tMicro : tmpl->microcubes) {
        glm::ivec3 parentPos = basePos + rotateOffset(tMicro.parentRelativePos);
        glm::ivec3 subPos = rotateLocal(tMicro.subcubePos);
        glm::ivec3 microPos = rotateLocal(tMicro.microcubePos);
        glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
        if (Chunk* chunk = getOrCreateChunk(parentPos)) {
            chunk->addMicrocube(localPos, subPos, microPos, tMicro.material);
        }
    }

    // Finalise all modified chunks: flush deferred collisions, rebuild faces
    for (Chunk* chunk : modifiedChunks) {
        chunk->batchUpdateCollisions();
        chunk->setPhysicsBulkMode(false);
        chunk->rebuildFaces();
        chunk->updateVulkanBuffer();
    }

    return true;
}

void ObjectTemplateManager::spawnTemplateSequentially(const std::string& name, const glm::vec3& worldPos, bool isStatic) {
    const VoxelTemplate* tmpl = getTemplate(name);
    if (!tmpl) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template not found: " << name);
        return;
    }

    // Create a new pending spawn task
    PendingSpawn spawn;
    spawn.templateName = name;
    spawn.worldPos = worldPos;
    spawn.isStatic = isStatic;
    spawn.templatePtr = tmpl;
    
    m_pendingSpawns.push_back(spawn);
    LOG_INFO_FMT("ObjectTemplateManager", "Queued sequential spawn for template: " << name);
}

void ObjectTemplateManager::update(float deltaTime) {
    if (m_pendingSpawns.empty()) return;

    // Process the first spawn in the queue
    PendingSpawn& spawn = m_pendingSpawns.front();
    const VoxelTemplate* tmpl = spawn.templatePtr;
    
    if (!tmpl) {
        m_pendingSpawns.pop_front();
        return;
    }

    glm::ivec3 basePos = glm::round(spawn.worldPos);
    std::unordered_set<Chunk*> modifiedChunks;
    int processedVoxels = 0;

    // ---------------------------------------------------------
    // Process Cubes Batch
    // ---------------------------------------------------------
    while (spawn.currentCubeIndex < tmpl->cubes.size() && processedVoxels < m_voxelsPerFrame) {
        const auto& tCube = tmpl->cubes[spawn.currentCubeIndex];
        glm::ivec3 pos = basePos + tCube.relativePos;
        
        if (spawn.isStatic) {
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(pos);
            glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(pos);

            Chunk* chunk = nullptr;
            auto it = m_chunkManager->chunkMap.find(chunkCoord);
            if (it != m_chunkManager->chunkMap.end()) {
                chunk = it->second;
            } else {
                // AUTO-CREATE CHUNK:
                // If the template extends into a chunk that doesn't exist yet (e.g. high in the air),
                // we must create it to avoid losing voxels.
                glm::ivec3 origin = chunkCoord * 32;
                m_chunkManager->createChunk(origin, false);

                it = m_chunkManager->chunkMap.find(chunkCoord);
                if (it != m_chunkManager->chunkMap.end()) {
                    chunk = it->second;
                }
            }

            if (chunk) {
                if (chunk->addCube(localPos)) {
                    modifiedChunks.insert(chunk);
                }
            }
        }

        spawn.currentCubeIndex++;
        processedVoxels++;
    }

    // ---------------------------------------------------------
    // Process Subcubes Batch
    // ---------------------------------------------------------
    while (spawn.currentSubcubeIndex < tmpl->subcubes.size() && processedVoxels < m_voxelsPerFrame) {
        const auto& tSub = tmpl->subcubes[spawn.currentSubcubeIndex];
        glm::ivec3 parentPos = basePos + tSub.parentRelativePos;
        
        if (spawn.isStatic) {
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(parentPos);
            glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
            
            Chunk* chunk = nullptr;
            auto it = m_chunkManager->chunkMap.find(chunkCoord);
            if (it != m_chunkManager->chunkMap.end()) {
                chunk = it->second;
            } else {
                 glm::ivec3 origin = chunkCoord * 32;
                m_chunkManager->createChunk(origin, false);
                it = m_chunkManager->chunkMap.find(chunkCoord);
                if (it != m_chunkManager->chunkMap.end()) {
                    chunk = it->second;
                }
            }

            if (chunk) {
                if (chunk->addSubcube(localPos, tSub.subcubePos, tSub.material)) {
                    modifiedChunks.insert(chunk);
                }
            }
        }

        spawn.currentSubcubeIndex++;
        processedVoxels++;
    }

    // ---------------------------------------------------------
    // Process Microcubes Batch
    // ---------------------------------------------------------
    while (spawn.currentMicrocubeIndex < tmpl->microcubes.size() && processedVoxels < m_voxelsPerFrame) {
        const auto& tMicro = tmpl->microcubes[spawn.currentMicrocubeIndex];
        glm::ivec3 parentPos = basePos + tMicro.parentRelativePos;
        
        if (spawn.isStatic) {
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(parentPos);
            glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
            
            Chunk* chunk = nullptr;
            auto it = m_chunkManager->chunkMap.find(chunkCoord);
            if (it != m_chunkManager->chunkMap.end()) {
                chunk = it->second;
            } else {
                 glm::ivec3 origin = chunkCoord * 32;
                m_chunkManager->createChunk(origin, false);
                it = m_chunkManager->chunkMap.find(chunkCoord);
                if (it != m_chunkManager->chunkMap.end()) {
                    chunk = it->second;
                }
            }

            if (chunk) {
                if (chunk->addMicrocube(localPos, tMicro.subcubePos, tMicro.microcubePos, tMicro.material)) {
                    modifiedChunks.insert(chunk);
                }
            }
        }

        spawn.currentMicrocubeIndex++;
        processedVoxels++;
    }

    // Update modified chunks immediately so the user sees the progress
    for (Chunk* chunk : modifiedChunks) {
        chunk->rebuildFaces();
        chunk->updateVulkanBuffer();
    }

    // Check if done
    if (spawn.currentCubeIndex >= tmpl->cubes.size() &&
        spawn.currentSubcubeIndex >= tmpl->subcubes.size() &&
        spawn.currentMicrocubeIndex >= tmpl->microcubes.size()) {
        
        m_pendingSpawns.pop_front();
        LOG_INFO("ObjectTemplateManager", "Finished sequential spawn");
    }
}

std::string ObjectTemplateManager::getTemplatePath(const std::string& name) const {
    auto it = m_templates.find(name);
    if (it == m_templates.end()) return "";
    return it->second->sourceFilePath;
}

bool ObjectTemplateManager::saveInteractionDefs(const std::string& templateName,
                                                 const std::vector<Core::InteractionPointDef>& defs) {
    std::string filePath = getTemplatePath(templateName);
    if (filePath.empty()) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Cannot save interaction defs: template '" << templateName << "' not found or has no source path");
        return false;
    }

    // Read the existing file
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Cannot open template file for reading: " << filePath);
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(inFile, line)) {
        // Skip existing interaction lines (both legacy and new format) — we'll rewrite them
        if (line.compare(0, 14, "# interaction:") == 0)
            continue;
        if (line.compare(0, 20, "# interaction_point:") == 0)
            continue;
        lines.push_back(line);
    }
    inFile.close();

    // Find insertion point: after other "# " metadata lines, before voxel data
    size_t insertIdx = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!lines[i].empty() && lines[i][0] == '#') {
            insertIdx = i + 1;
        } else if (!lines[i].empty()) {
            break; // Hit voxel data
        }
    }

    // Build interaction_point lines (new format: asset-level only)
    std::vector<std::string> interactionLines;
    for (const auto& def : defs) {
        // Build supported groups string
        std::string groupsStr = "*";
        if (!def.supportedGroups.empty()) {
            groupsStr.clear();
            for (size_t i = 0; i < def.supportedGroups.size(); ++i) {
                if (i > 0) groupsStr += ",";
                groupsStr += def.supportedGroups[i];
            }
        }
        char buf[512];
        int len = std::snprintf(buf, sizeof(buf),
            "# interaction_point: %s %s %.4f %.4f %.4f %.6f %s",
            def.pointId.c_str(), def.type.c_str(),
            def.localOffset.x, def.localOffset.y, def.localOffset.z,
            def.facingYaw, groupsStr.c_str());
        // Append optional fields if non-default
        if (def.interactionRadius > 0.0f || !def.promptText.empty() || def.viewAngleHalf > 0.0f) {
            len += std::snprintf(buf + len, sizeof(buf) - len, " %.2f", def.interactionRadius);
        }
        if (!def.promptText.empty() || def.viewAngleHalf > 0.0f) {
            len += std::snprintf(buf + len, sizeof(buf) - len, " \"%s\"", def.promptText.c_str());
        }
        if (def.viewAngleHalf > 0.0f) {
            len += std::snprintf(buf + len, sizeof(buf) - len, " %.1f", def.viewAngleHalf);
        }
        interactionLines.push_back(buf);
    }

    // Insert interaction lines
    lines.insert(lines.begin() + static_cast<int>(insertIdx),
                 interactionLines.begin(), interactionLines.end());

    // Write back
    std::ofstream outFile(filePath);
    if (!outFile.is_open()) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Cannot open template file for writing: " << filePath);
        return false;
    }
    for (const auto& l : lines) {
        outFile << l << "\n";
    }
    outFile.close();

    // Update the in-memory template's interaction points
    auto it = m_templates.find(templateName);
    if (it != m_templates.end()) {
        it->second->interactionPoints = defs;
    }

    LOG_INFO_FMT("ObjectTemplateManager", "Saved " << defs.size() << " interaction defs to " << filePath);
    return true;
}

} // namespace Phyxel
