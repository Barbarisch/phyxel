#include "core/EquipmentSystem.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

bool EquipmentSlots::equip(const ItemDefinition& item) {
    if (item.equipSlot == EquipSlot::None) {
        LOG_WARN("Equipment", "Item '{}' has no equip slot", item.id);
        return false;
    }

    // Validate item type allows equipping
    if (item.type != ItemType::Weapon && item.type != ItemType::Tool &&
        item.type != ItemType::Equippable) {
        LOG_WARN("Equipment", "Item '{}' (type {}) cannot be equipped",
                 item.id, itemTypeToString(item.type));
        return false;
    }

    m_slots[item.equipSlot] = item;
    if (m_onChanged) m_onChanged(item.equipSlot, item.id);
    return true;
}

std::optional<std::string> EquipmentSlots::unequip(EquipSlot slot) {
    auto it = m_slots.find(slot);
    if (it == m_slots.end()) return std::nullopt;

    std::string itemId = it->second.id;
    m_slots.erase(it);
    if (m_onChanged) m_onChanged(slot, "");
    return itemId;
}

const ItemDefinition* EquipmentSlots::getItem(EquipSlot slot) const {
    auto it = m_slots.find(slot);
    return (it != m_slots.end()) ? &it->second : nullptr;
}

bool EquipmentSlots::hasItem(EquipSlot slot) const {
    return m_slots.find(slot) != m_slots.end();
}

void EquipmentSlots::clear() {
    m_slots.clear();
}

std::unordered_map<EquipSlot, std::string> EquipmentSlots::getAllEquipped() const {
    std::unordered_map<EquipSlot, std::string> result;
    for (const auto& [slot, item] : m_slots) {
        result[slot] = item.id;
    }
    return result;
}

float EquipmentSlots::getTotalDamage() const {
    float total = 0.0f;
    for (const auto& [slot, item] : m_slots) {
        total += item.damage;
    }
    return total;
}

float EquipmentSlots::getTotalSpeed() const {
    float total = 1.0f;
    for (const auto& [slot, item] : m_slots) {
        if (item.speed != 1.0f) total *= item.speed;
    }
    return total;
}

float EquipmentSlots::getTotalReach() const {
    // Use the longest reach from equipped weapons/tools
    float maxReach = 1.5f; // default fist reach
    for (const auto& [slot, item] : m_slots) {
        if (item.reach > maxReach) maxReach = item.reach;
    }
    return maxReach;
}

nlohmann::json EquipmentSlots::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [slot, item] : m_slots) {
        j[equipSlotToString(slot)] = item.id;
    }
    return j;
}

void EquipmentSlots::fromJson(const nlohmann::json& j) {
    // Note: caller must resolve item IDs via ItemRegistry after calling this.
    // This method is a no-op for now — actual deserialization requires ItemRegistry.
    // Equipment should be restored via equip() calls during game load.
}

} // namespace Core
} // namespace Phyxel
