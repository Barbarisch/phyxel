#include "core/PlacedObjectManager.h"
#include "core/ChunkManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/SnapshotManager.h"
#include "core/VoxelTemplate.h"
#include "core/Chunk.h"
#include "utils/CoordinateUtils.h"
#include "utils/Logger.h"

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace Phyxel {
namespace Core {

// ============================================================================
// PlacedObject serialization
// ============================================================================

nlohmann::json PlacedObject::toJson() const {
    return {
        {"id", id},
        {"template_name", templateName},
        {"category", category},
        {"parent_id", parentId},
        {"position", {{"x", position.x}, {"y", position.y}, {"z", position.z}}},
        {"rotation", rotation},
        {"bounding_min", {{"x", boundingMin.x}, {"y", boundingMin.y}, {"z", boundingMin.z}}},
        {"bounding_max", {{"x", boundingMax.x}, {"y", boundingMax.y}, {"z", boundingMax.z}}}
    };
}

PlacedObject PlacedObject::fromJson(const nlohmann::json& j) {
    PlacedObject obj;
    obj.id = j.value("id", "");
    obj.templateName = j.value("template_name", "");
    obj.category = j.value("category", "template");
    obj.parentId = j.value("parent_id", "");
    if (j.contains("position")) {
        obj.position.x = j["position"].value("x", 0);
        obj.position.y = j["position"].value("y", 0);
        obj.position.z = j["position"].value("z", 0);
    }
    obj.rotation = j.value("rotation", 0);
    if (j.contains("bounding_min")) {
        obj.boundingMin.x = j["bounding_min"].value("x", 0);
        obj.boundingMin.y = j["bounding_min"].value("y", 0);
        obj.boundingMin.z = j["bounding_min"].value("z", 0);
    }
    if (j.contains("bounding_max")) {
        obj.boundingMax.x = j["bounding_max"].value("x", 0);
        obj.boundingMax.y = j["bounding_max"].value("y", 0);
        obj.boundingMax.z = j["bounding_max"].value("z", 0);
    }
    obj.createdAt = std::chrono::system_clock::now();
    return obj;
}

// ============================================================================
// PlacedObjectManager
// ============================================================================

PlacedObjectManager::PlacedObjectManager(ChunkManager* chunkMgr, ObjectTemplateManager* templateMgr,
                                         SnapshotManager* snapshotMgr)
    : m_chunkManager(chunkMgr)
    , m_templateManager(templateMgr)
    , m_snapshotManager(snapshotMgr)
{
}

std::string PlacedObjectManager::generateId(const std::string& baseName) {
    // m_mutex must already be held by caller
    int& counter = m_idCounters[baseName];
    ++counter;
    return baseName + "_" + std::to_string(counter);
}

std::pair<glm::ivec3, glm::ivec3> PlacedObjectManager::computeTemplateBounds(
    const std::string& templateName, const glm::ivec3& position, int rotation) const
{
    if (!m_templateManager) return {position, position};

    const VoxelTemplate* tmpl = m_templateManager->getTemplate(templateName);
    if (!tmpl || (tmpl->cubes.empty() && tmpl->subcubes.empty() && tmpl->microcubes.empty())) {
        return {position, position};
    }

    // Compute template-local bounding box first (for rotation pivot)
    glm::ivec3 localMin(INT_MAX), localMax(INT_MIN);
    for (const auto& c : tmpl->cubes) {
        localMin = glm::min(localMin, c.relativePos);
        localMax = glm::max(localMax, c.relativePos);
    }
    for (const auto& s : tmpl->subcubes) {
        localMin = glm::min(localMin, s.parentRelativePos);
        localMax = glm::max(localMax, s.parentRelativePos);
    }
    for (const auto& m : tmpl->microcubes) {
        localMin = glm::min(localMin, m.parentRelativePos);
        localMax = glm::max(localMax, m.parentRelativePos);
    }

    // Apply rotation (matching ObjectTemplateManager::spawnTemplate logic)
    int rotSteps = ((rotation % 360) + 360) % 360 / 90;
    if (rotSteps > 0) {
        glm::ivec3 maxExtent = localMax;
        auto rotateOffset = [&](glm::ivec3 pos) -> glm::ivec3 {
            switch (rotSteps) {
                case 1: return glm::ivec3(maxExtent.z - pos.z, pos.y, pos.x);
                case 2: return glm::ivec3(maxExtent.x - pos.x, pos.y, maxExtent.z - pos.z);
                case 3: return glm::ivec3(pos.z, pos.y, maxExtent.x - pos.x);
                default: return pos;
            }
        };

        glm::ivec3 rotMin(INT_MAX), rotMax(INT_MIN);
        for (const auto& c : tmpl->cubes) {
            glm::ivec3 rotated = rotateOffset(c.relativePos);
            rotMin = glm::min(rotMin, rotated);
            rotMax = glm::max(rotMax, rotated);
        }
        for (const auto& s : tmpl->subcubes) {
            glm::ivec3 rotated = rotateOffset(s.parentRelativePos);
            rotMin = glm::min(rotMin, rotated);
            rotMax = glm::max(rotMax, rotated);
        }
        for (const auto& m : tmpl->microcubes) {
            glm::ivec3 rotated = rotateOffset(m.parentRelativePos);
            rotMin = glm::min(rotMin, rotated);
            rotMax = glm::max(rotMax, rotated);
        }
        localMin = rotMin;
        localMax = rotMax;
    }

    return {position + localMin, position + localMax};
}

void PlacedObjectManager::clearRegion(const glm::ivec3& min, const glm::ivec3& max) {
    if (!m_chunkManager) return;

    // Group positions by chunk for batch removal (same pattern as clear_region handler)
    struct ChunkCoordHash {
        size_t operator()(const glm::ivec3& v) const {
            size_t h = std::hash<int>()(v.x);
            h ^= std::hash<int>()(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<glm::ivec3, std::vector<glm::ivec3>, ChunkCoordHash> chunkBatches;
    for (int x = min.x; x <= max.x; ++x) {
        for (int y = min.y; y <= max.y; ++y) {
            for (int z = min.z; z <= max.z; ++z) {
                glm::ivec3 worldPos(x, y, z);
                glm::ivec3 cc = ChunkManager::worldToChunkCoord(worldPos);
                glm::ivec3 lp = ChunkManager::worldToLocalCoord(worldPos);
                chunkBatches[cc].push_back(lp);
            }
        }
    }

    std::vector<Chunk*> modifiedChunks;
    for (auto& [cc, positions] : chunkBatches) {
        Chunk* chunk = m_chunkManager->getChunkAtCoord(cc);
        if (!chunk) continue;

        chunk->removeCubesBatch(positions);
        for (const auto& p : positions) {
            chunk->clearSubdivisionAt(p);
        }
        modifiedChunks.push_back(chunk);
    }

    // Rebuild meshes so cleared subdivisions are reflected visually
    for (Chunk* chunk : modifiedChunks) {
        chunk->rebuildFaces();
        chunk->updateVulkanBuffer();
    }
}

std::string PlacedObjectManager::placeTemplate(const std::string& templateName,
                                                const glm::ivec3& position, int rotation,
                                                const std::string& parentId) {
    if (!m_templateManager) return "";

    // Validate parent exists if specified
    if (!parentId.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_objects.find(parentId) == m_objects.end()) return "";
    }

    // Compute bounding box before placement
    auto [bmin, bmax] = computeTemplateBounds(templateName, position, rotation);

    // Spawn the template via ObjectTemplateManager
    bool ok = m_templateManager->spawnTemplate(templateName, glm::vec3(position), true, rotation);
    if (!ok) return "";

    std::lock_guard<std::mutex> lock(m_mutex);
    std::string id = generateId(templateName);

    PlacedObject obj;
    obj.id = id;
    obj.templateName = templateName;
    obj.category = "template";
    obj.parentId = parentId;
    obj.position = position;
    obj.rotation = rotation;
    obj.boundingMin = bmin;
    obj.boundingMax = bmax;
    obj.createdAt = std::chrono::system_clock::now();

    m_objects[id] = std::move(obj);

    LOG_INFO_FMT("PlacedObjectManager", "Placed template '" << templateName
                 << "' as '" << id << "' at (" << position.x << "," << position.y << "," << position.z
                 << ") rot=" << rotation << (parentId.empty() ? "" : " parent=" + parentId));
    return id;
}

std::string PlacedObjectManager::registerStructure(const std::string& typeName,
                                                    const glm::ivec3& position, int rotation,
                                                    const glm::ivec3& bboxMin, const glm::ivec3& bboxMax,
                                                    const std::string& parentId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Validate parent exists if specified
    if (!parentId.empty() && m_objects.find(parentId) == m_objects.end()) return "";

    std::string id = generateId(typeName);

    PlacedObject obj;
    obj.id = id;
    obj.templateName = typeName;
    obj.category = "structure";
    obj.parentId = parentId;
    obj.position = position;
    obj.rotation = rotation;
    obj.boundingMin = bboxMin;
    obj.boundingMax = bboxMax;
    obj.createdAt = std::chrono::system_clock::now();

    m_objects[id] = std::move(obj);

    LOG_INFO_FMT("PlacedObjectManager", "Registered structure '" << typeName
                 << "' as '" << id << "' bbox (" << bboxMin.x << "," << bboxMin.y << "," << bboxMin.z
                 << ")-(" << bboxMax.x << "," << bboxMax.y << "," << bboxMax.z << ")"
                 << (parentId.empty() ? "" : " parent=" + parentId));
    return id;
}

bool PlacedObjectManager::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_objects.find(id);
    if (it == m_objects.end()) return false;

    // Collect all descendants (depth-first) so children are removed first
    std::vector<std::string> toRemove;
    std::function<void(const std::string&)> collectDescendants = [&](const std::string& parentId) {
        for (const auto& [childId, childObj] : m_objects) {
            if (childObj.parentId == parentId) {
                collectDescendants(childId);
                toRemove.push_back(childId);
            }
        }
    };
    collectDescendants(id);
    toRemove.push_back(id);  // Remove self last

    // Clear voxels and erase entries
    for (const auto& removeId : toRemove) {
        auto removeIt = m_objects.find(removeId);
        if (removeIt == m_objects.end()) continue;

        const PlacedObject& obj = removeIt->second;
        clearRegion(obj.boundingMin, obj.boundingMax);

        LOG_INFO_FMT("PlacedObjectManager", "Removed '" << removeId << "' clearing region ("
                     << obj.boundingMin.x << "," << obj.boundingMin.y << "," << obj.boundingMin.z
                     << ")-(" << obj.boundingMax.x << "," << obj.boundingMax.y << "," << obj.boundingMax.z << ")");

        m_objects.erase(removeIt);
    }

    return true;
}

bool PlacedObjectManager::move(const std::string& id, const glm::ivec3& newPosition) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_objects.find(id);
    if (it == m_objects.end()) return false;

    PlacedObject& obj = it->second;

    // Only templates can be re-voxelized (structures don't have a loadable template)
    if (obj.category != "template" || !m_templateManager) return false;

    // Compute delta for moving children
    glm::ivec3 delta = newPosition - obj.position;

    // Clear old voxels
    clearRegion(obj.boundingMin, obj.boundingMax);

    // Re-place at new position
    bool ok = m_templateManager->spawnTemplate(obj.templateName, glm::vec3(newPosition), true, obj.rotation);
    if (!ok) {
        LOG_ERROR_FMT("PlacedObjectManager", "Failed to re-place '" << id << "' at new position");
        m_objects.erase(it);
        return false;
    }

    // Update metadata
    auto [bmin, bmax] = computeTemplateBounds(obj.templateName, newPosition, obj.rotation);
    obj.position = newPosition;
    obj.boundingMin = bmin;
    obj.boundingMax = bmax;

    // Recursively move all children by the same delta
    std::function<void(const std::string&)> moveChildren = [&](const std::string& parentId) {
        for (auto& [childId, childObj] : m_objects) {
            if (childObj.parentId != parentId) continue;

            glm::ivec3 childNewPos = childObj.position + delta;

            if (childObj.category == "template" && m_templateManager) {
                clearRegion(childObj.boundingMin, childObj.boundingMax);
                m_templateManager->spawnTemplate(childObj.templateName, glm::vec3(childNewPos), true, childObj.rotation);
                auto [cbmin, cbmax] = computeTemplateBounds(childObj.templateName, childNewPos, childObj.rotation);
                childObj.position = childNewPos;
                childObj.boundingMin = cbmin;
                childObj.boundingMax = cbmax;
            } else {
                // Structure: shift bounding box (voxels already moved with clear/re-place of parent)
                childObj.position = childNewPos;
                childObj.boundingMin += delta;
                childObj.boundingMax += delta;
            }

            moveChildren(childId);
        }
    };
    moveChildren(id);

    LOG_INFO_FMT("PlacedObjectManager", "Moved '" << id << "' to ("
                 << newPosition.x << "," << newPosition.y << "," << newPosition.z << ")");
    return true;
}

bool PlacedObjectManager::rotate(const std::string& id, int newRotation) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_objects.find(id);
    if (it == m_objects.end()) return false;

    PlacedObject& obj = it->second;

    if (obj.category != "template" || !m_templateManager) return false;

    // Clear old voxels
    clearRegion(obj.boundingMin, obj.boundingMax);

    // Re-place with new rotation
    bool ok = m_templateManager->spawnTemplate(obj.templateName, glm::vec3(obj.position), true, newRotation);
    if (!ok) {
        LOG_ERROR_FMT("PlacedObjectManager", "Failed to rotate '" << id << "'");
        m_objects.erase(it);
        return false;
    }

    // Update metadata
    auto [bmin, bmax] = computeTemplateBounds(obj.templateName, obj.position, newRotation);
    obj.rotation = newRotation;
    obj.boundingMin = bmin;
    obj.boundingMax = bmax;

    LOG_INFO_FMT("PlacedObjectManager", "Rotated '" << id << "' to " << newRotation << "°");
    return true;
}

const PlacedObject* PlacedObjectManager::get(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_objects.find(id);
    return (it != m_objects.end()) ? &it->second : nullptr;
}

std::vector<PlacedObject> PlacedObjectManager::list() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlacedObject> result;
    result.reserve(m_objects.size());
    for (const auto& [id, obj] : m_objects) {
        result.push_back(obj);
    }
    return result;
}

