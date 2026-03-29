#include "core/ItemRegistry.h"
#include "physics/Material.h"
#include "utils/Logger.h"

#include <fstream>

namespace Phyxel {
namespace Core {

ItemRegistry& ItemRegistry::instance() {
    static ItemRegistry s_instance;
    return s_instance;
}

bool ItemRegistry::registerItem(const ItemDefinition& def) {
    if (def.id.empty()) {
        LOG_WARN("ItemRegistry", "Cannot register item with empty ID");
        return false;
    }
    auto [it, inserted] = m_items.emplace(def.id, def);
    if (!inserted) {
        LOG_WARN("ItemRegistry", "Item '{}' already registered — skipping", def.id);
    }
    return inserted;
}

const ItemDefinition* ItemRegistry::getItem(const std::string& id) const {
    auto it = m_items.find(id);
    return (it != m_items.end()) ? &it->second : nullptr;
}

bool ItemRegistry::hasItem(const std::string& id) const {
    return m_items.find(id) != m_items.end();
}

std::vector<const ItemDefinition*> ItemRegistry::getItemsByType(ItemType type) const {
    std::vector<const ItemDefinition*> result;
    for (const auto& [id, def] : m_items) {
        if (def.type == type) {
            result.push_back(&def);
        }
    }
    return result;
}

std::vector<std::string> ItemRegistry::getAllItemIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_items.size());
    for (const auto& [id, def] : m_items) {
        ids.push_back(id);
    }
    return ids;
}

int ItemRegistry::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_WARN("ItemRegistry", "Could not open items file: {}", filepath);
        return 0;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(file);
        if (j.contains("items") && j["items"].is_array()) {
            return loadFromJson(j["items"]);
        } else if (j.is_array()) {
            return loadFromJson(j);
        }
        LOG_WARN("ItemRegistry", "Items file has no 'items' array: {}", filepath);
        return 0;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("ItemRegistry", "Failed to parse items file {}: {}", filepath, e.what());
        return 0;
    }
}

int ItemRegistry::loadFromJson(const nlohmann::json& itemsArray) {
    int loaded = 0;
    for (const auto& itemJson : itemsArray) {
        try {
            ItemDefinition def = ItemDefinition::fromJson(itemJson);
            if (registerItem(def)) {
                ++loaded;
            }
        } catch (const nlohmann::json::exception& e) {
            LOG_WARN("ItemRegistry", "Failed to parse item definition: {}", e.what());
        }
    }
    LOG_INFO("ItemRegistry", "Loaded {} item definitions", loaded);
    return loaded;
}

void ItemRegistry::clear() {
    m_items.clear();
}

void ItemRegistry::registerMaterialItems() {
    Physics::MaterialManager matMgr;
    auto names = matMgr.getAllMaterialNames();
    int registered = 0;
    for (const auto& name : names) {
        // Skip if already registered (user-defined items take priority)
        if (hasItem(name)) continue;

        ItemDefinition def;
        def.id = name;
        def.name = name;
        def.type = ItemType::Material;
        def.stackable = true;
        def.maxStack = 64;
        def.description = name + " material";

        if (registerItem(def)) {
            ++registered;
        }
    }
    if (registered > 0) {
        LOG_INFO("ItemRegistry", "Auto-registered {} materials as items", registered);
    }
}

} // namespace Core
} // namespace Phyxel
