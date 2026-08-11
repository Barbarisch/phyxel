#include "core/ItemPropManager.h"

#include "core/ItemEffectSystem.h"
#include "core/ItemRegistry.h"
#include "core/KinematicVoxelManager.h"
#include "core/MaterialRegistry.h"
#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"
#include "core/ChunkManager.h"
#include "utils/Logger.h"

#include "physics/VoxelDynamicsWorld.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Phyxel {
namespace Core {

namespace {
// Resolve a template's authored # surface: into the render-side descriptor:
// the surface texture name → atlas layer (via the material texture cache), and
// the axis char → 0/1/2. Inactive when the template has no surface texture.
KinematicSurface surfaceFromTemplate(const VoxelTemplate& tmpl) {
    KinematicSurface surf;
    if (!tmpl.surface.active()) return surf;
    int axis = (tmpl.surface.axis == 'x') ? 0 : (tmpl.surface.axis == 'z') ? 2 : 1;
    // Sample the surface material on its dominant (positive-axis) face. A surface
    // material carries one albedo, so any face index resolves to the same layer.
    static const int axisPosFace[3] = {2 /*+X*/, 4 /*+Y*/, 0 /*+Z*/};
    surf.active = true;
    surf.axis = static_cast<uint8_t>(axis);
    surf.textureIndex = MaterialRegistry::instance().getTextureIndex(tmpl.surface.texture,
                                                                     axisPosFace[axis]);
    return surf;
}
}  // namespace

namespace {
// Greedy box decomposition of a fine-grid template (finer-than-microcube item
// class). Cells merge along X, then Y, then Z — a fixed scan order, so output
// is deterministic — when they share (material, tint, state, part). Boxes
// never cross a cube boundary: the kinematic sub-tile UV mapping expresses a
// face as a rectangle of its parent cube's texture, so an extent within one
// cube keeps every UV in [0,1] (a multi-cube box would need texture tiling
// the 48-byte KinematicFaceData cannot express).
std::vector<KinematicVoxel> mergeFineVoxels(const VoxelTemplate& tmpl) {
    const int N = tmpl.fineGridResolution;
    const float cell = 1.0f / static_cast<float>(N);

    // Appearance key per cell index; cells sorted for scan-order determinism.
    struct Cell { glm::ivec3 p; const TemplateFineVoxel* fv; };
    std::vector<Cell> cells;
    cells.reserve(tmpl.fineVoxels.size());
    for (const auto& fv : tmpl.fineVoxels) cells.push_back({fv.pos, &fv});
    std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
        return std::tie(a.p.x, a.p.y, a.p.z) < std::tie(b.p.x, b.p.y, b.p.z);
    });

    auto key = [](const glm::ivec3& p) -> int64_t {
        return (int64_t(p.x) & 0xFFFFF) | ((int64_t(p.y) & 0xFFFFF) << 20)
             | ((int64_t(p.z) & 0xFFFFF) << 40);
    };
    std::unordered_map<int64_t, const TemplateFineVoxel*> occ;
    occ.reserve(cells.size() * 2);
    for (const auto& c : cells) occ[key(c.p)] = c.fv;

    auto same = [](const TemplateFineVoxel* a, const TemplateFineVoxel* b) {
        return a && b && a->material == b->material && a->tint == b->tint
            && a->state == b->state && a->partId == b->partId;
    };
    auto cubeOf = [N](int c) { return c >= 0 ? c / N : (c - N + 1) / N; };

    std::unordered_set<int64_t> used;
    used.reserve(cells.size() * 2);
    std::vector<KinematicVoxel> out;

    for (const auto& c : cells) {
        if (used.count(key(c.p))) continue;
        const TemplateFineVoxel* ref = c.fv;
        glm::ivec3 base = c.p, span(1);

        // Extend +X while the next cell matches, is unused, and stays in-cube.
        while (true) {
            glm::ivec3 n(base.x + span.x, base.y, base.z);
            auto it = occ.find(key(n));
            if (it == occ.end() || !same(it->second, ref) || used.count(key(n))
                || cubeOf(n.x) != cubeOf(base.x)) break;
            ++span.x;
        }
        // Extend +Y a full X-run at a time.
        auto rowOk = [&](int y, int z) {
            if (cubeOf(y) != cubeOf(base.y) || cubeOf(z) != cubeOf(base.z)) return false;
            for (int x = 0; x < span.x; ++x) {
                glm::ivec3 n(base.x + x, y, z);
                auto it = occ.find(key(n));
                if (it == occ.end() || !same(it->second, ref) || used.count(key(n)))
                    return false;
            }
            return true;
        };
        while (rowOk(base.y + span.y, base.z)) ++span.y;
        // Extend +Z a full XY-slab at a time.
        auto slabOk = [&](int z) {
            for (int y = 0; y < span.y; ++y)
                if (!rowOk(base.y + y, z)) return false;
            return true;
        };
        while (slabOk(base.z + span.z)) ++span.z;

        for (int x = 0; x < span.x; ++x)
            for (int y = 0; y < span.y; ++y)
                for (int z = 0; z < span.z; ++z)
                    used.insert(key(base + glm::ivec3(x, y, z)));

        KinematicVoxel v;
        v.scale    = glm::vec3(span) * cell;
        v.localPos = glm::vec3(base) * cell + v.scale * 0.5f;
        // Position within the parent cube [0,1) for sub-tile UV mapping.
        v.parentFrac = glm::vec3(base.x % N, base.y % N, base.z % N) * cell;
        v.materialName = ref->material;
        v.tint = ref->tint;
        out.push_back(v);
    }
    return out;
}
}  // namespace

