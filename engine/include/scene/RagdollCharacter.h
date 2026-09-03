#pragma once

#include "scene/Entity.h"
#include "physics/PhysicsWorld.h"
#include "core/HealthComponent.h"
#include "utils/Logger.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace Phyxel {
namespace Scene {

enum class Faction {
    Player,
    Enemy,
    Neutral
};

struct RagdollPart {
    glm::vec3 scale;
    glm::vec4 color;
    std::string name;
    glm::vec3 offset = glm::vec3(0.0f);
    bool active = true;
    // Direct-transform path used by AnimatedVoxelCharacter bones.
    bool      useDirectTransform = true;
    int       boneGroupId = -1;
    glm::vec3 worldPos = glm::vec3(0.0f);
    glm::quat worldRot = glm::quat(1, 0, 0, 0);
};

class RagdollCharacter : public Entity {
public:
    RagdollCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos)
        : physicsWorld(physicsWorld), faction(Faction::Neutral),
          m_health(std::make_unique<Core::HealthComponent>(100.0f)) {}

    virtual ~RagdollCharacter() = default;

    virtual void update(float deltaTime) override = 0;
    virtual void render(Graphics::RenderCoordinator* renderer) override {}

    virtual void setPosition(const glm::vec3& pos) override {}

    virtual glm::vec3 getPosition() const override { return glm::vec3(0.0f); }

    const std::vector<RagdollPart>& getParts() const { return parts; }

    // Direct-transform parts grouped by boneGroupId. The renderer batches each
    // group into a single instanced draw, so it needs this grouping every frame
    // for both the main and shadow passes. The grouping topology only changes
    // when parts are added/removed, so it is cached and rebuilt lazily instead
    // of rebuilding a std::map per character per pass per frame.
    struct PartGroup {
        int boneGroupId = -1;
        std::vector<int> partIndices;  // indices into parts[]
    };
    const std::vector<PartGroup>& getPartGroups() const {
        if (m_partGroupsDirty || m_partGroupsBuiltSize != parts.size())
            rebuildPartGroups();
        return m_partGroups;
    }

    // ---- Part-count LOD (docs/CharacterPipelineScaling.md P2.3) -------------------
    // A distant character does not need its full part count: a 1116-part humanoid a
    // hundred units away covers a few dozen pixels. Each level merges parts onto a
    // lattice 2^level coarser, WITHIN a bone group so the result still rigs correctly.
    //
    // Only offset/scale/color are stored: the per-frame bone transforms come from the
    // full-resolution part groups, which the animation path already updates. So a LOD
    // level is static per character and is built once, on demand.
    struct LodPart {
        glm::vec3 offset;
        glm::vec3 scale;
        glm::vec4 color;
    };
    struct LodGroupRange {
        int      boneGroupId = -1;
        uint32_t start = 0;      ///< index into LodLevel::parts
        uint32_t count = 0;
    };
    struct LodLevel {
        std::vector<LodPart>      parts;   ///< sorted by boneGroupId, so groups are contiguous
        std::vector<LodGroupRange> groups;
    };
    /// level >= 1. Built on first request and cached; invalidated with the part groups.
    const LodLevel& getLodLevel(int level) const {
        auto it = m_lodCache.find(level);
        if (it != m_lodCache.end()) return it->second;
        LodLevel lvl;
        buildLodLevel(level, lvl);
        return m_lodCache.emplace(level, std::move(lvl)).first->second;
    }
    static constexpr int kMaxLodLevel = 2;

    /// Bumped whenever the STRUCTURE of parts changes (added/removed, or a part's
    /// active flag flipped). offset/scale/color are otherwise immutable — animation
    /// only writes worldPos/worldRot — so renderers can cache a per-character instance
    /// blob and rebuild it only when this changes. See RenderCoordinator's blob cache.
    uint32_t partsVersion() const { return m_partsVersion; }
    void bumpPartsVersion() { ++m_partsVersion; m_lodCache.clear(); }

    /// Whole-character color multiply and opacity, applied where the renderer bakes
    /// its instance blob. This is the ONLY recolor lever that works on rigs whose
    /// boxes carry explicit colors (creature_forge output): the appearance-region
    /// palette only fills boxes that left their color unset. One neutral rig plus a
    /// tint therefore covers a whole palette family — ten dragon colors, a winter
    /// wolf, a polar bear — without a near-duplicate rig per variant.
    /// Alpha < 1 additionally routes the character to the translucent draw.
    void setRenderTint(const glm::vec3& t) {
        if (t == m_renderTint) return;
        m_renderTint = t;
        bumpPartsVersion();   // instance colors are baked — force a blob rebuild
    }
    const glm::vec3& getRenderTint() const { return m_renderTint; }

    void setRenderAlpha(float a) {
        if (a == m_renderAlpha) return;
        m_renderAlpha = a;
        bumpPartsVersion();
    }
    float getRenderAlpha() const { return m_renderAlpha; }
    bool isTranslucent() const { return m_renderAlpha < 0.999f; }

    void setFaction(Faction f) { faction = f; }
    Faction getFaction() const { return faction; }

    virtual void setControlInput(float forward, float turn) {}

    // Health component access. By default each character owns its own
    // HealthComponent. The host can inject an external (non-owning) component
    // via setHealthComponent() so a character SHARES a health store with
    // another system — used to make the player character and the HUD/respawn
    // health a single source of truth (see docs/TurnBasedCombat.md S2). Pass
    // nullptr to revert to the owned component.
    void setHealthComponent(Core::HealthComponent* external) { m_externalHealth = external; }
    Core::HealthComponent* getHealthComponent() override {
        return m_externalHealth ? m_externalHealth : m_health.get();
    }
    const Core::HealthComponent* getHealthComponent() const override {
        return m_externalHealth ? m_externalHealth : m_health.get();
    }

protected:
    // Subclasses must call this after mutating `parts` in a way that does not
    // change its size (e.g. an in-place rebuild). Size changes are detected
    // automatically by getPartGroups().
    void markPartGroupsDirty() { m_partGroupsDirty = true; m_lodCache.clear(); ++m_partsVersion; }

