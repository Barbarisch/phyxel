#include "core/NpcHeldItems.h"

#include "core/EquipmentSystem.h"
#include "core/ItemDefinition.h"
#include "core/ItemEffectSystem.h"
#include "core/MeleeAnimMapper.h"
#include "core/RpgItem.h"
#include "core/ItemPropManager.h"
#include "core/ItemRegistry.h"
#include "core/KinematicVoxelManager.h"
#include "core/NPCManager.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/NPCEntity.h"
#include "scene/behaviors/CombatBehavior.h"
#include "utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <unordered_set>

namespace Phyxel {
namespace Core {

NpcHeldItems::NpcHeldItems(KinematicVoxelManager* kinematic, TemplateResolver resolve,
                           ItemEffectSystem* effects)
    : m_kin(kinematic), m_resolve(std::move(resolve)), m_effects(effects) {}

NpcHeldItems::~NpcHeldItems() { clear(); }

std::string NpcHeldItems::desiredItem(const Scene::NPCEntity& npc) {
    std::string id;
    if (auto* cb = dynamic_cast<const Scene::CombatBehavior*>(npc.getBehavior())) id = cb->getWeaponId();
    // The hand shows whatever is EQUIPPED (equip_item -> EquipmentSlots MainHand) - the real
    // fine-voxel item template, same path as the player (upstream, 2026-09-29).
    if (id.empty())
        if (const ItemDefinition* eq = npc.getEquipment().getItem(EquipSlot::MainHand)) id = eq->id;
    if (id.empty()) id = npc.getCarriedItem();
    if (id.empty()) return {};
    const ItemDefinition* def = ItemRegistry::instance().getItem(id);
    if (!def || !def->holdable || def->templateFile.empty()) return {};
    return id;
}

glm::mat4 NpcHeldItems::gripTransform(const glm::vec3& pos, const glm::quat& rot, const ItemDefinition& def) {
    const auto& h = def.held;
    glm::mat4 t = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
    t = glm::rotate(t, glm::radians(h.gripEulerDeg.x), glm::vec3(1, 0, 0));
    t = glm::rotate(t, glm::radians(h.gripEulerDeg.y), glm::vec3(0, 1, 0));
    t = glm::rotate(t, glm::radians(h.gripEulerDeg.z), glm::vec3(0, 0, 1));
    const float s = h.scale > 0.0f ? h.scale : 1.0f;
    return glm::scale(t, glm::vec3(s));
}

std::string NpcHeldItems::heldItem(const std::string& npcName) const {
    auto it = m_held.find(npcName);
    return it == m_held.end() ? std::string() : it->second.itemId;
}

std::string NpcHeldItems::heldMeshId(const std::string& npcName) const {
    auto it = m_held.find(npcName);
    return it == m_held.end() ? std::string() : it->second.kinId;
}

namespace {
// A3: composition factors follow the hands (grip class + load band derived from the D&D weapon
// data; the off-hand shield makes 1h into 1h_shield). `def` null = empty hands.
void applyGripFactors(Scene::NPCEntity& npc, Scene::AnimatedVoxelCharacter& character,
                      const ItemDefinition* def) {
    auto& melee = MeleeAnimMapper::instance();
    if (!melee.isLoaded()) melee.loadConfig("resources/rpg_items/anim/melee_anim_families.json");
    auto& rpgReg = RpgItemRegistry::instance();
    if (rpgReg.count() == 0) rpgReg.loadFromDirectory("resources/rpg_items");
    const ItemDefinition* off = npc.getEquipment().getItem(EquipSlot::OffHand);
    const auto gf = melee.resolveGripFactors(def, off);
    auto factors = character.compositionFactors();
    factors.grip = gf.grip;
    factors.load = gf.load;
    character.setCompositionFactors(factors);
}
}  // namespace

void NpcHeldItems::drop(const std::string& npcName, Held& held, Scene::NPCEntity* npc) {
    if (held.effects && m_effects) m_effects->unregisterInstance(effectInstanceId(npcName));
    if (npc && npc->getAnimatedCharacter()) npc->getAnimatedCharacter()->clearOffHandTarget();
    held.effects = false;
    if (!held.kinId.empty() && m_kin) m_kin->remove(held.kinId);
    held.kinId.clear();
    if (held.anchorId >= 0 && npc && npc->getAnimatedCharacter())
        npc->getAnimatedCharacter()->detachFromBone(held.anchorId);
    held.anchorId = -1;
    held.itemId.clear();
}

void NpcHeldItems::update(NPCManager& npcs) {
    if (!m_kin) return;
    std::unordered_set<std::string> seen;
    npcs.forEachNPC([&](Scene::NPCEntity& npc) {
        const std::string& name = npc.getName();
        seen.insert(name);
        Scene::AnimatedVoxelCharacter* character = npc.getAnimatedCharacter();
        const std::string want = character ? desiredItem(npc) : std::string();
        Held& held = m_held[name];
        if (held.body && held.body != character) {
            // A different body under the same name (a scene reload): the old anchor id is not
            // this body's - drop without detaching from it.
            drop(name, held, nullptr);
            held.failed.clear();
        }
        held.body = character;

        if (want != held.itemId && (want.empty() || want != held.failed)) {
            drop(name, held, &npc);
            held.failed.clear();
            if (!want.empty()) {
                const ItemDefinition* def = ItemRegistry::instance().getItem(want);
                const VoxelTemplate* tmpl = (def && m_resolve) ? m_resolve(*def) : nullptr;
                std::vector<KinematicVoxel> voxels;
                if (tmpl) voxels = ItemPropManager::voxelsFromTemplate(*tmpl);
                if (!voxels.empty()) {
                    held.kinId = m_kin->add("npcheld_" + name, std::move(voxels));
                    m_kin->setPushesDebris(held.kinId, true);   // a swung weapon shoves debris (3b)
                    held.anchorId = character->attachToBone(def->held.gripBone, glm::vec3(0.02f),
                                                            def->held.gripOffset, glm::vec4(0.0f),
                                                            "npc_held_anchor");
                    if (held.anchorId < 0) {
                        // No grip bone on this rig: no item floating at the world origin.
                        m_kin->remove(held.kinId);
                        held.kinId.clear();
                        held.failed = want;
                    } else {
                        held.itemId = want;
                        if (m_effects && !def->effects.empty()) {
                            KinematicVoxelManager* kin = m_kin;
                            const std::string kinId = held.kinId;
                            m_effects->registerInstance(effectInstanceId(name), def, /*held=*/true,
                                                        [kin, kinId] { return kin->getTransform(kinId); });
                            held.effects = true;
                        }
                    }
                } else {
                    held.failed = want;
                    static int s_warned = 0;
                    if (s_warned++ < 4)
                        LOG_WARN("NpcHeldItems", "'{}' wants '{}' but its template is not loadable", name, want);
                }
            }
            // The hands changed: the pose composition follows (empty hands when nothing is drawn).
            if (character)
                applyGripFactors(npc, *character,
                                 held.itemId.empty() ? nullptr : ItemRegistry::instance().getItem(held.itemId));
        }

        // Follow the grip bone.
        if (!held.kinId.empty() && held.anchorId >= 0 && character) {
            const ItemDefinition* def = ItemRegistry::instance().getItem(held.itemId);
            glm::vec3 pos;
            glm::quat rot;
            if (def && character->getAttachmentTransform(held.anchorId, pos, rot)) {
                const glm::mat4 t = gripTransform(pos, rot, *def);
                m_kin->setTransform(held.kinId, t);
                // A3 off-hand pin: a two-handed item's second grip holds the other hand.
                const std::string& grip = character->compositionFactors().grip;
                if (def->held.hasSecondGrip && (grip == "2h_heavy" || grip == "2h_light" || grip == "staff"))
                    character->setOffHandTarget(glm::vec3(t * glm::vec4(def->held.secondGrip, 1.0f)));
                else
                    character->clearOffHandTarget();
            } else {
                drop(name, held, &npc);
            }
        }
    });

    // NPCs that no longer exist take their items (and lights) with them.
    for (auto it = m_held.begin(); it != m_held.end();) {
        if (seen.count(it->first)) { ++it; continue; }
        drop(it->first, it->second, nullptr);
        it = m_held.erase(it);
    }
}

void NpcHeldItems::clear() {
    for (auto& [name, held] : m_held) drop(name, held, nullptr);
    m_held.clear();
}

} // namespace Core
} // namespace Phyxel