bool ItemPropManager::worldAabb(const std::string& placedObjectId,
                                glm::vec3& lo, glm::vec3& hi) const {
    const Prop* p = get(placedObjectId);
    if (!p) return false;
    lo = glm::vec3(std::numeric_limits<float>::max());
    hi = glm::vec3(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c((i & 1) ? p->localHi.x : p->localLo.x,
                          (i & 2) ? p->localHi.y : p->localLo.y,
                          (i & 4) ? p->localHi.z : p->localLo.z);
        const glm::vec3 w = glm::vec3(p->lastTransform * glm::vec4(c, 1.0f));
        lo = glm::min(lo, w);
        hi = glm::max(hi, w);
    }
    return true;
}

std::vector<KinematicVoxel> ItemPropManager::voxelsFromTemplate(const VoxelTemplate& tmpl) {
    // Fine-grid templates hold ALL geometry in the fine tier (parse contract);
    // greedy-merge it into arbitrary-scale boxes the renderer draws directly.
    if (tmpl.isFineGrid()) return mergeFineVoxels(tmpl);

    std::vector<KinematicVoxel> voxels;
    voxels.reserve(tmpl.cubes.size() + tmpl.subcubes.size() + tmpl.microcubes.size());

    for (const auto& cube : tmpl.cubes) {
        KinematicVoxel v;
        v.localPos     = glm::vec3(cube.relativePos) + glm::vec3(0.5f);
        v.scale        = glm::vec3(1.0f);
        v.parentFrac   = glm::vec3(0.0f);
        v.materialName = cube.material;
        voxels.push_back(v);
    }
    for (const auto& sub : tmpl.subcubes) {
        KinematicVoxel v;
        constexpr float subScale = 1.0f / 3.0f;
        v.localPos = glm::vec3(sub.parentRelativePos)
                   + glm::vec3(sub.subcubePos) * subScale
                   + glm::vec3(subScale * 0.5f);
        v.scale        = glm::vec3(subScale);
        v.parentFrac   = glm::vec3(sub.subcubePos) * subScale;
        v.materialName = sub.material;
        voxels.push_back(v);
    }
    for (const auto& micro : tmpl.microcubes) {
        KinematicVoxel v;
        constexpr float subScale   = 1.0f / 3.0f;
        constexpr float microScale = 1.0f / 9.0f;
        v.localPos = glm::vec3(micro.parentRelativePos)
                   + glm::vec3(micro.subcubePos) * subScale
                   + glm::vec3(micro.microcubePos) * microScale
                   + glm::vec3(microScale * 0.5f);
        v.scale        = glm::vec3(microScale);
        v.parentFrac   = glm::vec3(micro.subcubePos) * subScale
                       + glm::vec3(micro.microcubePos) * microScale;
        v.materialName = micro.material;
        voxels.push_back(v);
    }
    return voxels;
}

const VoxelTemplate* ItemPropManager::resolveItemTemplate(const std::string& templateFile) const {
    if (!m_templates || templateFile.empty()) return nullptr;

    // Resolve by EXACT path: the registry key is the relative path without
    // extension ("items/torch"), NEVER the bare stem — the startup scan
    // preloads root-level templates by stem, and a legacy root file with the
    // same stem used to silently substitute for the item's real model
    // (found 2026-08-06: root BlockSmith relics shadowed items/lantern etc.).
    std::string key = templateFile;
    std::replace(key.begin(), key.end(), '\\', '/');
    if (const auto dot = key.rfind(".voxel"); dot != std::string::npos) key.erase(dot);

    if (const auto* t = m_templates->getTemplate(key)) return t;

    std::string path = "resources/templates/" + templateFile;
    if (fs::path(path).extension().empty()) path += ".voxel";
    if (fs::exists(path) && m_templates->loadTemplate(path, key))
        return m_templates->getTemplate(key);

    LOG_WARN("ItemPropManager", "Template '{}' not found (tried '{}')", templateFile, path);
    return nullptr;
}

