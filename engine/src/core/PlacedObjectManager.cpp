#include "core/PlacedObjectManager.h"
#include "core/ChunkManager.h"
#include "core/Uuid.h"
#include "core/ObjectTemplateManager.h"
#include "core/SnapshotManager.h"
#include "core/VoxelTemplate.h"
#include "core/Chunk.h"
#include "utils/CoordinateUtils.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <iomanip>
#include <set>

namespace Phyxel {
namespace Core {

// ============================================================================
// PlacedObject serialization
// ============================================================================

nlohmann::json PlacedObject::toJson() const {
    return {
        {"id", id},
        {"uuid", uuid},
        {"template_name", templateName},
        {"category", category},
        {"parent_id", parentId},
        {"position", {{"x", position.x}, {"y", position.y}, {"z", position.z}}},
        {"rotation", rotation},
        {"bounding_min", {{"x", boundingMin.x}, {"y", boundingMin.y}, {"z", boundingMin.z}}},
        {"bounding_max", {{"x", boundingMax.x}, {"y", boundingMax.y}, {"z", boundingMax.z}}},
        {"metadata", metadata}
    };
}

PlacedObject PlacedObject::fromJson(const nlohmann::json& j) {
    PlacedObject obj;
    obj.id = j.value("id", "");
    // Lazy backfill: worlds saved before uuids existed have no "uuid" field — mint
    // one on load so pre-existing objects become addressable by uuid. It persists
    // on the next save_world (no eager write — respects the ghost-record rule).
    obj.uuid = j.value("uuid", "");
    if (obj.uuid.empty()) obj.uuid = Core::Uuid::generate();
    obj.templateName = j.value("template_name", "");
    obj.category = j.value("category", "template");
    obj.parentId = j.value("parent_id", "");
    if (j.contains("position")) {
        obj.position.x = j["position"].value("x", 0);
        obj.position.y = j["position"].value("y", 0);
        obj.position.z = j["position"].value("z", 0);
    }
    obj.rotation = j.value("rotation", 0);
    if (j.contains("metadata") && j["metadata"].is_object()) {
        obj.metadata = j["metadata"];
    }
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

void PlacedObjectManager::insertObjectLocked(PlacedObject&& obj) {
    // m_mutex must already be held by caller.
    if (obj.uuid.empty()) obj.uuid = Core::Uuid::generate();
    // v4 collision is astronomically unlikely, but never silently overwrite an
    // existing identity — re-mint on the (practically impossible) clash.
    while (m_uuidToId.count(obj.uuid)) obj.uuid = Core::Uuid::generate();
    m_uuidToId[obj.uuid] = obj.id;
    m_objects[obj.id] = std::move(obj);
}

std::string PlacedObjectManager::resolveIdLocked(const std::string& idOrUuid) const {
    // m_mutex must already be held by caller.
    if (Core::Uuid::isValid(idOrUuid)) {
        auto it = m_uuidToId.find(idOrUuid);
        return (it != m_uuidToId.end()) ? it->second : std::string();  // unknown uuid → no match
    }
    return idOrUuid;  // legacy base_N id (or "") — used verbatim
}

// ============================================================================
// Interaction point helpers
// ============================================================================

static glm::vec3 rotateLocalOffset(const glm::vec3& offset, int rotDegrees) {
    // Rotate a template-local offset around the Y axis by rotDegrees (0/90/180/270).
    int steps = ((rotDegrees % 360) + 360) % 360 / 90;
    float x = offset.x, z = offset.z;
    for (int i = 0; i < steps; ++i) {
        float tmp = x;
        x = z;
        z = -tmp;
    }
    return {x, offset.y, z};
}

std::vector<InteractionPoint> PlacedObjectManager::computeInteractionPoints(
    const std::vector<InteractionPointDef>& defs,
    const glm::ivec3& position, int rotation)
{
    std::vector<InteractionPoint> result;
    result.reserve(defs.size());
    float rotRad = (rotation * 3.14159265f) / 180.0f;
    for (const auto& def : defs) {
        InteractionPoint pt;
        pt.pointId = def.pointId;
        pt.type    = def.type;
        pt.supportedGroups = def.supportedGroups;
        pt.objectRotation  = rotation;
        glm::vec3 rotOffset  = rotateLocalOffset(def.localOffset,  rotation);
        pt.worldPos        = glm::vec3(position) + rotOffset;
        pt.facingYaw       = def.facingYaw + rotRad;
        pt.worldSitDownOffset     = rotateLocalOffset(def.sitDownOffset,     rotation);
        pt.worldSittingIdleOffset = rotateLocalOffset(def.sittingIdleOffset, rotation);
        pt.worldSitStandUpOffset  = rotateLocalOffset(def.sitStandUpOffset,  rotation);
        pt.sitBlendDuration       = def.sitBlendDuration;
        pt.seatHeightOffset       = def.seatHeightOffset;
        pt.interactionRadius      = def.interactionRadius;
        pt.promptText             = def.promptText;
        pt.viewAngleHalf          = def.viewAngleHalf;
        pt.requireCompatibility   = def.requireCompatibility;
        result.push_back(std::move(pt));
    }
    return result;
}

void PlacedObjectManager::registerTemplateDefs(const std::string& templateName,
                                                const std::vector<InteractionPointDef>& defs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_templateDefs[templateName] = defs;
    LOG_INFO_FMT("PlacedObjectManager", "Registered " << defs.size()
                 << " interaction defs for template '" << templateName << "'");
}

void PlacedObjectManager::recomputeAllInteractionPoints() {
    std::lock_guard<std::mutex> lock(m_mutex);
    int count = 0;
    for (auto& [id, obj] : m_objects) {
        // Item props have a synthetic pickup point, not template-defined ones.
        if (obj.category == "item") {
            obj.interactionPoints = {makeItemPickupPoint(obj)};
            count += 1;
            continue;
        }
        auto defsIt = m_templateDefs.find(obj.templateName);
        if (defsIt == m_templateDefs.end()) continue;
        obj.interactionPoints = computeInteractionPoints(defsIt->second, obj.position, obj.rotation);
        for (const auto& pt : obj.interactionPoints) {
            LOG_INFO_FMT("PlacedObjectManager", "  [" << id << "] '" << pt.pointId
                << "' type=" << pt.type
                << " objPos=(" << obj.position.x << "," << obj.position.y << "," << obj.position.z << ")"
                << " worldPos=(" << pt.worldPos.x << "," << pt.worldPos.y << "," << pt.worldPos.z << ")");
        }
        count += (int)obj.interactionPoints.size();
    }
    LOG_INFO_FMT("PlacedObjectManager", "Recomputed interaction points: "
                 << count << " points across " << m_objects.size() << " objects");
}

std::pair<std::string, std::string> PlacedObjectManager::findNearestFreePoint(
    const glm::vec3& worldPos, float radius, const std::string& type) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    float bestDist2 = radius * radius;
    std::string bestObj, bestPt;
    for (const auto& [id, obj] : m_objects) {
        for (const auto& pt : obj.interactionPoints) {
            if (pt.type != type) continue;
            if (!pt.isFree()) continue;
            glm::vec3 diff = pt.worldPos - worldPos;
            float d2 = glm::dot(diff, diff);
if (d2 < bestDist2) {
                bestDist2 = d2;
                bestObj = id;
                bestPt  = pt.pointId;
            }
        }
    }
    return {bestObj, bestPt};
}

PlacedObjectManager::NearestPointResult PlacedObjectManager::findNearestFreePointEx(
    const glm::vec3& worldPos, const glm::vec3& playerFront,
    float defaultRadius, const std::string& type) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    NearestPointResult result;
    float bestDist2 = std::numeric_limits<float>::max();

