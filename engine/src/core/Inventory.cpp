#include "core/Inventory.h"

namespace Phyxel {
namespace Core {

Inventory::Inventory(int size) : m_slots(size), m_selectedSlot(0), m_creative(true) {}

std::optional<ItemStack> Inventory::getSlot(int index) const {
    if (index < 0 || index >= static_cast<int>(m_slots.size())) return std::nullopt;
    return m_slots[index];
}

bool Inventory::setSlot(int index, const std::optional<ItemStack>& item) {
    if (index < 0 || index >= static_cast<int>(m_slots.size())) return false;
    m_slots[index] = item;
    return true;
}

void Inventory::clearSlot(int index) {
    if (index >= 0 && index < static_cast<int>(m_slots.size())) {
        m_slots[index] = std::nullopt;
    }
}

int Inventory::addItem(const std::string& material, int count) {
    if (count <= 0) return 0;
    int remaining = count;

    // First pass: merge into existing stacks of the same material
    for (auto& slot : m_slots) {
        if (remaining <= 0) break;
        if (slot && slot->material == material && slot->count < slot->maxStack) {
            int toAdd = std::min(remaining, slot->spaceLeft());
            slot->count += toAdd;
            remaining -= toAdd;
        }
    }

    // Second pass: fill empty slots
    for (auto& slot : m_slots) {
        if (remaining <= 0) break;
        if (!slot) {
            int toAdd = std::min(remaining, 64);
            slot = ItemStack{material, toAdd, 64};
            remaining -= toAdd;
        }
    }

    return remaining; // Items that couldn't fit
}

int Inventory::removeItem(const std::string& material, int count) {
    if (count <= 0) return 0;
    int toRemove = count;

    // Remove from last slots first (preserve hotbar)
    for (int i = static_cast<int>(m_slots.size()) - 1; i >= 0; --i) {
        if (toRemove <= 0) break;
        if (m_slots[i] && m_slots[i]->material == material) {
            int take = std::min(toRemove, m_slots[i]->count);
            m_slots[i]->count -= take;
            toRemove -= take;
            if (m_slots[i]->count <= 0) {
                m_slots[i] = std::nullopt;
            }
        }
    }

    return count - toRemove; // Number actually removed
}

int Inventory::countItem(const std::string& material) const {
    int total = 0;
    for (const auto& slot : m_slots) {
        if (slot && slot->material == material) {
            total += slot->count;
        }
    }
    return total;
}

bool Inventory::hasItem(const std::string& material, int count) const {
    return countItem(material) >= count;
}

void Inventory::clear() {
    for (auto& slot : m_slots) {
        slot = std::nullopt;
    }
}

bool Inventory::setSelectedSlot(int slot) {
    if (slot < 0 || slot >= HOTBAR_SIZE) return false;
    m_selectedSlot = slot;
    return true;
}

std::string Inventory::getSelectedMaterial() const {
    if (m_selectedSlot < 0 || m_selectedSlot >= static_cast<int>(m_slots.size())) return "";
    const auto& slot = m_slots[m_selectedSlot];
    return slot ? slot->material : "";
}

bool Inventory::consumeSelected() {
    if (m_creative) return true; // Never consume in creative mode
    if (m_selectedSlot < 0 || m_selectedSlot >= static_cast<int>(m_slots.size())) return false;
    auto& slot = m_slots[m_selectedSlot];
    if (!slot || slot->count <= 0) return false;
    slot->count--;
    if (slot->count <= 0) {
        slot = std::nullopt;
    }
    return true;
}

nlohmann::json Inventory::toJson() const {
    nlohmann::json j;
    j["size"] = m_slots.size();
    j["selected_slot"] = m_selectedSlot;
    j["creative"] = m_creative;

    nlohmann::json slotsArr = nlohmann::json::array();
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
        if (m_slots[i]) {
            nlohmann::json slotJ = m_slots[i]->toJson();
            slotJ["slot"] = i;
            slotsArr.push_back(slotJ);
        }
    }
    j["slots"] = slotsArr;

    // Hotbar summary
    nlohmann::json hotbar = nlohmann::json::array();
    for (int i = 0; i < HOTBAR_SIZE && i < static_cast<int>(m_slots.size()); ++i) {
        if (m_slots[i]) {
            nlohmann::json h = m_slots[i]->toJson();
            h["slot"] = i;
            h["selected"] = (i == m_selectedSlot);
            hotbar.push_back(h);
        } else {
            hotbar.push_back({{"slot", i}, {"empty", true}, {"selected", (i == m_selectedSlot)}});
        }
    }
    j["hotbar"] = hotbar;
    return j;
}

void Inventory::fromJson(const nlohmann::json& j) {
    if (j.contains("size")) {
        m_slots.resize(j["size"].get<int>());
    }
    m_selectedSlot = j.value("selected_slot", 0);
    m_creative = j.value("creative", true);

    // Clear and load
    for (auto& slot : m_slots) slot = std::nullopt;
    if (j.contains("slots")) {
        for (const auto& slotJ : j["slots"]) {
            int idx = slotJ.value("slot", -1);
            if (idx >= 0 && idx < static_cast<int>(m_slots.size())) {
                ItemStack stack;
                stack.material = slotJ.value("material", "Default");
                stack.count = slotJ.value("count", 1);
                stack.maxStack = slotJ.value("max_stack", 64);
                m_slots[idx] = stack;
            }
        }
    }
}

} // namespace Core
} // namespace Phyxel
