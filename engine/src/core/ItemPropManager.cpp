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

    const std::string stem = fs::path(templateFile).stem().string();
    if (const auto* t = m_templates->getTemplate(stem)) return t;

    // On-demand load: the startup scan only covers resources/templates/ root.
    std::string path = "resources/templates/" + templateFile;
    if (fs::path(path).extension().empty()) path += ".voxel";
    if (fs::exists(path) && m_templates->loadTemplate(path))
        return m_templates->getTemplate(stem);

    LOG_WARN("ItemPropManager", "Template '{}' not found (tried '{}')", templateFile, path);
    return nullptr;
}

bool ItemPropManager::physicalizeProp(Prop& prop, const glm::vec3& comWorldPos,
                                      const glm::quat& orientation,
                                      const glm::vec3& initialVelocity) {
    if (!m_dynamics || prop.localBoxes.empty()) return false;
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
    if (!m_dynamics) return;
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
                // leaning on nothing (see kTipAssistUprightness).
                const glm::vec3 worldLong = body->orientation * glm::vec3(0, 1, 0);
                if (prop.elongated && std::abs(worldLong.y) > kTipAssistUprightness
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
        } else if (!prop.localBoxes.empty()) {
            // Settled prop: a moving player walking through it kicks it alive
            // (capsule approximation: 0.9 u radius, 1.6 u tall).
            if (glm::dot(playerVel, playerVel) < 0.25f) continue;  // < 0.5 u/s
            const glm::vec3 com(prop.lastTransform[3]);
            const glm::vec3 d = com - playerPos;
            if (std::abs(d.y) > 1.6f || (d.x * d.x + d.z * d.z) > 0.9f * 0.9f) continue;
            const glm::quat q = glm::quat_cast(glm::mat3(prop.lastTransform));
            physicalizeProp(prop, com, q,
                            playerVel * 0.6f + glm::vec3(0.0f, 0.8f, 0.0f));
        }
    }
}

std::string ItemPropManager::spawnProp(const std::string& itemId, const glm::vec3& position,
                                       float yawDeg, bool snapToGround, const std::string& instanceUuid,
                                       const glm::vec3& initialVelocity) {
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

    auto voxels = voxelsFromTemplate(*tmpl);
    if (voxels.empty()) {
        LOG_WARN("ItemPropManager", "spawnProp: template '{}' has no voxels", def->templateFile);
        return "";
    }

    // Bake held.scale straight into the geometry (legacy full-cube templates
    // ship undersized-by-scale): physics boxes and the render group then share
    // one unscaled local frame, which the rigid body's pose can drive directly
    // (VoxelDynamicsWorld has no scale concept).
    const float propScale = def->held.scale > 0.0f ? def->held.scale : 1.0f;
    if (propScale != 1.0f) {
        for (auto& v : voxels) { v.localPos *= propScale; v.scale *= propScale; }
    }

    // Local bounds + collision compound + material-weighted COM. The fine-item
    // merged boxes ARE the compound (maul = 21 boxes); legacy C/S/M items get
    // one box per voxel (small counts). Mass = material mass x volume, then
    // normalized into a sane gameplay band while preserving the distribution
    // (raw volumetric masses for hand items are ~0.01 — too light to solve
    // stably against the character and furniture).
    auto& matReg = MaterialRegistry::instance();
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    std::vector<Physics::LocalBox> boxes;
    boxes.reserve(voxels.size());
    glm::vec3 com(0.0f);
    float rawMass = 0.0f;
    for (const auto& v : voxels) {
        lo = glm::min(lo, v.localPos - v.scale * 0.5f);
        hi = glm::max(hi, v.localPos + v.scale * 0.5f);
        const float vol = v.scale.x * v.scale.y * v.scale.z;
        const float m = std::max(1e-4f, matReg.getPhysics(v.materialName).mass * vol);
        boxes.push_back({v.localPos, v.scale * 0.5f, m});
        com += v.localPos * m;
        rawMass += m;
    }
    com /= rawMass;
    const float totalMass = glm::clamp(rawMass * 100.0f, 0.8f, 10.0f);
    const float massScale = totalMass / rawMass;
    for (auto& b : boxes) {
        b.offset = b.offset - com;   // body frame is COM-centered
        b.mass *= massScale;
    }

    glm::vec3 pos = position;
    if (snapToGround && m_chunks) {
        int x = (int)std::floor(pos.x), z = (int)std::floor(pos.z);
        int y = (int)std::floor(pos.y);
        for (int i = 0; i < 32; ++i, --y) {
            if (m_chunks->hasVoxelAt(glm::ivec3(x, y, z))) { pos.y = float(y + 1); break; }
        }
    }

    glm::quat orientation = glm::angleAxis(glm::radians(yawDeg), glm::vec3(0, 1, 0));
    // PHYSICS SPAWN POSE: elongated items (swords, staffs, spears...) go in
    // LYING DOWN. Spawned standing on their base they are balanced enough for
    // island sleep to freeze them upright (or mid-topple) — verified live: a
    // sword frozen tip-down reads as levitation. Tipping the long axis
    // horizontal at spawn puts them straight into their natural rest pose.
    // Static fallback (no dynamics world) keeps the upright display pose.
    const glm::vec3 dims = hi - lo;
    const bool elongated = dims.y > 1.4f * std::max(dims.x, dims.z);
    if (m_dynamics && elongated) {
        orientation = orientation * glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0));
    }

    // Rest height from the ROTATED bounds: the item's lowest rotated corner
    // sits on pos.y (tiny lift so the body starts contact-free), centered
    // horizontally on the requested position.
    const glm::vec3 llo0 = lo - com, lhi0 = hi - com;
    glm::vec3 rlo(std::numeric_limits<float>::max()), rhi(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c((i & 1) ? lhi0.x : llo0.x, (i & 2) ? lhi0.y : llo0.y, (i & 4) ? lhi0.z : llo0.z);
        const glm::vec3 w = orientation * c;
        rlo = glm::min(rlo, w);
        rhi = glm::max(rhi, w);
    }
    const glm::vec3 comWorld(pos.x, pos.y - rlo.y + 0.02f, pos.z);
    const glm::mat4 transform = glm::translate(glm::mat4(1.0f), comWorld) * glm::mat4_cast(orientation);

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
        def->name);
    if (placedId.empty()) {
        m_kinematic->remove(kinId);
        return "";
    }

    // Persist the item-instance uuid on the prop's placed object so drop→reload→pickup keeps identity.
    if (!instanceUuid.empty()) m_placed->setMetadata(placedId, "instanceUuid", instanceUuid);

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
    physicalizeProp(prop, comWorld, orientation, initialVelocity);  // no-op without a dynamics world

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
        auto voxels = voxelsFromTemplate(*tmpl);

        const float propScale = def->held.scale > 0.0f ? def->held.scale : 1.0f;
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(obj.position));
        transform = glm::rotate(transform, glm::radians((float)obj.rotation), glm::vec3(0, 1, 0));
        transform = glm::scale(transform, glm::vec3(propScale));

        std::string kinId = m_kinematic->add("itemprop_" + itemId, std::move(voxels), transform,
                                             "", false, surfaceFromTemplate(*tmpl));
        m_props[obj.id] = {obj.id, itemId, kinId, obj.metadata.value("instanceUuid", std::string())};
        registerPropEffects(m_props[obj.id]);
        ++rebuilt;
    }
    if (rebuilt > 0)
        LOG_INFO("ItemPropManager", "Rebuilt {} item props from placed objects", rebuilt);
}

} // namespace Core
} // namespace Phyxel