    bool hasFront = glm::dot(playerFront, playerFront) > 0.001f;
    glm::vec3 frontNorm = hasFront ? glm::normalize(glm::vec3(playerFront.x, 0.0f, playerFront.z)) : glm::vec3(0);

    for (const auto& [id, obj] : m_objects) {
        for (const auto& pt : obj.interactionPoints) {
            if (pt.type != type) continue;
            if (!pt.isFree()) continue;

            // Per-point radius or default
            float r = pt.interactionRadius > 0.0f ? pt.interactionRadius : defaultRadius;
            float r2 = r * r;

            glm::vec3 diff = pt.worldPos - worldPos;
            float d2 = glm::dot(diff, diff);
            if (d2 >= r2 || d2 >= bestDist2) continue;

            // View angle check (XZ plane only)
            if (hasFront && pt.viewAngleHalf > 0.0f && d2 > 0.01f) {
                glm::vec3 toPoint = glm::normalize(glm::vec3(diff.x, 0.0f, diff.z));
                float cosAngle = glm::dot(frontNorm, toPoint);
                float halfRad = glm::radians(pt.viewAngleHalf);
                if (cosAngle < cosf(halfRad)) continue; // Outside view cone
            }

            bestDist2 = d2;
            result.objectId = id;
            result.pointId = pt.pointId;
            result.worldPos = pt.worldPos;
            result.promptText = pt.promptText;
            result.interactionRadius = pt.interactionRadius;
            result.viewAngleHalf = pt.viewAngleHalf;
            result.found = true;
        }
    }
    return result;
}

bool PlacedObjectManager::claimInteractionPoint(const std::string& objectId,
                                                 const std::string& pointId,
                                                 const std::string& occupantId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_objects.find(objectId);
    if (it == m_objects.end()) return false;
    for (auto& pt : it->second.interactionPoints) {
        if (pt.pointId == pointId) {
            if (!pt.isFree()) return false;
            pt.occupantId = occupantId;
            return true;
        }
    }
    return false;
}

void PlacedObjectManager::releaseInteractionPoint(const std::string& objectId,
                                                   const std::string& pointId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_objects.find(objectId);
    if (it == m_objects.end()) return;
    for (auto& pt : it->second.interactionPoints) {
        if (pt.pointId == pointId) {
            pt.occupantId.clear();
            return;
        }
    }
}

void PlacedObjectManager::releaseAllByOccupant(const std::string& occupantId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, obj] : m_objects) {
        for (auto& pt : obj.interactionPoints) {
            if (pt.occupantId == occupantId) {
                pt.occupantId.clear();
            }
        }
    }
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

