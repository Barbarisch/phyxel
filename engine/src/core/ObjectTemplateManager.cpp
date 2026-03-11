#include "core/ObjectTemplateManager.h"
#include "core/ChunkManager.h"
#include "core/DynamicObjectManager.h"
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
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
            loadTemplate(entry.path().string());
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

    std::string line;
    while (std::getline(file, line)) {
        parseLine(line, *tmpl);
    }

    LOG_INFO_FMT("ObjectTemplateManager", "Loaded template: " << tmpl->name << " with " 
                 << tmpl->cubes.size() << " cubes, " 
                 << tmpl->subcubes.size() << " subcubes, " 
                 << tmpl->microcubes.size() << " microcubes");

    m_templates[tmpl->name] = std::move(tmpl);
    return true;
}

void ObjectTemplateManager::parseLine(const std::string& line, VoxelTemplate& tmpl) {
    if (line.empty() || line[0] == '#') return;

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

bool ObjectTemplateManager::spawnTemplate(const std::string& name, const glm::vec3& worldPos, bool isStatic) {
    const VoxelTemplate* tmpl = getTemplate(name);
    if (!tmpl) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template not found: " << name);
        return false;
    }

    glm::ivec3 basePos = glm::round(worldPos);
    std::unordered_set<Chunk*> modifiedChunks;

    // Spawn Cubes
    for (const auto& tCube : tmpl->cubes) {
        glm::ivec3 pos = basePos + tCube.relativePos;
        
        if (isStatic) {
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(pos);
            glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(pos);
            
            Chunk* chunk = nullptr;
            auto it = m_chunkManager->chunkMap.find(chunkCoord);
            if (it != m_chunkManager->chunkMap.end()) {
                chunk = it->second;
            } else {
                // Create new empty chunk if it doesn't exist
                // This ensures templates can be spawned even in empty space/air
                glm::ivec3 origin = chunkCoord * 32;
                m_chunkManager->createChunk(origin, false); // false = empty (no noise generation)
                
                // Retrieve the newly created chunk
                it = m_chunkManager->chunkMap.find(chunkCoord);
                if (it != m_chunkManager->chunkMap.end()) {
                    chunk = it->second;
                }
            }

            // If chunk doesn't exist, we might need to create it or skip
            if (chunk) {
                if (chunk->addCube(localPos)) {
                    modifiedChunks.insert(chunk);
                }
                // Note: Material setting not fully implemented in addCube yet, usually defaults
                // If addCube supported material, we'd pass tCube.material
            }
        } else {
            auto cube = std::make_unique<Cube>(pos, tCube.material);
            
            if (m_chunkManager->physicsWorld) {
                glm::vec3 center = glm::vec3(pos) + glm::vec3(0.5f);
                btRigidBody* body = m_chunkManager->physicsWorld->createBreakawayCube(center, glm::vec3(1.0f), 1.0f);
                cube->setRigidBody(body);
                cube->setPhysicsPosition(center);
            }
            
            m_dynamicObjectManager->addGlobalDynamicCube(std::move(cube));
        }
    }

    // Spawn Subcubes
    for (const auto& tSub : tmpl->subcubes) {
        glm::ivec3 parentPos = basePos + tSub.parentRelativePos;
        
        if (isStatic) {
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(parentPos);
            glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
            
            Chunk* chunk = nullptr;
            auto it = m_chunkManager->chunkMap.find(chunkCoord);
            if (it != m_chunkManager->chunkMap.end()) {
                chunk = it->second;
            } else {
                // Create new empty chunk if it doesn't exist
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
        } else {
            auto subcube = std::make_unique<Subcube>(parentPos, tSub.subcubePos, tSub.material);
            
            if (m_chunkManager->physicsWorld) {
                glm::vec3 corner = subcube->getWorldPosition();
                glm::vec3 size(1.0f/3.0f);
                glm::vec3 center = corner + size * 0.5f;
                
                btRigidBody* body = m_chunkManager->physicsWorld->createBreakawayCube(center, size, 0.5f);
                subcube->setRigidBody(body);
                subcube->setPhysicsPosition(center);
            }
            
            m_dynamicObjectManager->addGlobalDynamicSubcube(std::move(subcube));
        }
    }

    // Spawn Microcubes
    for (const auto& tMicro : tmpl->microcubes) {
        glm::ivec3 parentPos = basePos + tMicro.parentRelativePos;
        
        if (isStatic) {
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(parentPos);
            glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
            
            Chunk* chunk = nullptr;
            auto it = m_chunkManager->chunkMap.find(chunkCoord);
            if (it != m_chunkManager->chunkMap.end()) {
                chunk = it->second;
            } else {
                // Create new empty chunk if it doesn't exist
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
        } else {
            auto microcube = std::make_unique<Microcube>(parentPos, tMicro.subcubePos, tMicro.microcubePos, tMicro.material);
            
            if (m_chunkManager->physicsWorld) {
                glm::vec3 corner = microcube->getWorldPosition();
                glm::vec3 size(1.0f/9.0f);
                glm::vec3 center = corner + size * 0.5f;
                
                btRigidBody* body = m_chunkManager->physicsWorld->createBreakawayCube(center, size, 0.1f);
                microcube->setRigidBody(body);
                microcube->setPhysicsPosition(center);
            }
            
            m_dynamicObjectManager->addGlobalDynamicMicrocube(std::move(microcube));
        }
    }

    // Update all modified chunks to reflect changes
    if (isStatic) {
        for (Chunk* chunk : modifiedChunks) {
            chunk->rebuildFaces();
            chunk->updateVulkanBuffer();
        }
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
        } else {
            // Dynamic logic (same as spawnTemplate)
             auto cube = std::make_unique<Cube>(pos, tCube.material);
            if (m_chunkManager->physicsWorld) {
                glm::vec3 center = glm::vec3(pos) + glm::vec3(0.5f);
                btRigidBody* body = m_chunkManager->physicsWorld->createBreakawayCube(center, glm::vec3(1.0f), 1.0f);
                cube->setRigidBody(body);
                cube->setPhysicsPosition(center);
            }
            m_dynamicObjectManager->addGlobalDynamicCube(std::move(cube));
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
        } else {
             auto subcube = std::make_unique<Subcube>(parentPos, tSub.subcubePos, tSub.material);
            if (m_chunkManager->physicsWorld) {
                glm::vec3 corner = subcube->getWorldPosition();
                glm::vec3 size(1.0f/3.0f);
                glm::vec3 center = corner + size * 0.5f;
                btRigidBody* body = m_chunkManager->physicsWorld->createBreakawayCube(center, size, 0.5f);
                subcube->setRigidBody(body);
                subcube->setPhysicsPosition(center);
            }
            m_dynamicObjectManager->addGlobalDynamicSubcube(std::move(subcube));
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
        } else {
            auto microcube = std::make_unique<Microcube>(parentPos, tMicro.subcubePos, tMicro.microcubePos, tMicro.material);
            if (m_chunkManager->physicsWorld) {
                glm::vec3 corner = microcube->getWorldPosition();
                glm::vec3 size(1.0f/9.0f);
                glm::vec3 center = corner + size * 0.5f;
                btRigidBody* body = m_chunkManager->physicsWorld->createBreakawayCube(center, size, 0.1f);
                microcube->setRigidBody(body);
                microcube->setPhysicsPosition(center);
            }
            m_dynamicObjectManager->addGlobalDynamicMicrocube(std::move(microcube));
        }
        
        spawn.currentMicrocubeIndex++;
        processedVoxels++;
    }

    // Update modified chunks immediately so the user sees the progress
    if (spawn.isStatic) {
        for (Chunk* chunk : modifiedChunks) {
            chunk->rebuildFaces();
            chunk->updateVulkanBuffer();
        }
    }

    // Check if done
    if (spawn.currentCubeIndex >= tmpl->cubes.size() &&
        spawn.currentSubcubeIndex >= tmpl->subcubes.size() &&
        spawn.currentMicrocubeIndex >= tmpl->microcubes.size()) {
        
        m_pendingSpawns.pop_front();
        LOG_INFO("ObjectTemplateManager", "Finished sequential spawn");
    }
}

} // namespace Phyxel