bool ItemPropManager::physicalizeProp(Prop& prop, const glm::vec3& comWorldPos,
                                      const glm::quat& orientation,
                                      const glm::vec3& initialVelocity) {
    if (!m_dynamics || prop.localBoxes.empty()) return false;

    // Concurrent-dynamic cap (narrowphase pairs grow as C(n,2) × M × N):
    // evict the OLDEST dynamic prop by retiring it in place — the furniture
    // MAX_DYNAMIC pattern.
    int dynamicCount = 0;
    Prop* oldest = nullptr;
    for (auto& [id, p] : m_props) {
        if (!p.dynamic) continue;
        ++dynamicCount;
        if (!oldest || p.dynamicSeq < oldest->dynamicSeq) oldest = &p;
    }
    if (dynamicCount >= kMaxDynamicItems && oldest && oldest != &prop)
        retireProp(*oldest, oldest->lastTransform);
    auto* body = m_dynamics->createBody(prop.localBoxes, comWorldPos, orientation,
                                        0.25f /*restitution*/, 0.6f /*friction*/,
                                        0.25f /*linearDamp*/, 0.35f /*angularDamp*/);
    if (!body) return false;
    // Items lie around indefinitely — opt out of cleanupDead's 30 s reaper
    // (the furniture pattern; without this a dropped sword evaporates).
    body->lifetime = FLT_MAX;
    body->buoyancy = 1.2f;   // dropped items bob rather than sink outright
    body->linearVelocity = initialVelocity;
    // Small deterministic angular nudge (position-hashed): nothing spawns
    // perfectly balanced, so island sleep can only freeze true rest poses.
    const uint32_t h = static_cast<uint32_t>(static_cast<int64_t>(comWorldPos.x * 73856093.0f))
                     ^ static_cast<uint32_t>(static_cast<int64_t>(comWorldPos.y * 19349663.0f))
                     ^ static_cast<uint32_t>(static_cast<int64_t>(comWorldPos.z * 83492791.0f));
    auto jitter = [h](uint32_t shift) {
        return (float((h >> shift) & 0xFFu) / 255.0f - 0.5f) * 0.6f;
    };
    body->angularVelocity += glm::vec3(jitter(0), jitter(8), jitter(16));
    body->wake();
    prop.bodyId = body->id;
    prop.dynamic = true;
    prop.restDwell = 0.0f;
    prop.tipAssists = 0;
    prop.dynamicSeq = ++m_dynamicSeq;
    return true;
}

void ItemPropManager::retireProp(Prop& prop, const glm::mat4& pose) {
    if (m_dynamics && prop.bodyId != 0) {
        if (auto* body = m_dynamics->getBodyById(prop.bodyId))
            m_dynamics->removeBody(body);
    }
    prop.bodyId = 0;
    prop.dynamic = false;
    prop.restDwell = 0.0f;
    prop.lastTransform = pose;
    if (m_kinematic && !prop.kinId.empty()) m_kinematic->setTransform(prop.kinId, pose);
    syncPlacedPose(prop, pose);
}

void ItemPropManager::syncPlacedPose(const Prop& prop, const glm::mat4& pose) {
    if (!m_placed) return;
    glm::vec3 wlo(std::numeric_limits<float>::max()), whi(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c((i & 1) ? prop.localHi.x : prop.localLo.x,
                          (i & 2) ? prop.localHi.y : prop.localLo.y,
                          (i & 4) ? prop.localHi.z : prop.localLo.z);
        const glm::vec3 w = glm::vec3(pose * glm::vec4(c, 1.0f));
        wlo = glm::min(wlo, w);
        whi = glm::max(whi, w);
    }
    m_placed->updateItemPropPose(prop.placedObjectId,
                                 glm::ivec3(glm::floor(wlo)), glm::ivec3(glm::ceil(whi)));
}

