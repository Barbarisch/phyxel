#pragma once

#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// ItemStack — a stack of items in a single inventory slot
// ============================================================================
struct ItemStack {
    std::string material;   // Material name (e.g. "Stone", "Wood", "Metal")
    int count = 1;          // Quantity in the stack
    int maxStack = 64;      // Maximum stack size

    bool canMerge(const ItemStack& other) const {
        return material == other.material && count < maxStack;
    }

    int spaceLeft() const { return maxStack - count; }

    nlohmann::json toJson() const {
        return {{"material", material}, {"count", count}, {"max_stack", maxStack}};
    }
};

// ============================================================================
// Inventory — slot-based container with hotbar selection
// ============================================================================
class Inventory {
public:
    static constexpr int DEFAULT_SIZE = 36;     // 4 rows of 9
    static constexpr int HOTBAR_SIZE = 9;       // First 9 slots = hotbar

    explicit Inventory(int size = DEFAULT_SIZE);

    // --- Slot access ---

    /// Get the item in a slot (nullopt if empty).
    std::optional<ItemStack> getSlot(int index) const;

    /// Set a slot directly (use nullopt to clear).
    bool setSlot(int index, const std::optional<ItemStack>& item);

    /// Clear a slot.
    void clearSlot(int index);

    /// Get total slot count.
    int size() const { return static_cast<int>(m_slots.size()); }

    // --- Item operations ---

    /// Add items to inventory. Returns the number of items that couldn't fit.
    int addItem(const std::string& material, int count = 1);

    /// Remove items from inventory. Returns the number actually removed.
    int removeItem(const std::string& material, int count = 1);

    /// Count total items of a material across all slots.
    int countItem(const std::string& material) const;

    /// Check if inventory has at least `count` of a material.
    bool hasItem(const std::string& material, int count = 1) const;

    /// Clear all slots.
    void clear();

    // --- Hotbar ---

    /// Get the currently selected hotbar slot index (0-8).
    int getSelectedSlot() const { return m_selectedSlot; }

    /// Set the selected hotbar slot (0 to HOTBAR_SIZE-1).
    bool setSelectedSlot(int slot);

    /// Get the material in the currently selected hotbar slot (empty if slot is empty).
    std::string getSelectedMaterial() const;

    /// Consume one item from the selected hotbar slot. Returns true if an item was consumed.
    bool consumeSelected();

    // --- Creative mode ---

    /// In creative mode, items are never consumed and all materials are available.
    bool isCreativeMode() const { return m_creative; }
    void setCreativeMode(bool creative) { m_creative = creative; }

    // --- Serialization ---
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::vector<std::optional<ItemStack>> m_slots;
    int m_selectedSlot = 0;
    bool m_creative = true;  // Default creative mode (infinite items)
};

} // namespace Core
} // namespace Phyxel