std::pair<glm::ivec3, glm::ivec3> PlacedObjectManager::computeMicroPlacedBounds(
    const std::string& templateName, const glm::ivec3& worldMicro, int rotation) const
{
    auto fdiv = [](int a, int b) { int q = a / b; if ((a % b) != 0 && ((a < 0) != (b < 0))) --q; return q; };
    const glm::ivec3 cubeAnchor(fdiv(worldMicro.x, 9), fdiv(worldMicro.y, 9), fdiv(worldMicro.z, 9));
    if (!m_templateManager) return {cubeAnchor, cubeAnchor};
    const VoxelTemplate* tmpl = m_templateManager->getTemplate(templateName);
    if (!tmpl || (tmpl->cubes.empty() && tmpl->subcubes.empty() && tmpl->microcubes.empty()))
        return {cubeAnchor, cubeAnchor};

    // Template-local MICRO AABB — expand each primitive to its micro cell RANGE (cube -> 9, subcube ->
    // 3, microcube -> 1), exactly as spawnTemplateMicro does. Track the min origin and the max cell
    // (origin + span - 1) so the AABB covers the last filled micro.
    glm::ivec3 mmin(INT_MAX), mmax(INT_MIN);
    auto acc = [&](const glm::ivec3& origin, int span) {
        mmin = glm::min(mmin, origin);
        mmax = glm::max(mmax, origin + glm::ivec3(span - 1));
    };
    for (const auto& c : tmpl->cubes)      acc(c.relativePos * 9, 9);
    for (const auto& s : tmpl->subcubes)   acc(s.parentRelativePos * 9 + s.subcubePos * 3, 3);
    for (const auto& m : tmpl->microcubes) acc(m.parentRelativePos * 9 + m.subcubePos * 3 + m.microcubePos, 1);
    if (mmin.x > mmax.x) return {cubeAnchor, cubeAnchor};

    // Rotate the micro AABB about the micro pivot (mmax), matching spawnTemplateMicro's rotMicro. The
    // image of an axis-aligned box under this reflection-and-swap is another axis-aligned box, exactly
    // spanned by the rotated 8 corners — so min/max over the corners is the true rotated AABB.
    const int rotSteps = ((rotation % 360) + 360) % 360 / 90;
    auto rotMicro = [&](const glm::ivec3& p) -> glm::ivec3 {
        switch (rotSteps) {
            case 1: return glm::ivec3(mmax.z - p.z, p.y, p.x);
            case 2: return glm::ivec3(mmax.x - p.x, p.y, mmax.z - p.z);
            case 3: return glm::ivec3(p.z, p.y, mmax.x - p.x);
            default: return p;
        }
    };
    glm::ivec3 rmin(INT_MAX), rmax(INT_MIN);
    for (int cx = 0; cx < 2; ++cx) for (int cy = 0; cy < 2; ++cy) for (int cz = 0; cz < 2; ++cz) {
        const glm::ivec3 corner(cx ? mmax.x : mmin.x, cy ? mmax.y : mmin.y, cz ? mmax.z : mmin.z);
        const glm::ivec3 r = rotMicro(corner);
        rmin = glm::min(rmin, r);  rmax = glm::max(rmax, r);
    }

    // Shift by the exact worldMicro (INCLUDING the off-grid inset) and floor-divide to cubes.
    const glm::ivec3 gmin = worldMicro + rmin;
    const glm::ivec3 gmax = worldMicro + rmax;
    return {glm::ivec3(fdiv(gmin.x, 9), fdiv(gmin.y, 9), fdiv(gmin.z, 9)),
            glm::ivec3(fdiv(gmax.x, 9), fdiv(gmax.y, 9), fdiv(gmax.z, 9))};
}