std::vector<std::string> PlacedObjectManager::getAt(const glm::ivec3& worldPos) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result;
    for (const auto& [id, obj] : m_objects) {
        if (worldPos.x >= obj.boundingMin.x && worldPos.x <= obj.boundingMax.x &&
            worldPos.y >= obj.boundingMin.y && worldPos.y <= obj.boundingMax.y &&
            worldPos.z >= obj.boundingMin.z && worldPos.z <= obj.boundingMax.z) {
            result.push_back(id);
        }
    }
    return result;
}

void PlacedObjectManager::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_objects.clear();
}

nlohmann::json PlacedObjectManager::toJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [id, obj] : m_objects) {
        arr.push_back(obj.toJson());
    }
    return arr;
}

void PlacedObjectManager::fromJson(const nlohmann::json& j) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_objects.clear();

    if (!j.is_array()) return;
    for (const auto& item : j) {
        PlacedObject obj = PlacedObject::fromJson(item);
        if (!obj.id.empty()) {
            // Restore ID counter so future IDs don't collide
            auto underscorePos = obj.id.rfind('_');
            if (underscorePos != std::string::npos) {
                std::string base = obj.id.substr(0, underscorePos);
                try {
                    int num = std::stoi(obj.id.substr(underscorePos + 1));
                    if (num > m_idCounters[base]) {
                        m_idCounters[base] = num;
                    }
                } catch (...) {}
            }
            m_objects[obj.id] = std::move(obj);
        }
    }
}