void ItemPropManager::update(float dt, const glm::vec3& playerPos, const glm::vec3& playerVel) {
    (void)playerPos; (void)playerVel;  // STATIC-FIRST: no proximity revive —
                                       // items wake only on drop/throw/hit.
    if (!m_dynamics) return;
    int dynamicCount = 0;
    for (const auto& [id, prop] : m_props)
        if (prop.dynamic) ++dynamicCount;
    for (auto& [id, prop] : m_props) {
        if (prop.dynamic) {
            auto* body = m_dynamics->getBodyById(prop.bodyId);
            if (!body) {
                // Body killed externally (void fall / world clear): freeze the
                // prop at its last synced pose rather than crashing or leaking.
                prop.bodyId = 0;
                prop.dynamic = false;
                retireProp(prop, prop.lastTransform);
                continue;
            }
            const glm::mat4 pose = glm::translate(glm::mat4(1.0f), body->position)
                                 * glm::mat4_cast(body->orientation);
            prop.lastTransform = pose;
            if (m_kinematic) m_kinematic->setTransform(prop.kinId, pose);
            // Keep [E] Take on the moving item: the pickup point is a static
            // bbox snapshot unless we push the pose into the placed object.
            syncPlacedPose(prop, pose);

            if (body->isAsleep) {
                // Tip-over assist: sleep caught a slow topple while the item
                // is still standing — nudge it over instead of freezing it
                // leaning on nothing (see kTipAssistUprightness). Skipped when
                // >2 items simulate: assists keep whole islands awake, and in
                // a pile a leaning item is usually resting on a neighbor.
                const glm::vec3 worldLong = body->orientation * glm::vec3(0, 1, 0);
                if (dynamicCount <= 2
                    && prop.elongated && std::abs(worldLong.y) > kTipAssistUprightness
                    && prop.tipAssists < kTipAssistMax) {
                    ++prop.tipAssists;
                    glm::vec3 axis = glm::cross(glm::vec3(0, 1, 0), worldLong);
                    const float len = glm::length(axis);
                    axis = (len > 1e-3f) ? axis / len : glm::vec3(1, 0, 0);
                    body->wake();
                    body->angularVelocity += axis * 0.8f;   // tips the long axis floorward
                    prop.restDwell = 0.0f;
                } else {
                    prop.restDwell += dt;
                    if (prop.restDwell >= kRestRetireSeconds) retireProp(prop, pose);
                }
            } else {
                prop.restDwell = 0.0f;
            }
        }
        // (Walk-through bump-revive REMOVED 2026-08-07: static-first — settled
        // items wake only via hitProp / drop. It re-ignited whole piles on
        // mere proximity.)
    }
}

bool ItemPropManager::hitProp(const std::string& placedObjectId, const glm::vec3& impulse) {
    auto it = m_props.find(placedObjectId);
    if (it == m_props.end()) return false;
    Prop& prop = it->second;
    if (prop.dynamic) {
        if (auto* body = m_dynamics ? m_dynamics->getBodyById(prop.bodyId) : nullptr) {
            body->wake();
            body->applyCentralImpulse(impulse);
        }
        return true;
    }
    if (prop.localBoxes.empty()) return false;  // rebuilt-from-DB props have no compound
    const glm::vec3 com(prop.lastTransform[3]);
    const glm::quat q = glm::quat_cast(glm::mat3(prop.lastTransform));
    if (!physicalizeProp(prop, com, q, glm::vec3(0.0f))) return false;
    if (auto* body = m_dynamics->getBodyById(prop.bodyId))
        body->applyCentralImpulse(impulse);
    return true;
}

