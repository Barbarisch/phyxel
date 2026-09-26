#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Phyxel {

class VoxelTemplate;
namespace Scene { class NPCEntity; }

namespace Core {

class NPCManager;
class KinematicVoxelManager;
class ItemEffectSystem;
struct ItemDefinition;

// ============================================================================
// NpcHeldItems — what every NPC holds in its hand, drawn and alive.
//
// One engine class for the editor and the shipped game (both used to keep their own
// combat-weapon-only copy: Application::updateNpcHeldItems and the scaffold's
// equipNpcWeapons/updateHeldWeapons, with DIFFERENT grip-rotation math). Per frame it
// reconciles each NPC's DESIRED item (desiredItem: the combat weapon, else the item the
// NPC is carrying - a torch at night, NPCEntity::setCarriedItem) with what is drawn:
// builds the item's kinematic voxel mesh from its template, attaches it to the grip
// bone, follows the bone, and registers the item's EFFECTS with ItemEffectSystem so a
// torch's flame and light ride the hand (neither old copy registered effects - an NPC
// torch would have been a dark stick). Ravenmere T-73 / the torch feature, 2026-09-26.
// ============================================================================
class NpcHeldItems {
public:
    /// Resolve an item's .voxel template (the editor: ItemPropManager::resolveItemTemplate;
    /// the shell: its template manager). nullptr = not loadable, the item is not drawn.
    using TemplateResolver = std::function<const VoxelTemplate*(const ItemDefinition&)>;

    NpcHeldItems(KinematicVoxelManager* kinematic, TemplateResolver resolve,
                 ItemEffectSystem* effects = nullptr);
    ~NpcHeldItems();

    void setItemEffectSystem(ItemEffectSystem* effects) { m_effects = effects; }

    /// Per frame: rebuild changed items, follow the grip bones, drop NPCs that are gone.
    void update(NPCManager& npcs);

    /// The item an NPC should be holding: its combat weapon if it has one, otherwise its
    /// carried item. "" = empty hands. Unknown / unholdable / template-less items read "".
    static std::string desiredItem(const Scene::NPCEntity& npc);

    /// The item currently DRAWN in an NPC's hand ("" = none) - for tests and the test API.
    std::string heldItem(const std::string& npcName) const;
    /// The kinematic mesh id of that item ("" = none).
    std::string heldMeshId(const std::string& npcName) const;
    size_t count() const { return m_held.size(); }
    /// The ItemEffectSystem instance id an NPC's held item registers under.
    static std::string effectInstanceId(const std::string& npcName) { return "npcheld_" + npcName; }

    /// The world transform the grip math produces for an item held at a bone attachment
    /// (translate * rotate(attachment) * euler(x, y, z applied in that order) * scale).
    /// Public so the editor, the shell and the tests share one grip convention.
    static glm::mat4 gripTransform(const glm::vec3& pos, const glm::quat& rot,
                                   const ItemDefinition& def);

    /// Remove every drawn item and its effects (scene unload / shutdown).
    void clear();

private:
    struct Held {
        std::string itemId;
        std::string kinId;
        int anchorId = -1;
        bool effects = false;
        std::string failed;   ///< an item that could not be drawn; not retried until the want changes
        const void* body = nullptr;   ///< the character the anchor belongs to (a same-named NPC after a reload is a different body)
    };
    void drop(const std::string& npcName, Held& held, Scene::NPCEntity* npc);

    KinematicVoxelManager* m_kin = nullptr;
    TemplateResolver m_resolve;
    ItemEffectSystem* m_effects = nullptr;
    std::unordered_map<std::string, Held> m_held;   // npc name -> drawn item
};

} // namespace Core
} // namespace Phyxel