size_t PlacedObjectManager::count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_objects.size();
}

bool PlacedObjectManager::setParent(const std::string& id, const std::string& parentId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_objects.find(id);
    if (it == m_objects.end()) return false;

    // Validate parent exists (if non-empty)
    if (!parentId.empty() && m_objects.find(parentId) == m_objects.end()) return false;

    // Prevent self-parenting
    if (id == parentId) return false;

    // Prevent circular references: walk up from parentId, ensure we don't reach id
    if (!parentId.empty()) {
        std::string current = parentId;
        while (!current.empty()) {
            if (current == id) return false;  // Would create a cycle
            auto pit = m_objects.find(current);
            if (pit == m_objects.end()) break;
            current = pit->second.parentId;
        }
    }

    it->second.parentId = parentId;
    LOG_INFO_FMT("PlacedObjectManager", "Set parent of '" << id << "' to '"
                 << (parentId.empty() ? "(root)" : parentId) << "'");
    return true;
}

std::vector<PlacedObject> PlacedObjectManager::getChildren(const std::string& parentId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlacedObject> result;
    for (const auto& [id, obj] : m_objects) {
        if (obj.parentId == parentId) {
            result.push_back(obj);
        }
    }
    return result;
}

std::vector<PlacedObject> PlacedObjectManager::getDescendants(const std::string& rootId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlacedObject> result;

    std::function<void(const std::string&)> collect = [&](const std::string& parentId) {
        for (const auto& [id, obj] : m_objects) {
            if (obj.parentId == parentId) {
                result.push_back(obj);
                collect(id);
            }
        }
    };
    collect(rootId);
    return result;
}