    /// Merge a bone group's parts onto a lattice `2^level` times coarser. Uses the
    /// group's modal part size as the base cell so per-limb scaling is respected, and
    /// averages merged colors. Decimating per group (never across groups) is what keeps
    /// the result riggable — parts in different groups move independently.
    void buildLodLevel(int level, LodLevel& out) const {
        const float factor = static_cast<float>(1 << level);
        out.parts.clear();
        out.groups.clear();

        for (const auto& grp : getPartGroups()) {
            if (grp.partIndices.empty()) continue;

            // Modal size in this group defines the lattice.
            std::unordered_map<uint64_t, int> sizeVotes;
            auto sizeKey = [](const glm::vec3& s) {
                return (static_cast<uint64_t>(static_cast<uint32_t>(s.x * 100000.0f)) << 40)
                     ^ (static_cast<uint64_t>(static_cast<uint32_t>(s.y * 100000.0f)) << 20)
                     ^  static_cast<uint64_t>(static_cast<uint32_t>(s.z * 100000.0f));
            };
            glm::vec3 modal(0.0f); int best = -1;
            for (int pi : grp.partIndices) {
                if (!parts[pi].active) continue;
                int v = ++sizeVotes[sizeKey(parts[pi].scale)];
                if (v > best) { best = v; modal = parts[pi].scale; }
            }
            if (best < 0) continue;

            const glm::vec3 cell = modal * factor;
            if (cell.x <= 0.0f || cell.y <= 0.0f || cell.z <= 0.0f) continue;

            struct Accum { glm::vec4 color{0.0f}; int n = 0; };
            std::unordered_map<uint64_t, Accum> cells;
            std::vector<uint64_t> order;   // keep emission deterministic
            for (int pi : grp.partIndices) {
                const auto& p = parts[pi];
                if (!p.active) continue;
                const int cx = static_cast<int>(std::floor(p.offset.x / cell.x));
                const int cy = static_cast<int>(std::floor(p.offset.y / cell.y));
                const int cz = static_cast<int>(std::floor(p.offset.z / cell.z));
                const uint64_t k = (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 42)
                                 ^ (static_cast<uint64_t>(static_cast<uint32_t>(cy)) << 21)
                                 ^  static_cast<uint64_t>(static_cast<uint32_t>(cz));
                auto it = cells.find(k);
                if (it == cells.end()) { cells.emplace(k, Accum{}); order.push_back(k); it = cells.find(k); }
                it->second.color += p.color;
                it->second.n++;
                // stash the cell origin on first touch via a parallel map entry
                if (it->second.n == 1) {
                    LodPart lp;
                    lp.offset = glm::vec3((cx + 0.5f) * cell.x, (cy + 0.5f) * cell.y, (cz + 0.5f) * cell.z);
                    lp.scale  = cell;
                    lp.color  = glm::vec4(0.0f);   // filled below
                    out.parts.push_back(lp);
                }
            }

            LodGroupRange range;
            range.boneGroupId = grp.boneGroupId;
            range.count = static_cast<uint32_t>(order.size());
            range.start = static_cast<uint32_t>(out.parts.size() - range.count);
            for (size_t i = 0; i < order.size(); ++i) {
                const Accum& a = cells[order[i]];
                out.parts[range.start + i].color = a.n > 0 ? a.color / static_cast<float>(a.n)
                                                           : glm::vec4(1.0f);
            }
            if (range.count > 0) out.groups.push_back(range);
        }
    }

    void rebuildPartGroups() const {
        m_partGroups.clear();
        std::unordered_map<int, size_t> indexByGroup;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (!parts[i].useDirectTransform) continue;
            int g = parts[i].boneGroupId;
            auto it = indexByGroup.find(g);
            size_t idx;
            if (it == indexByGroup.end()) {
                idx = m_partGroups.size();
                indexByGroup.emplace(g, idx);
                m_partGroups.push_back(PartGroup{g, {}});
            } else {
                idx = it->second;
            }
            m_partGroups[idx].partIndices.push_back(static_cast<int>(i));
        }
        m_partGroupsDirty = false;
        m_partGroupsBuiltSize = parts.size();
        m_lodCache.clear();
        ++m_partsVersion;
    }

    Physics::PhysicsWorld* physicsWorld;
    std::vector<RagdollPart> parts;
    Faction faction;
    std::unique_ptr<Core::HealthComponent> m_health;
    // Non-owning override; when set, getHealthComponent() returns this instead
    // of the owned m_health (single-source sharing, e.g. player + HUD/respawn).
    Core::HealthComponent* m_externalHealth = nullptr;

    // Lazily-rebuilt cache of `parts` grouped by boneGroupId (see getPartGroups).
    mutable std::vector<PartGroup> m_partGroups;
    mutable bool   m_partGroupsDirty = true;
    mutable size_t m_partGroupsBuiltSize = 0;
    mutable std::unordered_map<int, LodLevel> m_lodCache;   ///< level -> decimated parts
    mutable uint32_t m_partsVersion = 1;   ///< see partsVersion() (bumped from const rebuild)
    glm::vec3 m_renderTint{1.0f};          ///< see setRenderTint
    float     m_renderAlpha = 1.0f;        ///< see setRenderAlpha
};

} // namespace Scene
} // namespace Phyxel
