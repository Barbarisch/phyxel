#pragma once

#include "core/DiceSystem.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// LootEntry — one item in a loot table
// ---------------------------------------------------------------------------

struct LootEntry {
    std::string itemId;
    float       weight   = 1.0f;  ///< Relative weight for weighted random selection
    int         minCount = 1;     ///< Min quantity if this entry is selected
    int         maxCount = 1;     ///< Max quantity if this entry is selected
    float       chance   = 1.0f;  ///< Independent inclusion probability [0–1]

    nlohmann::json toJson() const;
    static LootEntry fromJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// LootResult — one resolved item drop
// ---------------------------------------------------------------------------

struct LootResult {
    std::string itemId;
    int         count = 1;
};

// ---------------------------------------------------------------------------
// LootTable
// ---------------------------------------------------------------------------

/// A weighted random loot table.
/// Each roll selects one entry by weight (if its chance roll passes),
/// and repeats for minRolls–maxRolls total draws.
struct LootTable {
    std::string             id;
    std::string             name;
    std::vector<LootEntry>  entries;
    int                     minRolls = 1;  ///< Minimum number of item draws per roll
    int                     maxRolls = 1;  ///< Maximum number of item draws per roll

    /// Roll the table, returning a (possibly empty) list of item drops.
    std::vector<LootResult> roll(DiceSystem& dice) const;

    nlohmann::json toJson() const;
    static LootTable fromJson(const nlohmann::json& j);

private:
    /// Select one entry by weight. Returns nullptr if table is empty.
    const LootEntry* selectWeighted(DiceSystem& dice) const;
};

// ---------------------------------------------------------------------------
// LootTableRegistry — singleton
// ---------------------------------------------------------------------------

class LootTableRegistry {
public:
    static LootTableRegistry& instance();

    void registerTable(LootTable table);
    const LootTable* getTable(const std::string& id) const;

    /// Roll a registered table by ID. Returns empty vector if not found.
    std::vector<LootResult> rollTable(const std::string& id, DiceSystem& dice) const;

    size_t count() const { return m_tables.size(); }
    void   clear();

    bool loadFromFile(const std::string& filePath);
    int  loadFromDirectory(const std::string& dirPath);

private:
    LootTableRegistry() = default;
    std::unordered_map<std::string, LootTable> m_tables;
};

} // namespace Phyxel::Core