nlohmann::json PlacedObjectManager::getTree(const std::string& rootId) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_objects.find(rootId);
    if (it == m_objects.end()) return nullptr;

    std::function<nlohmann::json(const std::string&)> buildTree = [&](const std::string& nodeId) -> nlohmann::json {
        auto nodeIt = m_objects.find(nodeId);
        if (nodeIt == m_objects.end()) return nullptr;

        nlohmann::json node = nodeIt->second.toJson();
        nlohmann::json children = nlohmann::json::array();
        for (const auto& [id, obj] : m_objects) {
            if (obj.parentId == nodeId) {
                children.push_back(buildTree(id));
            }
        }
        node["children"] = children;
        return node;
    };

    return buildTree(rootId);
}

// ============================================================================
// SQLite persistence
// ============================================================================

bool PlacedObjectManager::saveToDb(sqlite3* db) const {
    if (!db) return false;

    const char* createSql = R"(
        CREATE TABLE IF NOT EXISTS placed_objects (
            id TEXT PRIMARY KEY,
            objects_json TEXT NOT NULL,
            modified_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";
    char* err = nullptr;
    sqlite3_exec(db, createSql, nullptr, nullptr, &err);
    if (err) {
        LOG_ERROR("PlacedObjectManager", "Failed to create placed_objects table: {}", err);
        sqlite3_free(err);
        return false;
    }

    const char* upsertSql = R"(
        INSERT OR REPLACE INTO placed_objects (id, objects_json, modified_at)
        VALUES ('registry', ?, datetime('now'));
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, upsertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("PlacedObjectManager", "Failed to prepare save statement: {}", sqlite3_errmsg(db));
        return false;
    }

    std::string jsonStr = toJson().dump();
    sqlite3_bind_text(stmt, 1, jsonStr.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (ok) {
        LOG_INFO("PlacedObjectManager", "Saved {} placed objects to database", count());
    } else {
        LOG_ERROR("PlacedObjectManager", "Failed to save placed objects: {}", sqlite3_errmsg(db));
    }
    return ok;
}

bool PlacedObjectManager::loadFromDb(sqlite3* db) {
    if (!db) return false;

    // Ensure table exists
    const char* createSql = R"(
        CREATE TABLE IF NOT EXISTS placed_objects (
            id TEXT PRIMARY KEY,
            objects_json TEXT NOT NULL,
            modified_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";
    sqlite3_exec(db, createSql, nullptr, nullptr, nullptr);

    const char* selectSql = "SELECT objects_json FROM placed_objects WHERE id = 'registry';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, selectSql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("PlacedObjectManager", "Failed to prepare load statement: {}", sqlite3_errmsg(db));
        return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) {
            auto j = nlohmann::json::parse(text, nullptr, false);
            if (!j.is_discarded()) {
                fromJson(j);
                ok = true;
                LOG_INFO("PlacedObjectManager", "Loaded {} placed objects from database", count());
            } else {
                LOG_ERROR("PlacedObjectManager", "Failed to parse placed objects JSON from database");
            }
        }
    } else {
        LOG_INFO("PlacedObjectManager", "No placed objects found in database");
        ok = true; // Not an error — just no data yet
    }

    sqlite3_finalize(stmt);
    return ok;
}

} // namespace Core
} // namespace Phyxel