// ============================================================================
// Deterministic structure seating
//
// Everything here is MEASURED from the template's own geometry and the live
// terrain — nothing is assumed. The old "surface-snap" only ever RAISED a
// template onto the highest cube under it; it never seated the floor flush with
// the surrounding walkable surface, never excavated, and never built steps,
// which is why structures kept ending up buried or floating a cube off.
// ============================================================================
PlacedObjectManager::SeatPlan PlacedObjectManager::seatStructure(
    const std::string& templateName, const glm::ivec3& requestedPos,
    int rotation, int maxStepRise, const std::string& stepMaterial)
{
    SeatPlan plan;
    if (!m_templateManager || !m_chunkManager) return plan;
    const VoxelTemplate* tmpl = m_templateManager->getTemplate(templateName);
    if (!tmpl || (tmpl->cubes.empty() && tmpl->subcubes.empty() && tmpl->microcubes.empty()))
        return plan;

    // ---- local bounds + rotation (mirror computeTemplateBounds / spawnTemplate) ----
    glm::ivec3 localMin(INT_MAX), localMax(INT_MIN);
    auto acc = [&](const glm::ivec3& p){ localMin = glm::min(localMin, p); localMax = glm::max(localMax, p); };
    for (const auto& c : tmpl->cubes)      acc(c.relativePos);
    for (const auto& s : tmpl->subcubes)   acc(s.parentRelativePos);
    for (const auto& m : tmpl->microcubes) acc(m.parentRelativePos);

    const int rotSteps = ((rotation % 360) + 360) % 360 / 90;
    const glm::ivec3 maxExtent = localMax;
    auto rotateOffset = [&](const glm::ivec3& pos) -> glm::ivec3 {
        switch (rotSteps) {
            case 1: return glm::ivec3(maxExtent.z - pos.z, pos.y, pos.x);
            case 2: return glm::ivec3(maxExtent.x - pos.x, pos.y, maxExtent.z - pos.z);
            case 3: return glm::ivec3(pos.z, pos.y, maxExtent.x - pos.x);
            default: return pos;
        }
    };

    // ---- occupied cube columns + per-column occupied levels (rotated, world XZ) ----
    // Rotation preserves Y, so the structure's floor layer is the global min local Y.
    const int floorLocalY = localMin.y;
    auto key = [](int x, int z){
        return (static_cast<long long>(x) << 21) ^ (static_cast<long long>(z) & 0x1fffffLL);
    };
    std::unordered_map<long long, std::pair<int,int>> colXZ;   // key -> (wx,wz)
    std::unordered_map<long long, std::set<int>>      colLevels; // key -> occupied local Y levels
    auto addCell = [&](const glm::ivec3& localPos){
        glm::ivec3 r = rotateOffset(localPos);
        int wx = requestedPos.x + r.x, wz = requestedPos.z + r.z;
        long long k = key(wx, wz);
        colXZ[k] = {wx, wz};
        colLevels[k].insert(localPos.y);
    };
    for (const auto& c : tmpl->cubes)      addCell(c.relativePos);
    for (const auto& s : tmpl->subcubes)   addCell(s.parentRelativePos);
    for (const auto& m : tmpl->microcubes) addCell(m.parentRelativePos);

    // ---- sample ground under each occupied column (terrain only; not placed yet) ----
    const int scanTop = requestedPos.y + localMax.y + 4;
    auto groundTopAt = [&](int wx, int wz) -> int {
        for (int wy = scanTop; wy >= scanTop - 192 && wy >= 0; --wy)
            if (m_chunkManager->hasVoxelAt(glm::ivec3(wx, wy, wz))) return wy;
        return INT_MIN;
    };
    // Use the MEDIAN column top — robust against a few stray high/low voxels (leftover
    // debris, a lone boulder, a pit). Seating to the raw max would let a single high voxel
    // lift the whole structure a cube off the ground; the median tracks the dominant grade.
    std::vector<int> tops;
    tops.reserve(colXZ.size());
    for (auto& [k, xz] : colXZ) {
        int t = groundTopAt(xz.first, xz.second);
        if (t != INT_MIN) tops.push_back(t);
    }
    int groundTop;
    if (tops.empty()) {
        groundTop = requestedPos.y + floorLocalY;       // void: honor request
    } else {
        std::sort(tops.begin(), tops.end());
        groundTop = tops[tops.size() / 2];
    }

    // ---- solve flush seat: floor layer sits AT groundTop so its TOP == walkable surface ----
    plan.groundTop   = groundTop;
    plan.seatY       = groundTop - floorLocalY;       // place origin here (snap=false)
    plan.floorWorldY = groundTop;                     // == seatY + floorLocalY
    const int structTopY        = plan.seatY + localMax.y;
    const int interiorSurfaceY  = plan.floorWorldY + 1; // character stands here inside

    // ---- excavate the terrain the structure occupies (occupied columns, floor..top) ----
    // Keep terrain BELOW the floor as foundation; remove the floor-level cube (grass) and
    // anything poking into the body so the placed floor isn't buried or fighting terrain.
    std::set<Chunk*> dirty;
    for (auto& [k, xz] : colXZ) {
        std::vector<glm::ivec3> rm;
        Chunk* ch = nullptr;
        for (int wy = plan.floorWorldY; wy <= structTopY; ++wy) {
            glm::ivec3 wp(xz.first, wy, xz.second);
            if (!m_chunkManager->hasVoxelAt(wp)) continue;
            glm::ivec3 cc = ChunkManager::worldToChunkCoord(wp);
            glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
            Chunk* c2 = m_chunkManager->getChunkAtCoord(cc);
            if (!c2) continue;
            c2->removeCube(lp, /*deferRebuild=*/true);
            c2->clearSubdivisionAt(lp);
            dirty.insert(c2);
            ++plan.excavated;
        }
        (void)ch; (void)rm;
    }

    // ---- steps: in front of GROUND-LEVEL openings where the exterior ground is lower ----
    // A doorway column has the floor layer but NO wall directly above it, and a neighbor
    // cell that is outside the footprint. On flush/flat ground the exterior walkable
    // surface already equals the interior one, so this loop places zero steps.
    auto occupied = [&](int wx, int wz){ return colXZ.count(key(wx, wz)) > 0; };
    const glm::ivec2 dirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    for (auto& [k, xz] : colXZ) {
        const std::set<int>& levels = colLevels[k];
        if (!levels.count(floorLocalY) || levels.count(floorLocalY + 1)) continue; // not a floor-level opening
        const int wx = xz.first, wz = xz.second;
        for (auto d : dirs) {
            if (occupied(wx + d.x, wz + d.y)) continue;   // neighbor is inside the structure
            int prevSurface = interiorSurfaceY;
            for (int s = 1; s <= 6; ++s) {
                int ex = wx + d.x * s, ez = wz + d.y * s;
                int extTop = groundTopAt(ex, ez);
                int extSurface = (extTop == INT_MIN) ? interiorSurfaceY : extTop + 1;
                if (extSurface >= prevSurface) break;     // already walkable from here out
                int rise = prevSurface - extSurface;
                if (rise > maxStepRise * 6) break;        // too tall to bridge sanely
                int stepTopY = prevSurface - 1;           // one cube down from the prior tread
                glm::ivec3 wp(ex, stepTopY, ez);
                glm::ivec3 cc = ChunkManager::worldToChunkCoord(wp);
                glm::ivec3 lp = ChunkManager::worldToLocalCoord(wp);
                Chunk* ch = m_chunkManager->getChunkAtCoord(cc);
                if (ch && ch->addCube(lp, stepMaterial)) { dirty.insert(ch); ++plan.stepsPlaced; }
                prevSurface = stepTopY + 1;
            }
            break; // one exit direction per opening
        }
    }

    for (Chunk* c : dirty) m_chunkManager->markChunkDirty(c);
    if (!dirty.empty()) m_chunkManager->updateDirtyChunks();

    plan.flush = true;  // floorWorldY == groundTop by construction; scan-verified by the caller
    plan.ok    = true;
    LOG_INFO_FMT("PlacedObjectManager", "seatStructure '" << templateName << "': groundTop="
                 << groundTop << " seatY=" << plan.seatY << " floorY=" << plan.floorWorldY
                 << " interiorSurface=" << interiorSurfaceY << " excavated=" << plan.excavated
                 << " steps=" << plan.stepsPlaced);
    return plan;
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

    bool anyModified = false;
    for (auto& [cc, positions] : chunkBatches) {
        Chunk* chunk = m_chunkManager->getChunkAtCoord(cc);
        if (!chunk) continue;

        chunk->removeCubesBatch(positions);
        for (const auto& p : positions) {
            chunk->clearSubdivisionAt(p);
        }
        // Mark dirty so updateDirtyChunks() rebuilds with proper cross-chunk culling
        m_chunkManager->markChunkDirty(chunk);
        anyModified = true;
    }

    // Flush dirty chunks immediately so the visual update is applied this frame
    if (anyModified) {
        m_chunkManager->updateDirtyChunks();
    }
}

