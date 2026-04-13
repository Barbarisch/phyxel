#include "core/LootTable.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numeric>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// LootEntry JSON
// ---------------------------------------------------------------------------

nlohmann::json LootEntry::toJson() const {
    return {
        {"itemId",   itemId},
        {"weight",   weight},
        {"minCount", minCount},
        {"maxCount", maxCount},
        {"chance",   chance}
    };
}

LootEntry LootEntry::fromJson(const nlohmann::json& j) {
    LootEntry e;
    e.itemId   = j.value("itemId",   "");
    e.weight   = j.value("weight",   1.0f);
    e.minCount = j.value("minCount", 1);
    e.maxCount = j.value("maxCount", 1);
    e.chance   = j.value("chance",   1.0f);
    return e;
}

// ---------------------------------------------------------------------------
// LootTable::selectWeighted
// ---------------------------------------------------------------------------

const LootEntry* LootTable::selectWeighted(DiceSystem& dice) const {
    if (entries.empty()) return nullptr;

    float totalWeight = 0.0f;
    for (const auto& e : entries) totalWeight += e.weight;
    if (totalWeight <= 0.0f) return nullptr;

    // Roll a value in [0, totalWeight)
    float r = dice.rollFloat() * totalWeight;
    float cumulative = 0.0f;
    for (const auto& e : entries) {
        cumulative += e.weight;
        if (r < cumulative) return &e;
    }
    return &entries.back();
}

// ---------------------------------------------------------------------------
// LootTable::roll
// ---------------------------------------------------------------------------

std::vector<LootResult> LootTable::roll(DiceSystem& dice) const {
    std::vector<LootResult> results;
    if (entries.empty()) return results;

    int numRolls = minRolls;
    if (maxRolls > minRolls) {
        int range = maxRolls - minRolls;
        numRolls = minRolls + static_cast<int>(dice.rollFloat() * (range + 1));
        numRolls = std::min(numRolls, maxRolls);
    }

    for (int i = 0; i < numRolls; ++i) {
        const auto* entry = selectWeighted(dice);
        if (!entry) continue;

        // Independent chance roll
        if (entry->chance < 1.0f && dice.rollFloat() >= entry->chance) continue;

        int count = entry->minCount;
        if (entry->maxCount > entry->minCount) {
            int range = entry->maxCount - entry->minCount;
            count = entry->minCount + static_cast<int>(dice.rollFloat() * (range + 1));
            count = std::min(count, entry->maxCount);
        }

        // Merge stacks for the same item
        auto it = std::find_if(results.begin(), results.end(),
            [&](const LootResult& r) { return r.itemId == entry->itemId; });
        if (it != results.end())
            it->count += count;
        else
            results.push_back({ entry->itemId, count });
    }

    return results;
}

// ---------------------------------------------------------------------------
// LootTable JSON
// ---------------------------------------------------------------------------

nlohmann::json LootTable::toJson() const {
    nlohmann::json j;
    j["id"]       = id;
    j["name"]     = name;
    j["minRolls"] = minRolls;
    j["maxRolls"] = maxRolls;
    auto arr = nlohmann::json::array();
    for (const auto& e : entries) arr.push_back(e.toJson());
    j["entries"] = arr;
    return j;
}

LootTable LootTable::fromJson(const nlohmann::json& j) {
    LootTable t;
    t.id       = j.value("id",       "");
    t.name     = j.value("name",     "");
    t.minRolls = j.value("minRolls", 1);
    t.maxRolls = j.value("maxRolls", 1);
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& ej : j["entries"])
            t.entries.push_back(LootEntry::fromJson(ej));
    }
    return t;
}

// ---------------------------------------------------------------------------
// LootTableRegistry
// ---------------------------------------------------------------------------

LootTableRegistry& LootTableRegistry::instance() {
    static LootTableRegistry s_instance;
    return s_instance;
}

void LootTableRegistry::registerTable(LootTable table) {
    m_tables[table.id] = std::move(table);
}

const LootTable* LootTableRegistry::getTable(const std::string& id) const {
    auto it = m_tables.find(id);
    return (it != m_tables.end()) ? &it->second : nullptr;
}

std::vector<LootResult> LootTableRegistry::rollTable(const std::string& id,
                                                       DiceSystem& dice) const {
    const auto* table = getTable(id);
    if (!table) return {};
    return table->roll(dice);
}

void LootTableRegistry::clear() {
    m_tables.clear();
}

bool LootTableRegistry::loadFromFile(const std::string& filePath) {
    std::ifstream f(filePath);
    if (!f.is_open()) return false;
    try {
        auto j = nlohmann::json::parse(f);
        if (j.is_array()) {
            for (const auto& item : j)
                registerTable(LootTable::fromJson(item));
        } else if (j.is_object()) {
            registerTable(LootTable::fromJson(j));
        }
        return true;
    } catch (...) {
        return false;
    }
}

int LootTableRegistry::loadFromDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (entry.path().extension() == ".json") {
            if (loadFromFile(entry.path().string()))
                ++count;
        }
    }
    return count;
}

} // namespace Phyxel::Core
