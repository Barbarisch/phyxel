#pragma once

#include "core/ItemDefinition.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// ItemRegistry — central registry of all item definitions
// ============================================================================
class ItemRegistry {
public:
    /// Get the global singleton instance.
    static ItemRegistry& instance();

    /// Register an item definition. Returns false if the ID already exists.
    bool registerItem(const ItemDefinition& def);

    /// Look up an item by ID. Returns nullptr if not found.
    const ItemDefinition* getItem(const std::string& id) const;

    /// Check if an item ID is registered.
    bool hasItem(const std::string& id) const;

    /// Get all items of a specific type.
    std::vector<const ItemDefinition*> getItemsByType(ItemType type) const;

    /// Get all registered item IDs.
    std::vector<std::string> getAllItemIds() const;

    /// Get total count of registered items.
    size_t count() const { return m_items.size(); }

    /// Load item definitions from a JSON file. Returns number of items loaded.
    int loadFromFile(const std::string& filepath);

    /// Load item definitions from a JSON array.
    int loadFromJson(const nlohmann::json& itemsArray);

    /// Clear all registered items.
    void clear();

    /// Ensure all engine materials are registered as Material-type items.
    /// Call this after MaterialManager is initialized.
    void registerMaterialItems();

private:
    ItemRegistry() = default;
    ItemRegistry(const ItemRegistry&) = delete;
    ItemRegistry& operator=(const ItemRegistry&) = delete;

    std::unordered_map<std::string, ItemDefinition> m_items;
};

} // namespace Core
} // namespace Phyxel
