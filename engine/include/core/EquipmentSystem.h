#pragma once

#include "core/ItemDefinition.h"
#include <string>
#include <unordered_map>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// EquipmentSlots — what an entity currently has equipped
// ============================================================================
class EquipmentSlots {
public:
    /// Equip an item into its designated slot. Returns false if slot mismatch.
    bool equip(const ItemDefinition& item);

    /// Equip carrying a per-instance uuid (for a UNIQUE item), so unequip returns the SAME instance.
    bool equip(const ItemDefinition& item, const std::string& instanceUuid);

    /// Item-instance uuid equipped in a slot (empty if none / not a unique item).
    std::string getInstanceUuid(EquipSlot slot) const;

    /// Unequip whatever is in the given slot. Returns the item ID if something was removed.
    /// (Read getInstanceUuid(slot) BEFORE unequipping to recover the instance identity.)
    std::optional<std::string> unequip(EquipSlot slot);

    /// Get the item in a slot (nullptr if empty).
    const ItemDefinition* getItem(EquipSlot slot) const;

    /// Check if a slot has an item.
    bool hasItem(EquipSlot slot) const;

    /// Clear all slots.
    void clear();

    /// Get all equipped items as slot→itemId pairs.
    std::unordered_map<EquipSlot, std::string> getAllEquipped() const;

    /// Get total armor/stat bonus from all equipped items.
    float getTotalDamage() const;
    float getTotalSpeed() const;
    float getTotalReach() const;

    /// Serialization.
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

    /// Callback fired when equipment changes (slot, newItemId or "" if unequipped).
    using OnEquipmentChanged = std::function<void(EquipSlot, const std::string&)>;
    void setOnEquipmentChanged(OnEquipmentChanged cb) { m_onChanged = std::move(cb); }

private:
    std::unordered_map<EquipSlot, ItemDefinition> m_slots;
    std::unordered_map<EquipSlot, std::string> m_instanceUuids;  ///< per-slot item-instance uuid
    OnEquipmentChanged m_onChanged;
};

} // namespace Core
} // namespace Phyxel
