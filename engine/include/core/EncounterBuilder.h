#pragma once

#include "core/Party.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// EncounterDifficulty
// ---------------------------------------------------------------------------

enum class EncounterDifficulty { Easy, Medium, Hard, Deadly };

const char*         encounterDifficultyName(EncounterDifficulty d);
EncounterDifficulty encounterDifficultyFromString(const char* name);

// ---------------------------------------------------------------------------
// MonsterEntry — one monster type in an encounter
// ---------------------------------------------------------------------------

struct MonsterEntry {
    std::string monsterId;
    std::string name;
    int         xpValue       = 0;    ///< XP awarded per monster
    int         count         = 1;    ///< Number of this monster in the encounter
    int         challengeRating = 0;  ///< CR × 100 (e.g. 1/4 CR = 25, 1 CR = 100, 5 CR = 500)

    int totalXp() const { return xpValue * count; }

    nlohmann::json toJson() const;
    static MonsterEntry fromJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// EncounterBudget — XP thresholds for a party
// ---------------------------------------------------------------------------

struct EncounterBudget {
    int easyXp   = 0;
    int mediumXp = 0;
    int hardXp   = 0;
    int deadlyXp = 0;
};

// ---------------------------------------------------------------------------
// Encounter — a complete encounter definition
// ---------------------------------------------------------------------------

struct Encounter {
    std::string              id;
    std::string              name;
    std::string              description;
    std::vector<MonsterEntry> monsters;
    std::string              lootTableId;   ///< Optional — rolled when encounter is completed

    int   totalMonsterCount() const;
    int   totalMonsterXp()    const;   ///< Raw XP before multiplier
    float getMultiplier()     const;   ///< D&D 5e multiplier by monster count
    int   adjustedXp()        const;   ///< totalMonsterXp × multiplier

    nlohmann::json toJson() const;
    static Encounter fromJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// EncounterBuilder — fluent builder + difficulty analysis
// ---------------------------------------------------------------------------

class EncounterBuilder {
public:
    // -----------------------------------------------------------------------
    // Fluent builder
    // -----------------------------------------------------------------------

    EncounterBuilder& setId(const std::string& id)          { m_encounter.id   = id;   return *this; }
    EncounterBuilder& setName(const std::string& name)      { m_encounter.name = name; return *this; }
    EncounterBuilder& setDescription(const std::string& d)  { m_encounter.description = d; return *this; }
    EncounterBuilder& setLootTable(const std::string& id)   { m_encounter.lootTableId = id; return *this; }

    /// Add a monster group (same type, one or more).
    EncounterBuilder& addMonster(const std::string& monsterId,
                                  const std::string& name,
                                  int xpValue,
                                  int count = 1,
                                  int challengeRating = 0);

    Encounter build() const { return m_encounter; }

    // -----------------------------------------------------------------------
    // Static analysis
    // -----------------------------------------------------------------------

    /// Calculate the XP budget thresholds for a party.
    static EncounterBudget calculateBudget(const Party& party);

    /// Evaluate the difficulty of an encounter for the given party.
    static EncounterDifficulty evaluateDifficulty(const Encounter& enc,
                                                   const Party& party);

    /// True if adjustedXp falls within the target difficulty tier.
    static bool isValidForDifficulty(const Encounter& enc,
                                      const Party& party,
                                      EncounterDifficulty target);

    // -----------------------------------------------------------------------
    // XP threshold tables (D&D 5e, levels 1–20)
    // -----------------------------------------------------------------------

    /// Per-character XP threshold for a given level and difficulty.
    static int xpThreshold(int characterLevel, EncounterDifficulty difficulty);

    /// Encounter multiplier based on total monster count (D&D 5e table).
    static float encounterMultiplier(int monsterCount);

private:
    Encounter m_encounter;
};

} // namespace Phyxel::Core