// Coarse GEOMETRY-ONLY collision compound (2026-08-07 perf plan): narrowphase
// cost is quadratic in per-body box count, so the collider must NOT be the
// render mesh (a longsword renders as ~37 material-split boxes). Rasterize the
// voxels' occupancy onto a small per-axis grid over the bounds and greedy-merge
// — coarsening the grid until the budget holds. Conservative: a cell is solid
// if ANY voxel overlaps it, so the collider never underfills the silhouette.
static std::vector<Physics::LocalBox> buildCoarseCollider(
    const std::vector<KinematicVoxel>& voxels,
    const glm::vec3& lo, const glm::vec3& hi,
    const glm::vec3& com, float totalMass, int maxBoxes) {
    const glm::vec3 dims = glm::max(hi - lo, glm::vec3(1e-4f));

    for (int div = 4; div >= 1; --div) {
        glm::ivec3 n;
        for (int a = 0; a < 3; ++a) {
            // Proportional resolution: the longest axis gets `div` cells,
            // shorter axes proportionally fewer (min 1) — keeps cells cubish.
            const float longest = std::max({dims.x, dims.y, dims.z});
            n[a] = std::max(1, (int)std::round(div * dims[a] / longest));
        }
        const glm::vec3 cell = dims / glm::vec3(n);

        std::vector<bool> occ((size_t)n.x * n.y * n.z, false);
        auto idx = [&](int x, int y, int z) { return (size_t)(x + n.x * (y + n.y * z)); };
        for (const auto& v : voxels) {
            const glm::vec3 vmn = v.localPos - v.scale * 0.5f - lo;
            const glm::vec3 vmx = v.localPos + v.scale * 0.5f - lo;
            glm::ivec3 c0, c1;
            for (int a = 0; a < 3; ++a) {
                c0[a] = glm::clamp((int)std::floor(vmn[a] / cell[a]), 0, n[a] - 1);
                c1[a] = glm::clamp((int)std::floor((vmx[a] - 1e-5f) / cell[a]), 0, n[a] - 1);
            }
            for (int x = c0.x; x <= c1.x; ++x)
                for (int y = c0.y; y <= c1.y; ++y)
                    for (int z = c0.z; z <= c1.z; ++z)
                        occ[idx(x, y, z)] = true;
        }

        // Greedy X→Y→Z merge of solid cells into boxes.
        std::vector<bool> used((size_t)n.x * n.y * n.z, false);
        std::vector<Physics::LocalBox> boxes;
        float volumeSum = 0.0f;
        for (int z = 0; z < n.z && (int)boxes.size() <= maxBoxes; ++z)
        for (int y = 0; y < n.y && (int)boxes.size() <= maxBoxes; ++y)
        for (int x = 0; x < n.x && (int)boxes.size() <= maxBoxes; ++x) {
            if (!occ[idx(x, y, z)] || used[idx(x, y, z)]) continue;
            glm::ivec3 base(x, y, z), span(1);
            while (base.x + span.x < n.x && occ[idx(base.x + span.x, y, z)]
                   && !used[idx(base.x + span.x, y, z)]) ++span.x;
            auto rowOk = [&](int yy, int zz) {
                for (int xx = 0; xx < span.x; ++xx)
                    if (!occ[idx(base.x + xx, yy, zz)] || used[idx(base.x + xx, yy, zz)])
                        return false;
                return true;
            };
            while (base.y + span.y < n.y && rowOk(base.y + span.y, z)) ++span.y;
            auto slabOk = [&](int zz) {
                for (int yy = 0; yy < span.y; ++yy)
                    if (!rowOk(base.y + yy, zz)) return false;
                return true;
            };
            while (base.z + span.z < n.z && slabOk(base.z + span.z)) ++span.z;
            for (int xx = 0; xx < span.x; ++xx)
                for (int yy = 0; yy < span.y; ++yy)
                    for (int zz = 0; zz < span.z; ++zz)
                        used[idx(base.x + xx, base.y + yy, base.z + zz)] = true;

            Physics::LocalBox b;
            const glm::vec3 bmn = lo + glm::vec3(base) * cell;
            const glm::vec3 bsz = glm::vec3(span) * cell;
            b.halfExtents = bsz * 0.5f;
            b.offset = bmn + b.halfExtents - com;   // body frame is COM-centered
            b.mass = bsz.x * bsz.y * bsz.z;          // volume; scaled below
            volumeSum += b.mass;
            boxes.push_back(b);
        }

        if ((int)boxes.size() <= maxBoxes && !boxes.empty()) {
            // Distribute the ORIGINAL material-weighted total mass by volume.
            const float scale = totalMass / std::max(volumeSum, 1e-6f);
            for (auto& b : boxes) b.mass *= scale;
            return boxes;
        }
    }
    // Fallback: one AABB box (cannot happen with div=1 unless voxels empty).
    Physics::LocalBox b;
    b.halfExtents = glm::max((hi - lo) * 0.5f, glm::vec3(1e-3f));
    b.offset = (lo + hi) * 0.5f - com;
    b.mass = totalMass;
    return {b};
}

