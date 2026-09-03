#include "BestiaryHall.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/CharacterVisualResolver.h"
#include "core/MonsterVisualRegistry.h"
#include "core/NPCManager.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/NPCEntity.h"
#include "utils/Logger.h"

namespace Phyxel::Editor {

namespace {
constexpr const char* kTag = "BestiaryHall";

/// Prefix for every staged NPC. Namespaced so a hall teardown can never remove
/// an NPC the user spawned themselves.
constexpr const char* kNamePrefix = "hall_";
}  // namespace

bool BestiaryHall::Entry::canPlay(const std::string& state) const {
    if (state == "Idle")   return !clipIdle.empty();
    if (state == "Walk")   return !clipWalk.empty();
    if (state == "Attack") return !clipAttack.empty();
    if (state == "Death")  return !clipDeath.empty();
    return false;
}

// ---------------------------------------------------------------------------

bool BestiaryHall::loadRoster(const std::string& path) {
    m_entries.clear();
    m_selectedId.clear();
    m_staged = false;

    std::ifstream f(path);
    if (!f) {
        m_lastError = "cannot open roster: " + path +
                      " (run: python tools/creature_forge/gen_hall.py)";
        LOG_ERROR(kTag, "{}", m_lastError);
        return false;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        m_lastError = std::string("roster is not valid JSON: ") + e.what();
        LOG_ERROR(kTag, "{}", m_lastError);
        return false;
    }

    if (!j.contains("entries") || !j["entries"].is_array()) {
        m_lastError = "roster has no 'entries' array";
        LOG_ERROR(kTag, "{}", m_lastError);
        return false;
    }

    for (const auto& e : j["entries"]) {
        Entry entry;
        entry.id             = e.value("id", "");
        entry.name           = e.value("name", entry.id);
        entry.category       = e.value("category", "Other");
        entry.animFile       = e.value("animFile", "");
        entry.representative = e.value("representative", "");
        entry.statBlocks     = e.value("statBlocks", 0);
        entry.boxes          = e.value("boxes", 0);
        entry.bones          = e.value("bones", 0);
        if (entry.id.empty() || entry.animFile.empty()) continue;

        if (e.contains("bind")) {
            const auto& b = e["bind"];
            entry.width  = b.value("width", 1.0f);
            entry.height = b.value("height", 1.8f);
            entry.depth  = b.value("depth", 1.0f);
            entry.footY  = b.value("footY", 0.0f);
        }
        if (e.contains("clips")) {
            const auto& c = e["clips"];
            entry.clipIdle   = c.value("Idle", "");
            entry.clipWalk   = c.value("Walk", "");
            entry.clipAttack = c.value("Attack", "");
            entry.clipDeath  = c.value("Death", "");
        }
        m_entries.push_back(std::move(entry));
    }

    m_lastError.clear();
    LOG_INFO(kTag, "roster loaded: {} rigs", m_entries.size());
    return !m_entries.empty();
}

// ---------------------------------------------------------------------------

bool BestiaryHall::spawn(Core::NPCManager* npcs, const glm::vec3& origin,
                         float fallbackGroundY, const GroundSampler& sampleGround) {
    if (!npcs) { m_lastError = "no NPCManager"; return false; }
    if (m_entries.empty()) { m_lastError = "roster is empty"; return false; }

    despawn(npcs);
    m_origin  = origin;
    m_groundY = fallbackGroundY;
    Core::MonsterVisualRegistry::instance().ensureLoaded();

    // Rows are laid out per category so related creatures read together.
    // Within a row, X advances by each neighbour's measured half-width plus a
    // fixed gap — uniform spacing would either crowd the ancient dragon or
    // strand the rodents in acres of grass.
    std::vector<std::string> order;
    for (const auto& e : m_entries)
        if (std::find(order.begin(), order.end(), e.category) == order.end())
            order.push_back(e.category);

    float z = origin.z;
    int   staged = 0;

    for (const auto& cat : order) {
        std::vector<Entry*> row;
        for (auto& e : m_entries)
            if (e.category == cat) row.push_back(&e);

        // Wrap a category into as many sub-rows as its measured widths need.
        std::vector<std::vector<Entry*>> subRows{{}};
        float rowW = 0.0f;
        for (auto* e : row) {
            const float step = e->width + kGapX;
            if (rowW + step > kMaxRowWidth && !subRows.back().empty()) {
                subRows.push_back({});
                rowW = 0.0f;
            }
            subRows.back().push_back(e);
            rowW += step;
        }

        for (auto& sub : subRows) {
            float total = 0.0f;
            for (auto* e : sub) total += e->width + kGapX;

            float deepest = 0.0f;
            for (auto* e : sub) deepest = std::max(deepest, e->depth);

            float x = origin.x - total * 0.5f;
            for (auto* e : sub) {
                x += e->width * 0.5f;
                // Ground is sampled AT THIS CREATURE, not once for the hall:
                // on real terrain a shared floor buries the downhill half of a
                // row and floats the uphill half.
                const float gy = sampleGround ? sampleGround(x, z) : fallbackGroundY;
                // footY is the rig origin's offset from its lowest voxel, so
                // subtracting it plants every creature ON the floor regardless
                // of where its author put the origin.
                e->position = glm::vec3(x, gy - e->footY, z);
                x += e->width * 0.5f + kGapX;

                const Core::MonsterVisual* vis =
                    e->representative.empty()
                        ? nullptr
                        : Core::MonsterVisualRegistry::instance().get(e->representative);

                nlohmann::json vparams = {{"animFile", e->animFile}};
                if (vis && !vis->appearance.is_null()) {
                    // Keep the palette, DROP every *Scale key. A binding's
                    // scale belongs to one family member, and the first member
                    // is often the runt: the "Great Cat" archetype was being
                    // staged as a housecat (heightScale 0.3), the scorpion at
                    // 0.2, the sprawling reptile at 0.28. The hall is a
                    // catalogue of RIGS, so each is shown at the size it was
                    // authored at. (Appearance scale shrinks bone lengths but
                    // NOT the grounding capsule, so a scaled-down rig also
                    // hovers — the shrunken cat floated 0.15 clear of the
                    // ground.)
                    nlohmann::json app = vis->appearance;
                    if (app.is_object()) {
                        for (auto it = app.begin(); it != app.end();) {
                            const std::string& k = it.key();
                            const bool isScale =
                                k.size() >= 5 &&
                                k.compare(k.size() - 5, 5, "Scale") == 0;
                            it = isScale ? app.erase(it) : std::next(it);
                        }
                    }
                    vparams["appearance"] = app;
                }

                e->npcName = kNamePrefix + e->id;
                auto visual = Core::CharacterVisualResolver::resolve(vparams, e->npcName);

                // Idle behavior: the hall is a stage, not an encounter. Combat
                // NPCs would immediately walk off their marks and maul each
                // other, which is exactly what a catalogue must not do.
                Scene::NPCEntity* npc = npcs->spawnNPC(
                    e->npcName, visual.animFile, e->position,
                    Core::NPCBehaviorType::Idle, {}, 2.0f, 2.0f, visual.appearance);
                if (!npc) {
                    LOG_WARN(kTag, "failed to stage {}", e->id);
                    e->spawned = false;
                    continue;
                }

                if (auto* ch = npc->getAnimatedCharacter()) {
                    for (const auto& [state, clip] : visual.animationMapping)
                        ch->setAnimationMapping(state, clip);
                    if (vis) {
                        for (const auto& [state, clip] : vis->animationMapping)
                            ch->setAnimationMapping(state, clip);
                        // Show the palette the bestiary actually ships: the
                        // binding tint is what turns one neutral archetype rig
                        // into its family member.
                        ch->setRenderTint(glm::vec3(vis->tint[0], vis->tint[1], vis->tint[2]));
                    }
                }
                if (vis) e->representative = vis->monsterId;
                npc->setMonsterId(e->representative);
                e->spawned = true;
                ++staged;
            }
            z -= deepest + kGapZ;
        }
    }

    m_staged = staged > 0;
    applyGhosting(npcs);
    LOG_INFO(kTag, "staged {}/{} rigs", staged, m_entries.size());
    if (!m_staged) m_lastError = "no rigs could be staged";
    return m_staged;
}

void BestiaryHall::despawn(Core::NPCManager* npcs) {
    if (npcs) {
        for (auto& e : m_entries)
            if (e.spawned && !e.npcName.empty()) npcs->removeNPC(e.npcName);
    }
    for (auto& e : m_entries) { e.spawned = false; e.npcName.clear(); }
    m_staged = false;
    m_selectedId.clear();
}

// ---------------------------------------------------------------------------

void BestiaryHall::select(Core::NPCManager* npcs, const std::string& rigId) {
    if (!rigId.empty() && !find(rigId)) return;   // ignore unknown ids
    m_selectedId = rigId;
    applyGhosting(npcs);
}

void BestiaryHall::applyGhosting(Core::NPCManager* npcs) const {
    if (!npcs) return;
    const bool anySelected = !m_selectedId.empty();
    for (const auto& e : m_entries) {
        if (!e.spawned) continue;
        auto* npc = npcs->getNPC(e.npcName);
        if (!npc) continue;
        auto* ch = npc->getAnimatedCharacter();
        if (!ch) continue;
        // With nothing selected the whole hall is solid; with a selection,
        // everything else falls back so the chosen creature is the only thing
        // you can read. setRenderAlpha is a no-op when the value is unchanged,
        // so calling this every selection change costs nothing.
        ch->setRenderAlpha((!anySelected || e.id == m_selectedId) ? 1.0f : kGhostAlpha);
    }
}

int BestiaryHall::playState(Core::NPCManager* npcs, const std::string& state, bool allRigs) {
    if (!npcs) return 0;
    const auto target = Scene::AnimatedVoxelCharacter::stringToState(state);
    int played = 0;
    for (auto& e : m_entries) {
        if (!e.spawned) continue;
        if (!allRigs && e.id != m_selectedId) continue;
        // Skip rather than force: three imported quadruped rigs have no attack
        // clip at all, and driving them to a state they lack drops them into a
        // T-pose that reads as a broken rig.
        if (!e.canPlay(state)) continue;
        auto* npc = npcs->getNPC(e.npcName);
        if (!npc) continue;
        if (auto* ch = npc->getAnimatedCharacter()) {
            ch->setAnimationState(target);
            ++played;
        }
    }
    return played;
}

// ---------------------------------------------------------------------------

bool BestiaryHall::focusView(const std::string& rigId,
                             glm::vec3& outPos, glm::vec3& outTarget) const {
    const Entry* e = find(rigId);
    if (!e || !e->spawned) return false;
    // Frame by measured size so a rodent and an ancient dragon both fill a
    // comparable share of the screen.
    const float h = std::max(e->height, 0.4f);
    const float dist = std::max(3.0f, h * 2.4f + std::max(e->width, e->depth));
    outTarget = e->position + glm::vec3(0.0f, h * 0.55f, 0.0f);
    outPos    = outTarget + glm::vec3(0.0f, h * 0.35f, dist);
    return true;
}

BestiaryHall::Entry* BestiaryHall::find(const std::string& rigId) {
    for (auto& e : m_entries) if (e.id == rigId) return &e;
    return nullptr;
}

const BestiaryHall::Entry* BestiaryHall::find(const std::string& rigId) const {
    for (const auto& e : m_entries) if (e.id == rigId) return &e;
    return nullptr;
}

}  // namespace Phyxel::Editor