std::string PlacedObjectManager::placeTemplate(const std::string& templateName,
                                                const glm::ivec3& position, int rotation,
                                                const std::string& parentId,
                                                bool snapToGround) {
    if (!m_templateManager) return "";

    // Validate parent exists if specified
    if (!parentId.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_objects.find(parentId) == m_objects.end()) return "";
    }

    // Ground-snap: seat the template's lowest voxel on the surface beneath the
    // requested position. Robust against a caller passing a Y one cube too high
    // (e.g. spawn_y + 1 instead of spawn_y), which left trees hovering a cube
    // off the ground. We scan DOWN from the template's intended footprint so
    // other objects' overhangs above don't interfere, and take the highest
    // ground cell under the footprint so the base doesn't sink into a rise.
    glm::ivec3 place = position;
    if (snapToGround && m_chunkManager) {
        // Pre-placement bounds (chunks don't hold this template yet, so every
        // solid we find is pre-existing terrain/scenery).
        auto [pbmin, pbmax] = computeTemplateBounds(templateName, position, rotation);
        const int baseCell = pbmin.y;           // world cell of the lowest voxel as requested
        int highestGround = INT_MIN;
        // Bound the sampled footprint so a huge template can't trigger a giant scan.
        const int stepX = std::max(1, (pbmax.x - pbmin.x) / 16);
        const int stepZ = std::max(1, (pbmax.z - pbmin.z) / 16);
        for (int x = pbmin.x; x <= pbmax.x; x += stepX) {
            for (int z = pbmin.z; z <= pbmax.z; z += stepZ) {
                // First solid at or below the requested base = the supporting ground.
                for (int y = baseCell; y >= baseCell - 128 && y >= 0; --y) {
                    if (m_chunkManager->hasVoxelAt(glm::ivec3(x, y, z))) {
                        highestGround = std::max(highestGround, y);
                        break;
                    }
                }
            }
        }
        if (highestGround != INT_MIN) {
            // Seat the lowest voxel one cell above the ground top.
            const int snappedBase = highestGround + 1;
            place.y += (snappedBase - baseCell);
        }
    }

    // Compute bounding box before placement
    auto [bmin, bmax] = computeTemplateBounds(templateName, place, rotation);

    // Spawn the template via ObjectTemplateManager
    bool ok = m_templateManager->spawnTemplate(templateName, glm::vec3(place), true, rotation);
    if (!ok) return "";

    // Phase C0b/D: snapshot any kinematic part IDs emitted by the spawn so we
    // can route interaction events (try_pivot) back to them. The animator was
    // wired in Phase C; this records the address book.
    std::vector<std::string> kinematicIds = m_templateManager->lastSpawnedKinematicIds();

    std::lock_guard<std::mutex> lock(m_mutex);
    std::string id = generateId(templateName);

    PlacedObject obj;
    obj.id = id;
    obj.templateName = templateName;
    obj.category = "template";
    obj.parentId = parentId;
    obj.position = place;
    obj.rotation = rotation;
    obj.boundingMin = bmin;
    obj.boundingMax = bmax;
    obj.createdAt = std::chrono::system_clock::now();

    // Populate interaction points if defs are registered for this template
    auto defsIt = m_templateDefs.find(templateName);
    if (defsIt != m_templateDefs.end()) {
        obj.interactionPoints = computeInteractionPoints(defsIt->second, place, rotation);
    }

    // Phase D: record kinematic part IDs in metadata so try_pivot can find
    // the animator binding without doing a name lookup against the template.
    if (!kinematicIds.empty()) {
        obj.metadata["kinematic_part_ids"] = kinematicIds;
    }

    insertObjectLocked(std::move(obj));

    LOG_INFO_FMT("PlacedObjectManager", "Placed template '" << templateName
                 << "' as '" << id << "' at (" << place.x << "," << place.y << "," << place.z
                 << ") rot=" << rotation
                 << (place.y != position.y ? " [ground-snapped from y=" + std::to_string(position.y) + "]" : "")
                 << (parentId.empty() ? "" : " parent=" + parentId));
    return id;
}