ItemPropManager::PropGeometry ItemPropManager::buildPropGeometry(
        const ItemDefinition& def, const VoxelTemplate& tmpl, const glm::vec3& position,
        float yawDeg, bool dynamic, bool snapToGround) {
    PropGeometry g;
    g.voxels = voxelsFromTemplate(tmpl);
    if (g.voxels.empty()) return g;

    // Bake held.scale straight into the geometry (legacy full-cube templates
    // ship undersized-by-scale): physics boxes and the render group then share
    // one unscaled local frame, which the rigid body's pose can drive directly
    // (VoxelDynamicsWorld has no scale concept).
    g.scale = def.held.scale > 0.0f ? def.held.scale : 1.0f;
    if (g.scale != 1.0f)
        for (auto& v : g.voxels) { v.localPos *= g.scale; v.scale *= g.scale; }

    // Local bounds + collision compound + material-weighted COM. The fine-item
    // merged boxes ARE the compound (maul = 21 boxes); legacy C/S/M items get
    // one box per voxel (small counts). Mass = material mass x volume, then
    // normalized into a sane gameplay band while preserving the distribution
    // (raw volumetric masses for hand items are ~0.01 — too light to solve
    // stably against the character and furniture).
    auto& matReg = MaterialRegistry::instance();
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    glm::vec3 com(0.0f);
    float rawMass = 0.0f;
    for (const auto& v : g.voxels) {
        lo = glm::min(lo, v.localPos - v.scale * 0.5f);
        hi = glm::max(hi, v.localPos + v.scale * 0.5f);
        const float vol = v.scale.x * v.scale.y * v.scale.z;
        const float m = std::max(1e-4f, matReg.getPhysics(v.materialName).mass * vol);
        com += v.localPos * m;
        rawMass += m;
    }
    com /= rawMass;
    g.com = com;
    const float totalMass = glm::clamp(rawMass * 100.0f, 0.8f, 10.0f);
    g.boxes = buildCoarseCollider(g.voxels, lo, hi, com, totalMass, kMaxColliderBoxes);

    glm::vec3 pos = position;
    if (snapToGround && m_chunks) {
        int x = (int)std::floor(pos.x), z = (int)std::floor(pos.z);
        int y = (int)std::floor(pos.y);
        for (int i = 0; i < 32; ++i, --y) {
            if (m_chunks->hasVoxelAt(glm::ivec3(x, y, z))) { pos.y = float(y + 1); break; }
        }
    }

    g.orientation = glm::angleAxis(glm::radians(yawDeg), glm::vec3(0, 1, 0));
    // PHYSICS SPAWN POSE: elongated items (swords, staffs, spears...) go in
    // LYING DOWN when spawned DYNAMIC. Spawned standing on their base they are
    // balanced enough for island sleep to freeze them upright (or mid-topple)
    // — verified live: a sword frozen tip-down reads as levitation. STATIC
    // spawns (the default under static-first) keep the upright display pose —
    // authored placement wants shelves/racks/tables to look composed.
    const glm::vec3 dims = hi - lo;
    g.elongated = dims.y > 1.4f * std::max(dims.x, dims.z);
    if (m_dynamics && dynamic && g.elongated)
        g.orientation = g.orientation * glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0));

    // Rest height from the ROTATED bounds: the item's lowest rotated corner
    // sits on pos.y (tiny lift so the body starts contact-free), centered
    // horizontally on the requested position.
    g.localLo = lo - com;
    g.localHi = hi - com;
    glm::vec3 rlo(std::numeric_limits<float>::max()), rhi(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c((i & 1) ? g.localHi.x : g.localLo.x,
                          (i & 2) ? g.localHi.y : g.localLo.y,
                          (i & 4) ? g.localHi.z : g.localLo.z);
        const glm::vec3 w = g.orientation * c;
        rlo = glm::min(rlo, w);
        rhi = glm::max(rhi, w);
    }
    // Rest EXACTLY on pos.y when static (no body -> no contact to resolve; the
    // old unconditional 0.02 lift + the caller's own epsilon read as items
    // hovering ~3 cm above their shelf). Dynamic spawns keep the contact-free
    // starting gap; a 0.003 static epsilon avoids coplanar-face z-fighting.
    const float restLift = (m_dynamics && dynamic) ? 0.02f : 0.003f;
    g.basePos = pos;
    g.comWorld = glm::vec3(pos.x, pos.y - rlo.y + restLift, pos.z);
    g.transform = glm::translate(glm::mat4(1.0f), g.comWorld) * glm::mat4_cast(g.orientation);
    g.ok = true;
    return g;
}

void ItemPropManager::writeExactPose(const std::string& placedObjectId,
                                     const glm::vec3& basePos, float yawDeg) {
    if (!m_placed) return;
    m_placed->setMetadata(placedObjectId, "pose",
                          nlohmann::json::array({basePos.x, basePos.y, basePos.z, yawDeg}));
}

