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
    std::string itemId;         // Item identifier (material name or item definition ID)
    int count = 1;              // Quantity in the stack
    int maxStack = 64;          // Maximum stack size
    int durability = -1;        // Current durability (-1 = not applicable / indestructible)
    std::string instanceUuid;   // Stable per-instance id for UNIQUE items (empty for fungible stacks)

    /// Backward-compat alias: treat itemId as material name.
    const std::string& material() const { return itemId; }

    /// A distinguishable individual (non-stackable or durability-bearing) that should carry a stable
    /// instance uuid — as opposed to a fungible commodity that merges by itemId. Drives when a uuid is
    /// minted (give/spawn/pickup) and, via canMerge, guarantees such a stack keeps its own slot.
    bool isUnique() const { return maxStack <= 1 || durability >= 0; }

    bool canMerge(const ItemStack& other) const {
        // A stack carrying an instance uuid NEVER merges — merging would silently collapse identity.
        return itemId == other.itemId && count < maxStack && durability < 0 && other.durability < 0
            && instanceUuid.empty() && other.instanceUuid.empty();
    }

    int spaceLeft() const { return maxStack - count; }

    nlohmann::json toJson() const {
        nlohmann::json j = {{"material", itemId}, {"count", count}, {"max_stack", maxStack}};
        if (durability >= 0) j["durability"] = durability;
        if (!instanceUuid.empty()) j["uuid"] = instanceUuid;
        return j;
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

    /// Add a specific stack (carrying durability / instance uuid). If the stack is unique
    /// (isUnique) and lacks an instance uuid, one is minted. Unique stacks take their own empty
    /// slot and never merge. Returns true if placed, false if the inventory is full.
    bool addItemStack(const ItemStack& stack);

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