std::string PlacedObjectManager::placeTemplateMicro(const std::string& templateName,
                                                    const glm::ivec3& worldMicro, int rotation,
                                                    const std::string& parentId) {
    if (!m_templateManager) return "";
    if (!parentId.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_objects.find(parentId) == m_objects.end()) return "";
    }

    // RENDER-ACCURATE bbox: the registered box must equal what spawnTemplateMicro actually stamps,
    // including the sub-cube micro-spill an off-grid (wall-inset) worldMicro pushes into the next cube.
    // The old cube-anchored computeTemplateBounds(floorDiv(worldMicro/9)) dropped that spill, so the
    // registered box was a strict SUBSET of the render — the furniture-overlap + chest-facing detectors
    // and the V8 chimney-centering all read this box, so they saw a fixture smaller/mis-centred vs its
    // real geometry. computeMicroPlacedBounds matches FurniturePlacer::placedCubeSpan (the reservation),
    // giving reservation == registration == render.
    auto fdiv = [](int a, int b) { int q = a / b, r = a % b; if (r != 0 && (r < 0) != (b < 0)) --q; return q; };
    const glm::ivec3 cubePos(fdiv(worldMicro.x, 9), fdiv(worldMicro.y, 9), fdiv(worldMicro.z, 9));
    auto [bmin, bmax] = computeMicroPlacedBounds(templateName, worldMicro, rotation);

    if (!m_templateManager->spawnTemplateMicro(templateName, worldMicro, rotation)) return "";

    std::lock_guard<std::mutex> lock(m_mutex);
    std::string id = generateId(templateName);
    PlacedObject obj;
    obj.id = id;
    obj.templateName = templateName;
    obj.category = "template";
    obj.parentId = parentId;
    obj.position = cubePos;
    obj.rotation = rotation;
    obj.boundingMin = bmin;
    obj.boundingMax = bmax;
    obj.createdAt = std::chrono::system_clock::now();
    insertObjectLocked(std::move(obj));
    LOG_INFO_FMT("PlacedObjectManager", "Placed template (micro) '" << templateName << "' as '" << id
                 << "' at micro (" << worldMicro.x << "," << worldMicro.y << "," << worldMicro.z
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

    insertObjectLocked(std::move(obj));

    LOG_INFO_FMT("PlacedObjectManager", "Registered structure '" << typeName
                 << "' as '" << id << "' bbox (" << bboxMin.x << "," << bboxMin.y << "," << bboxMin.z
                 << ")-(" << bboxMax.x << "," << bboxMax.y << "," << bboxMax.z << ")"
                 << (parentId.empty() ? "" : " parent=" + parentId));
    return id;
}