std::string ItemPropManager::spawnProp(const std::string& itemId, const glm::vec3& position,
                                       float yawDeg, bool snapToGround, const std::string& instanceUuid,
                                       const glm::vec3& initialVelocity, bool dynamic) {
    if (!m_placed || !m_kinematic) return "";

    const auto* def = ItemRegistry::instance().getItem(itemId);
    if (!def) {
        LOG_WARN("ItemPropManager", "spawnProp: unknown item '{}'", itemId);
        return "";
    }
    if (!def->holdable || def->templateFile.empty()) {
        LOG_WARN("ItemPropManager", "spawnProp: item '{}' is not holdable / has no template", itemId);
        return "";
    }
    const VoxelTemplate* tmpl = resolveItemTemplate(def->templateFile);
    if (!tmpl) return "";

    // ONE geometry path, shared with the reload path (see buildPropGeometry):
    // a restored prop must be identical to the one that was saved.
    PropGeometry g = buildPropGeometry(*def, *tmpl, position, yawDeg, dynamic, snapToGround);
    if (!g.ok) {
        LOG_WARN("ItemPropManager", "spawnProp: template '{}' has no voxels", def->templateFile);
        return "";
    }
    auto voxels = std::move(g.voxels);
    const float propScale = g.scale;
    const glm::vec3 com = g.com;
    std::vector<Physics::LocalBox> boxes = std::move(g.boxes);
    const glm::quat orientation = g.orientation;
    const bool elongated = g.elongated;
    const glm::vec3 comWorld = g.comWorld;
    const glm::mat4 transform = g.transform;
    const glm::vec3 pos = g.basePos;   // post-snapToGround resting base
    const glm::vec3 llo0 = g.localLo, lhi0 = g.localHi;
    glm::vec3 rlo(std::numeric_limits<float>::max()), rhi(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c((i & 1) ? lhi0.x : llo0.x, (i & 2) ? lhi0.y : llo0.y,
                          (i & 4) ? lhi0.z : llo0.z);
        const glm::vec3 w = orientation * c;
        rlo = glm::min(rlo, w);
        rhi = glm::max(rhi, w);
    }

    // Render group lives in the same COM-centered frame as the body.
    for (auto& v : voxels) v.localPos -= com;

    // Conservative world AABB from the rotated bounds.
    const glm::vec3 llo = llo0, lhi = lhi0;
    const glm::vec3 wlo = comWorld + rlo, whi = comWorld + rhi;

    std::string kinId = m_kinematic->add("itemprop_" + itemId, std::move(voxels), transform,
                                         "", false, surfaceFromTemplate(*tmpl));

    std::string placedId = m_placed->registerItemProp(
        itemId, tmpl->name,
        glm::ivec3(glm::floor(pos)), (int)yawDeg,
        glm::ivec3(glm::floor(wlo)), glm::ivec3(glm::ceil(whi)),
        def->name, def->fixed);
    if (placedId.empty()) {
        m_kinematic->remove(kinId);
        return "";
    }

    // Persist the item-instance uuid on the prop's placed object so drop→reload→pickup keeps identity.
    if (!instanceUuid.empty()) m_placed->setMetadata(placedId, "instanceUuid", instanceUuid);
    // ...and the EXACT pose, so reload puts it back where it was. The placed
    // object's own position is an integer cell; restoring from that dropped a
    // hearth log two thirds of a voxel onto the firebox floor.
    writeExactPose(placedId, pos, yawDeg);   // the RESTING pose, not the request

    Prop prop;
    prop.placedObjectId = placedId;
    prop.itemId = itemId;
    prop.kinId = kinId;
    prop.instanceUuid = instanceUuid;
    prop.scale = propScale;
    prop.localCOM = com;
    prop.localLo = llo;
    prop.localHi = lhi;
    prop.localBoxes = std::move(boxes);
    prop.lastTransform = transform;
    prop.elongated = elongated;
    // STATIC-FIRST: only drops/throws/hits simulate. A placed item costs zero
    // physics; the compound is kept on the prop for later hitProp revival.
    if (dynamic)
        physicalizeProp(prop, comWorld, orientation, initialVelocity);

    m_props[placedId] = std::move(prop);
    registerPropEffects(m_props[placedId]);
    LOG_INFO("ItemPropManager", "Spawned item prop '{}' as '{}' at ({}, {}, {}){}",
             itemId, placedId, pos.x, pos.y, pos.z,
             m_props[placedId].dynamic ? " [dynamic]" : "");
    return placedId;
}

void ItemPropManager::registerPropEffects(const Prop& prop) {
    if (!m_effects || !m_kinematic) return;
    const auto* def = ItemRegistry::instance().getItem(prop.itemId);
    if (!def || def->effects.empty()) return;
    KinematicVoxelManager* kin = m_kinematic;
    const std::string kinId = prop.kinId;
    // Effect anchors are TEMPLATE-local (items.json), but the prop's render
    // transform is COM-centered (physics frame) — shift back so a torch's
    // flame sits on its tip, not offset by the center of mass.
    const glm::mat4 comShift = glm::translate(glm::mat4(1.0f), -prop.localCOM);
    m_effects->registerInstance(prop.placedObjectId, def, /*held=*/false,
                                [kin, kinId, comShift]() {
                                    return kin->getTransform(kinId) * comShift;
                                });
}

