#include "core/KinematicVoxelManager.h"
#include "core/MaterialRegistry.h"
#include "core/Types.h"
#include "physics/PhysicsWorld.h"
#include "utils/Logger.h"

#include <algorithm>
#include <climits>
#include <cfloat>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace Phyxel {
namespace Core {

// ============================================================================
// Construction / destruction
// ============================================================================

KinematicVoxelManager::KinematicVoxelManager(Physics::PhysicsWorld* /*physicsWorld*/)
{}

KinematicVoxelManager::~KinematicVoxelManager() {
    clear();
}

// ============================================================================
// Public interface
// ============================================================================

std::string KinematicVoxelManager::add(const std::string& idHint,
                                        std::vector<KinematicVoxel> voxels,
                                        const glm::mat4& initialTransform,
                                        const std::string& placedObjectId,
                                        bool skipCollider,
                                        const KinematicSurface& surface,
                                        const glm::vec3& worldToLocalShift)
{
    std::string id = generateId(idHint);

    KinematicVoxelObject obj;
    obj.id             = id;
    obj.placedObjectId = placedObjectId;
    obj.surface        = surface;
    obj.voxels         = std::move(voxels);
    obj.currentTransform = initialTransform;

    // F3: with a world->local shift, billboarded-leaf voxels with grid identity become
    // FOLIAGE CARDS (skip their solid faces); everything else builds faces as before.
    const bool foliageCapable = (worldToLocalShift.x < 3.0e38f);
    if (foliageCapable) {
        auto& reg = MaterialRegistry::instance();
        std::vector<KinematicVoxel> solid;
        std::vector<const KinematicVoxel*> leaves;
        solid.reserve(obj.voxels.size());
        for (const auto& v : obj.voxels) {
            const MaterialDef* md = reg.getMaterial(v.materialName);
            if (md && md->billboarded && v.gridCell.x != INT_MIN) leaves.push_back(&v);
            else solid.push_back(v);
        }
        obj.faces = buildFaces(solid, surface);
        buildFoliage(obj, leaves, worldToLocalShift);
    } else {
        obj.faces = buildFaces(obj.voxels, surface);
    }

    // Cached local bounding sphere for per-pass culling (center of the voxel
    // AABB + half-diagonal). Computed once — voxel composition is immutable.
    if (!obj.voxels.empty()) {
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(std::numeric_limits<float>::lowest());
        for (const auto& v : obj.voxels) {
            mn = glm::min(mn, v.localPos - v.scale * 0.5f);
            mx = glm::max(mx, v.localPos + v.scale * 0.5f);
        }
        obj.localCenter    = (mn + mx) * 0.5f;
        obj.boundingRadius = glm::length(mx - mn) * 0.5f;
    }

    LOG_INFO_FMT("KinematicVoxelManager", "Added '" << id << "': "
                 << obj.voxels.size() << " voxels, "
                 << obj.faces.size() << " faces, "
                 << obj.foliage.size() << " foliage sprigs");

    m_objects[id] = std::move(obj);
    m_bufferDirty = true;
    return id;
}

void KinematicVoxelManager::remove(const std::string& id) {
    auto it = m_objects.find(id);
    if (it == m_objects.end()) return;

    m_objects.erase(it);
    m_bufferDirty = true;
    LOG_INFO_FMT("KinematicVoxelManager", "Removed '" << id << "'");
}

void KinematicVoxelManager::setTransform(const std::string& id, const glm::mat4& transform) {
    auto it = m_objects.find(id);
    if (it != m_objects.end()) {
        it->second.currentTransform = transform;
    }
}

glm::mat4 KinematicVoxelManager::getTransform(const std::string& id) const {
    auto it = m_objects.find(id);
    return (it != m_objects.end()) ? it->second.currentTransform : glm::mat4(1.0f);
}

void KinematicVoxelManager::clear() {
    m_objects.clear();
    m_bufferDirty = true;
}

// ============================================================================
// Private helpers
// ============================================================================

void KinematicVoxelManager::buildFoliage(KinematicVoxelObject& obj,
                                         const std::vector<const KinematicVoxel*>& leaves,
                                         const glm::vec3& worldToLocalShift)
{
    obj.foliage.clear();
    if (leaves.empty()) return;

    // Fragment-local integer frame: min corner of the leaf CELLS. The packed format
    // holds 5 bits per axis (0..31); leaves beyond a 32-cell span are skipped (logged).
    glm::ivec3 minCell(INT_MAX);
    for (const auto* v : leaves) minCell = glm::min(minCell, v->gridCell);
    obj.foliageOrigin = glm::vec3(minCell) - worldToLocalShift;   // hinge-local origin

    auto& reg = MaterialRegistry::instance();
    std::unordered_set<uint64_t> emitted;   // one sprig per (cell, subcube slot)
    int skipped = 0;
    for (const auto* v : leaves) {
        glm::ivec3 rel = v->gridCell - minCell;
        if (rel.x > 31 || rel.y > 31 || rel.z > 31) { ++skipped; continue; }
        const glm::ivec3& s = v->gridSlot;
        uint64_t key = (static_cast<uint64_t>(rel.x) << 40) | (static_cast<uint64_t>(rel.y) << 32)
                     | (static_cast<uint64_t>(rel.z) << 24) | (static_cast<uint64_t>(s.x) << 16)
                     | (static_cast<uint64_t>(s.y) << 8)  | static_cast<uint64_t>(s.z);
        if (!emitted.insert(key).second) continue;

        FoliageInstanceData fi;
        fi.packed = (static_cast<uint32_t>(rel.x) & 0x1F)
                  | ((static_cast<uint32_t>(rel.y) & 0x1F) << 5)
                  | ((static_cast<uint32_t>(rel.z) & 0x1F) << 10)
                  | ((static_cast<uint32_t>(s.x) & 0x3) << 15)
                  | ((static_cast<uint32_t>(s.y) & 0x3) << 17)
                  | ((static_cast<uint32_t>(s.z) & 0x3) << 19)
                  | (12u << 21);                       // skylight: bright default (v1; no bake carry)
        fi.tex = static_cast<uint32_t>(reg.getTextureIndex(v->materialName, 0));
        obj.foliage.push_back(fi);
    }
    if (skipped > 0) {
        LOG_WARN_FMT("KinematicVoxelManager", "foliage: " << skipped
                     << " leaf voxels beyond the 32-cell pack range were skipped");
    }
}

// Build GPU face instances from voxels. Performs adjacency culling (skips faces
// between touching voxels of the same scale) and computes per-face UV offsets for
// sub-tile texture mapping.
//
// Texture mapping logic: each face maps two of the three world axes to U,V.
// The UV offset is derived from parentFrac (the voxel's position within its parent
// cube, set during template loading). Per-face axis swaps and Y-flips match the
// static_voxel.vert conventions so textures look identical whether a voxel is
// static (in a chunk) or dynamic (in a KinematicVoxelObject).
//
// For full cubes (scale=1, parentFrac=0): uvOffset=(0,0), uvScale=1 → full texture.
// For microcubes (scale=1/9): uvOffset selects the 1/9 slice → seamless tiling.
std::vector<KinematicFaceData> KinematicVoxelManager::buildFaces(
    const std::vector<KinematicVoxel>& voxels,
    const KinematicSurface& surface)
{
    // Adjacency culling on a PER-OBJECT lattice. Each voxel stamps the cells it
    // covers; a face is culled when EVERY cell across it is occupied. This
    // handles MIXED-scale sets — a felled tree is cubes + subcubes + micros, and
    // the old same-scale-only path culled NOTHING for those (6 faces per voxel,
    // e.g. 3910 wood voxels → 23460 faces → over the render cap → invisible).
    //
    // The lattice is the GCD of every voxel extent in units of the engine's
    // finest cell (1/81): cube = 81, subcube = 27, microcube = 9, fine-grid
    // items 3 or 1. Legacy objects therefore resolve to the historical 1/9
    // lattice (gcd(81,27,9) = 9) — behavior-identical and cheap — while
    // fine-grid item objects resolve to their own grid cell, which the old
    // hardcoded 1/9 lattice collapsed (distinct 1/27 voxels rounded onto one
    // cell -> over- AND mis-culling). GCD rather than min-scale because a fine
    // object whose thinnest box spans 2 cells may also hold 3-cell extents —
    // there is no 2-cell lattice that stamps both exactly. Spans are per-axis:
    // merged fine boxes are non-cubic, and the old scale.x-only span mis-
    // stamped slabs. NOTE: the parse contract (no mixing V with C/S/M) is what
    // keeps this cheap — a full cube in a 1/81-lattice object would stamp 81^3
    // cells, but no such object can exist.
    constexpr int kFinePerCube = 81;
    int g = 0;
    for (const auto& v : voxels) {
        for (int a = 0; a < 3; ++a) {
            const int units = std::max(1, static_cast<int>(std::round(v.scale[a] * kFinePerCube)));
            g = g ? std::gcd(g, units) : units;
        }
    }
    if (g <= 0) g = 9;
    const float cell = static_cast<float>(g) / static_cast<float>(kFinePerCube);

    std::unordered_set<int64_t> occupied;
    auto posKey = [](glm::ivec3 p) -> int64_t {
        return (int64_t(p.x) & 0xFFFFF) | ((int64_t(p.y) & 0xFFFFF) << 20) | ((int64_t(p.z) & 0xFFFFF) << 40);
    };
    auto cellBase = [&](const KinematicVoxel& v) -> glm::ivec3 {
        return glm::ivec3(glm::round((v.localPos - v.scale * 0.5f) / cell));
    };
    auto cellSpan = [&](const KinematicVoxel& v) -> glm::ivec3 {
        return glm::ivec3(
            std::max(1, static_cast<int>(std::round(v.scale.x / cell))),
            std::max(1, static_cast<int>(std::round(v.scale.y / cell))),
            std::max(1, static_cast<int>(std::round(v.scale.z / cell))));
    };
    for (const auto& v : voxels) {
        const glm::ivec3 base = cellBase(v);
        const glm::ivec3 n = cellSpan(v);
        for (int x = 0; x < n.x; ++x)
            for (int y = 0; y < n.y; ++y)
                for (int z = 0; z < n.z; ++z)
                    occupied.insert(posKey(base + glm::ivec3(x, y, z)));
    }

    // Face direction offsets: +Z, -Z, +X, -X, +Y, -Y
    static const glm::ivec3 faceDir[6] = {
        {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}
    };

    std::vector<KinematicFaceData> faces;
    faces.reserve(voxels.size() * 3); // expect ~half culled

    // Object extent (object-local space) for planar projected surfaces — the
    // projected image spans the whole footprint along the two non-projection
    // axes. Computed once; only used when surface.active.
    glm::vec3 objMin( FLT_MAX), objMax(-FLT_MAX);
    if (surface.active) {
        for (const auto& v : voxels) {
            objMin = glm::min(objMin, v.localPos - v.scale * 0.5f);
            objMax = glm::max(objMax, v.localPos + v.scale * 0.5f);
        }
    }
    const glm::vec3 objExtent = objMax - objMin;
    // Which world axis each face's normal lies along (0=X,1=Y,2=Z).
    static const int faceNormalAxis[6] = {2, 2, 0, 0, 1, 1};

    // A face is covered when the full in-plane cell layer just outside it is
    // occupied (per-axis spans — merged fine boxes are non-cubic).
    auto faceCovered = [&](const glm::ivec3& base, const glm::ivec3& n, uint32_t faceId) {
        const glm::ivec3 d = faceDir[faceId];
        glm::ivec3 start = base;
        if (d.x > 0) start.x += n.x; else if (d.x < 0) start.x -= 1;
        if (d.y > 0) start.y += n.y; else if (d.y < 0) start.y -= 1;
        if (d.z > 0) start.z += n.z; else if (d.z < 0) start.z -= 1;
        const int axisA = (d.x != 0) ? 1 : 0;   // first in-plane axis
        const int axisB = (d.z != 0) ? 1 : 2;   // second in-plane axis
        for (int a = 0; a < n[axisA]; ++a)
            for (int b = 0; b < n[axisB]; ++b) {
                glm::ivec3 p = start;
                p[axisA] += a; p[axisB] += b;
                if (!occupied.count(posKey(p))) return false;
            }
        return true;
    };

    for (const auto& v : voxels) {
        const glm::ivec3 base = cellBase(v);
        const glm::ivec3 n = cellSpan(v);

        for (uint32_t faceId = 0; faceId < 6; ++faceId) {
            if (faceCovered(base, n, faceId)) continue;

            KinematicFaceData f{};
            f.localPosition = v.localPos;
            f.scale         = v.scale;
            f.textureIndex  = Phyxel::Core::MaterialRegistry::instance().getTextureIndex(v.materialName, (int)faceId);
            // Pack face (bits 0-2) + per-voxel 0xRRGGBB tint (bits 3-26). The shader
            // masks the low 3 bits for the face and decodes the rest as the tint.
            f.faceId        = (faceId & 0x7u) | (v.tint << 3);

            // UV mapping. Two modes, sharing the per-face axis/flip convention
            // of static_voxel.vert so the result tiles seamlessly:
            //   • Projected (this face's normal is on the surface axis): one
            //     image spans the whole object — uvOffset/uvScale are the face's
            //     fraction of the object extent along the two in-plane axes, and
            //     the surface texture replaces the per-voxel material texture.
            //   • Default: per-cube sub-tile mapping (uvScale = voxel scale).
            if (surface.active && faceNormalAxis[faceId] == surface.axis) {
                // Normalized min-corner position and size within the object,
                // per axis (degenerate extent → full image, no divide-by-zero).
                auto fracN = [&](int a) -> float {
                    return objExtent[a] > 1e-6f
                        ? (v.localPos[a] - v.scale[a] * 0.5f - objMin[a]) / objExtent[a] : 0.0f;
                };
                auto sizeN = [&](int a) -> float {
                    return objExtent[a] > 1e-6f ? v.scale[a] / objExtent[a] : 1.0f;
                };
                const glm::vec3 fn(fracN(0), fracN(1), fracN(2));
                const glm::vec3 sn(sizeN(0), sizeN(1), sizeN(2));
                const glm::vec3 mf = glm::vec3(1.0f) - sn;  // per-axis flip pivot
                switch (faceId) {
                    case 0: f.uvOffset = {fn.x,        mf.y - fn.y}; f.uvScale = {sn.x, sn.y}; break; // +Z
                    // -Z: the BACK of a z-projected board — crop-verified to render the art
                    // rotated 180° under the legacy mapping (the user's upside-down smithy,
                    // 2026-08-27). The global 180° fix must hold for MULTI-quad merged
                    // boards: each quad samples the OPPOSITE slice of the image, reversed —
                    // offset 1-fn with negative scale on both axes. (Same-slice reversal
                    // {fn+sn, -sn} flips each quad in place and scrambles the layout;
                    // offset-only variants are no-ops on a single merged quad. Both were
                    // tried and photographed.)
                    case 1: f.uvOffset = {1.0f - fn.x, 1.0f - fn.y}; f.uvScale = {-sn.x, -sn.y}; break; // -Z
                    case 2: f.uvOffset = {mf.z - fn.z, mf.y - fn.y}; f.uvScale = {sn.z, sn.y}; break; // +X
                    case 3: f.uvOffset = {fn.z,        mf.y - fn.y}; f.uvScale = {sn.z, sn.y}; break; // -X
                    case 4: f.uvOffset = {mf.x - fn.x, mf.z - fn.z}; f.uvScale = {sn.x, sn.z}; break; // +Y
                    case 5: f.uvOffset = {fn.x,        mf.z - fn.z}; f.uvScale = {sn.x, sn.z}; break; // -Y
                }
                f.textureIndex = surface.textureIndex;
            } else {
                // Per-cube sub-tile texture mapping (matches static_voxel.vert).
                // Per-AXIS uv extents: cubic voxels (all legacy content) behave
                // exactly as the old scalar path; non-cubic merged fine boxes
                // get each face's true in-plane rectangle of the cube texture.
                const glm::vec3 s = glm::min(v.scale, glm::vec3(1.0f));
                const glm::vec3 mf = glm::vec3(1.0f) - s;  // per-axis flip pivot

                glm::vec3 pf = v.parentFrac;
                switch (faceId) {
                    case 0: // +Z (Front): U=X, V=flip(Y)
                        f.uvOffset = glm::vec2(pf.x, mf.y - pf.y);        f.uvScale = glm::vec2(s.x, s.y); break;
                    case 1: // -Z (Back): U=X, V=Y
                        f.uvOffset = glm::vec2(pf.x, pf.y);               f.uvScale = glm::vec2(s.x, s.y); break;
                    case 2: // +X (Right): U=flip(Z), V=flip(Y)
                        f.uvOffset = glm::vec2(mf.z - pf.z, mf.y - pf.y); f.uvScale = glm::vec2(s.z, s.y); break;
                    case 3: // -X (Left): U=Z, V=flip(Y)
                        f.uvOffset = glm::vec2(pf.z, mf.y - pf.y);        f.uvScale = glm::vec2(s.z, s.y); break;
                    case 4: // +Y (Top): U=flip(X), V=flip(Z)
                        f.uvOffset = glm::vec2(mf.x - pf.x, mf.z - pf.z); f.uvScale = glm::vec2(s.x, s.z); break;
                    case 5: // -Y (Bottom): U=X, V=flip(Z)
                        f.uvOffset = glm::vec2(pf.x, mf.z - pf.z);        f.uvScale = glm::vec2(s.x, s.z); break;
                }
            }

            faces.push_back(f);
        }
    }
    return faces;
}

glm::vec3 KinematicVoxelManager::computeHalfExtents(const std::vector<KinematicVoxel>& voxels) {
    if (voxels.empty()) return glm::vec3(0.5f);

    glm::vec3 mn( FLT_MAX);
    glm::vec3 mx(-FLT_MAX);

    for (const auto& v : voxels) {
        glm::vec3 h = v.scale * 0.5f;
        mn = glm::min(mn, v.localPos - h);
        mx = glm::max(mx, v.localPos + h);
    }
    return (mx - mn) * 0.5f;
}

std::string KinematicVoxelManager::generateId(const std::string& hint) {
    int& counter = m_idCounters[hint];
    ++counter;
    return hint + "_" + std::to_string(counter);
}

} // namespace Core
} // namespace Phyxel