std::string PlacedObjectManager::registerItemProp(const std::string& itemId,
                                                   const std::string& templateName,
                                                   const glm::ivec3& position, int rotation,
                                                   const glm::ivec3& bboxMin, const glm::ivec3& bboxMax,
                                                   const std::string& displayName) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string id = generateId("item_" + itemId);

    PlacedObject obj;
    obj.id = id;
    obj.templateName = templateName;
    obj.category = "item";
    obj.position = position;
    obj.rotation = rotation;
    obj.boundingMin = bboxMin;
    obj.boundingMax = bboxMax;
    obj.createdAt = std::chrono::system_clock::now();
    obj.metadata["itemId"] = itemId;
    if (!displayName.empty()) obj.metadata["displayName"] = displayName;

    obj.interactionPoints.push_back(makeItemPickupPoint(obj));

    insertObjectLocked(std::move(obj));

    LOG_INFO_FMT("PlacedObjectManager", "Registered item prop '" << itemId << "' as '" << id
                 << "' at (" << position.x << "," << position.y << "," << position.z << ")");
    return id;
}

bool PlacedObjectManager::updateItemPropPose(const std::string& id,
                                             const glm::ivec3& bboxMin, const glm::ivec3& bboxMax) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_objects.find(id);
    if (it == m_objects.end() || it->second.category != "item") return false;

    PlacedObject& obj = it->second;
    obj.boundingMin = bboxMin;
    obj.boundingMax = bboxMax;
    // Position tracks the bbox floor-center (item props have no meaningful
    // anchor cell once physics has moved them).
    const glm::vec3 center = (glm::vec3(bboxMin) + glm::vec3(bboxMax)) * 0.5f;
    obj.position = glm::ivec3(glm::floor(glm::vec3(center.x, float(bboxMin.y), center.z)));
    // The synthetic pickup point sits at the bbox center; regenerate it so
    // [E] Take follows the moving item (prompt text etc. are re-derived).
    for (auto& pt : obj.interactionPoints) {
        if (pt.type == "pickup") {
            const std::string pointId = pt.pointId;
            pt = makeItemPickupPoint(obj);
            pt.pointId = pointId;
        }
    }
    return true;
}

InteractionPoint PlacedObjectManager::makeItemPickupPoint(const PlacedObject& obj) {
    InteractionPoint pickup;
    pickup.pointId = "pickup_0";
    pickup.type = "pickup";
    pickup.worldPos = (glm::vec3(obj.boundingMin) + glm::vec3(obj.boundingMax)) * 0.5f;
    pickup.interactionRadius = 2.0f;
    std::string display = obj.metadata.value("displayName",
                              obj.metadata.value("itemId", std::string("item")));
    pickup.promptText = "Take " + display;
    pickup.objectRotation = obj.rotation;
    return pickup;
}