void ItemPropManager::onPlacedObjectRemoved(const std::string& placedObjectId) {
    auto it = m_props.find(placedObjectId);
    if (it == m_props.end()) return;
    if (m_effects) m_effects->unregisterInstance(placedObjectId);
    // A live rigid body must die with the prop — items pin lifetime=FLT_MAX,
    // so a body left behind here would simulate (and collide) forever.
    if (m_dynamics && it->second.bodyId != 0) {
        if (auto* body = m_dynamics->getBodyById(it->second.bodyId))
            m_dynamics->removeBody(body);
    }
    if (m_kinematic) m_kinematic->remove(it->second.kinId);
    m_props.erase(it);
}

bool ItemPropManager::removeProp(const std::string& placedObjectId) {
    if (!m_props.count(placedObjectId)) return false;
    onPlacedObjectRemoved(placedObjectId);          // kinematic teardown (idempotent)
    if (m_placed) m_placed->remove(placedObjectId); // registry entry (no voxel clear for items)
    return true;
}

std::string ItemPropManager::pickupProp(const std::string& placedObjectId, std::string* outInstanceUuid) {
    auto it = m_props.find(placedObjectId);
    if (it == m_props.end()) return "";
    std::string itemId = it->second.itemId;
    if (outInstanceUuid) *outInstanceUuid = it->second.instanceUuid;
    removeProp(placedObjectId);
    return itemId;
}

void ItemPropManager::rebuildFromPlacedObjects() {
    if (!m_placed || !m_kinematic) return;

    // Prune props whose placed object vanished (e.g. snapshot restore).
    for (auto it = m_props.begin(); it != m_props.end();) {
        if (!m_placed->get(it->first)) {
            m_kinematic->remove(it->second.kinId);
            it = m_props.erase(it);
        } else {
            ++it;
        }
    }

    int rebuilt = 0;
    for (const auto& obj : m_placed->list()) {
        if (obj.category != "item" || m_props.count(obj.id)) continue;
        std::string itemId = obj.metadata.value("itemId", std::string());
        const auto* def = itemId.empty() ? nullptr : ItemRegistry::instance().getItem(itemId);
        const VoxelTemplate* tmpl = def ? resolveItemTemplate(def->templateFile) : nullptr;
        if (!tmpl) {
            LOG_WARN("ItemPropManager", "Cannot rebuild item prop '{}' (item '{}')", obj.id, itemId);
            continue;
        }
        // RESTORE IN PLACE. Prefer the exact pose recorded at spawn; fall back
        // to the placed object's integer cell only for objects saved before
        // that existed. The old code ALWAYS used the integer cell and rebuilt
        // the transform by hand — which both moved the prop (up to a voxel) and
        // left localCOM at zero, so a restored torch's flame hung off its model.
        glm::vec3 basePos(obj.position);
        float yawDeg = static_cast<float>(obj.rotation);
        if (obj.metadata.contains("pose") && obj.metadata["pose"].is_array() &&
            obj.metadata["pose"].size() >= 4) {
            const auto& p = obj.metadata["pose"];
            basePos = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
            yawDeg = p[3].get<float>();
        }

        PropGeometry g = buildPropGeometry(*def, *tmpl, basePos, yawDeg,
                                           /*dynamic=*/false, /*snapToGround=*/false);
        if (!g.ok) {
            LOG_WARN("ItemPropManager", "Cannot rebuild item prop '{}' (no voxels)", obj.id);
            continue;
        }
        for (auto& v : g.voxels) v.localPos -= g.com;   // COM-centered, as at spawn

        std::string kinId = m_kinematic->add("itemprop_" + itemId, std::move(g.voxels), g.transform,
                                             "", false, surfaceFromTemplate(*tmpl));
        Prop prop;
        prop.placedObjectId = obj.id;
        prop.itemId = itemId;
        prop.kinId = kinId;
        prop.instanceUuid = obj.metadata.value("instanceUuid", std::string());
        prop.scale = g.scale;
        prop.localCOM = g.com;          // effects anchor off this — it must be real
        prop.localLo = g.localLo;
        prop.localHi = g.localHi;
        prop.localBoxes = g.boxes;      // so a restored item can still be knocked about
        prop.lastTransform = g.transform;
        prop.elongated = g.elongated;
        m_props[obj.id] = std::move(prop);
        registerPropEffects(m_props[obj.id]);
        ++rebuilt;
    }
    if (rebuilt > 0)
        LOG_INFO("ItemPropManager", "Rebuilt {} item props from placed objects", rebuilt);
}

} // namespace Core
} // namespace Phyxel