bool PlacedObjectManager::remove(const std::string& idOrUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string id = resolveIdLocked(idOrUuid);
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

        // Let subsystems tear down derived state (e.g. an active dynamic-furniture
        // body + render) before the object disappears, so it cannot be re-baked.
        if (m_preRemove) m_preRemove(removeId);

        const PlacedObject& obj = removeIt->second;
        // Item props are never baked into chunks — clearing their bbox would
        // carve air out of whatever terrain the prop rests on.
        if (obj.category != "item") {
            clearRegion(obj.boundingMin, obj.boundingMax);
        }

        LOG_INFO_FMT("PlacedObjectManager", "Removed '" << removeId << "' clearing region ("
                     << obj.boundingMin.x << "," << obj.boundingMin.y << "," << obj.boundingMin.z
                     << ")-(" << obj.boundingMax.x << "," << obj.boundingMax.y << "," << obj.boundingMax.z << ")");

        m_uuidToId.erase(obj.uuid);
        m_objects.erase(removeIt);
    }

    return true;
}

bool PlacedObjectManager::move(const std::string& idOrUuid, const glm::ivec3& newPosition) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string id = resolveIdLocked(idOrUuid);
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

    // Recompute interaction point world positions
    auto defsIt = m_templateDefs.find(obj.templateName);
    if (defsIt != m_templateDefs.end()) {
        auto newPts = computeInteractionPoints(defsIt->second, newPosition, obj.rotation);
        // Preserve occupancy state
        for (auto& newPt : newPts) {
            for (const auto& oldPt : obj.interactionPoints) {
                if (oldPt.pointId == newPt.pointId) { newPt.occupantId = oldPt.occupantId; break; }
            }
        }
        obj.interactionPoints = std::move(newPts);
    }

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

bool PlacedObjectManager::rotate(const std::string& idOrUuid, int newRotation) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string id = resolveIdLocked(idOrUuid);
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

    // Recompute interaction point world positions for new rotation
    auto defsIt = m_templateDefs.find(obj.templateName);
    if (defsIt != m_templateDefs.end()) {
        auto newPts = computeInteractionPoints(defsIt->second, obj.position, newRotation);
        for (auto& newPt : newPts) {
            for (const auto& oldPt : obj.interactionPoints) {
                if (oldPt.pointId == newPt.pointId) { newPt.occupantId = oldPt.occupantId; break; }
            }
        }
        obj.interactionPoints = std::move(newPts);
    }

    LOG_INFO_FMT("PlacedObjectManager", "Rotated '" << id << "' to " << newRotation << "°");
    return true;
}

const PlacedObject* PlacedObjectManager::get(const std::string& idOrUuid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_objects.find(resolveIdLocked(idOrUuid));
    return (it != m_objects.end()) ? &it->second : nullptr;
}

bool PlacedObjectManager::setMetadata(const std::string& idOrUuid, const std::string& key,
                                      const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_objects.find(resolveIdLocked(idOrUuid));
    if (it == m_objects.end()) return false;
    it->second.metadata[key] = value;
    return true;
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

bool PlacedObjectManager::clearVoxelsOnly(const std::string& idOrUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_objects.find(resolveIdLocked(idOrUuid));
    if (it == m_objects.end()) return false;
    clearRegion(it->second.boundingMin, it->second.boundingMax);
    return true;
}

void PlacedObjectManager::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_objects.clear();
    m_uuidToId.clear();
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
    m_uuidToId.clear();

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
            insertObjectLocked(std::move(obj));  // indexes uuid → id (uuid already restored/backfilled)
        }
    }
}

size_t PlacedObjectManager::count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_objects.size();
}

bool PlacedObjectManager::setParent(const std::string& idOrUuid, const std::string& parentIdOrUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string id = resolveIdLocked(idOrUuid);
    const std::string parentId = parentIdOrUuid.empty() ? std::string() : resolveIdLocked(parentIdOrUuid);
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

std::vector<PlacedObject> PlacedObjectManager::getChildren(const std::string& parentIdOrUuid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string parentId = parentIdOrUuid.empty() ? std::string() : resolveIdLocked(parentIdOrUuid);
    std::vector<PlacedObject> result;
    for (const auto& [id, obj] : m_objects) {
        if (obj.parentId == parentId) {
            result.push_back(obj);
        }
    }
    return result;
}

std::vector<PlacedObject> PlacedObjectManager::getDescendants(const std::string& rootIdOrUuid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string rootId = resolveIdLocked(rootIdOrUuid);
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

nlohmann::json PlacedObjectManager::getTree(const std::string& rootIdOrUuid) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string rootId = resolveIdLocked(rootIdOrUuid);
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
