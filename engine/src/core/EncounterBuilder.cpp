#include "core/EncounterBuilder.h"
#include <algorithm>
#include <numeric>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

const char* encounterDifficultyName(EncounterDifficulty d) {
    switch (d) {
        case EncounterDifficulty::Easy:   return "Easy";
        case EncounterDifficulty::Medium: return "Medium";
        case EncounterDifficulty::Hard:   return "Hard";
        case EncounterDifficulty::Deadly: return "Deadly";
    }
    return "Medium";
}

EncounterDifficulty encounterDifficultyFromString(const char* name) {
    if (!name) return EncounterDifficulty::Medium;
    auto eq = [&](const char* s) { return _stricmp(name, s) == 0; };
    if (eq("Easy"))   return EncounterDifficulty::Easy;
    if (eq("Medium")) return EncounterDifficulty::Medium;
    if (eq("Hard"))   return EncounterDifficulty::Hard;
    if (eq("Deadly")) return EncounterDifficulty::Deadly;
    return EncounterDifficulty::Medium;
}

// ---------------------------------------------------------------------------
// MonsterEntry JSON
// ---------------------------------------------------------------------------

nlohmann::json MonsterEntry::toJson() const {
    return {
        {"monsterId",        monsterId},
        {"name",             name},
        {"xpValue",          xpValue},
        {"count",            count},
        {"challengeRating",  challengeRating}
    };
}

MonsterEntry MonsterEntry::fromJson(const nlohmann::json& j) {
    MonsterEntry e;
    e.monsterId       = j.value("monsterId",       "");
    e.name            = j.value("name",            "");
    e.xpValue         = j.value("xpValue",          0);
    e.count           = j.value("count",            1);
    e.challengeRating = j.value("challengeRating",  0);
    return e;
}

// ---------------------------------------------------------------------------
// Encounter
// ---------------------------------------------------------------------------

int Encounter::totalMonsterCount() const {
    int total = 0;
    for (const auto& m : monsters) total += m.count;
    return total;
}

int Encounter::totalMonsterXp() const {
    int total = 0;
    for (const auto& m : monsters) total += m.totalXp();
    return total;
}

float Encounter::getMultiplier() const {
    return EncounterBuilder::encounterMultiplier(totalMonsterCount());
}

int Encounter::adjustedXp() const {
    return static_cast<int>(totalMonsterXp() * getMultiplier());
}

nlohmann::json Encounter::toJson() const {
    nlohmann::json j;
    j["id"]          = id;
    j["name"]        = name;
    j["description"] = description;
    j["lootTableId"] = lootTableId;
    auto arr = nlohmann::json::array();
    for (const auto& m : monsters) arr.push_back(m.toJson());
    j["monsters"] = arr;
    return j;
}

Encounter Encounter::fromJson(const nlohmann::json& j) {
    Encounter e;
    e.id          = j.value("id",          "");
    e.name        = j.value("name",        "");
    e.description = j.value("description", "");
    e.lootTableId = j.value("lootTableId", "");
    if (j.contains("monsters") && j["monsters"].is_array()) {
        for (const auto& mj : j["monsters"])
            e.monsters.push_back(MonsterEntry::fromJson(mj));
    }
    return e;
}

// ---------------------------------------------------------------------------
// EncounterBuilder — fluent
// ---------------------------------------------------------------------------

EncounterBuilder& EncounterBuilder::addMonster(const std::string& monsterId,
                                                 const std::string& name,
                                                 int xpValue,
                                                 int count,
                                                 int challengeRating) {
    m_encounter.monsters.push_back({ monsterId, name, xpValue, count, challengeRating });
    return *this;
}

// ---------------------------------------------------------------------------
// XP threshold table (D&D 5e, levels 1–20)
// ---------------------------------------------------------------------------

static const int s_xpThresholds[20][4] = {
    //  Easy  Med  Hard  Deadly
    {    25,   50,   75,   100 },  // 1
    {    50,  100,  150,   200 },  // 2
    {    75,  150,  225,   400 },  // 3
    {   125,  250,  375,   500 },  // 4
    {   250,  500,  750,  1100 },  // 5
    {   300,  600,  900,  1400 },  // 6
    {   350,  750, 1100,  1700 },  // 7
    {   450,  900, 1400,  2100 },  // 8
    {   550, 1100, 1600,  2400 },  // 9
    {   600, 1200, 1900,  2800 },  // 10
    {   800, 1600, 2400,  3600 },  // 11
    {  1000, 2000, 3000,  4500 },  // 12
    {  1100, 2200, 3400,  5100 },  // 13
    {  1250, 2500, 3800,  5700 },  // 14
    {  1400, 2800, 4300,  6400 },  // 15
    {  1600, 3200, 5000,  7500 },  // 16
    {  2000, 3900, 5800,  8700 },  // 17
    {  2100, 4200, 6300,  9500 },  // 18
    {  2400, 4900, 7300, 10900 },  // 19
    {  2800, 5700, 8500, 12700 },  // 20
};

int EncounterBuilder::xpThreshold(int characterLevel, EncounterDifficulty difficulty) {
    int level = std::clamp(characterLevel, 1, 20) - 1;
    int col   = static_cast<int>(difficulty);
    return s_xpThresholds[level][col];
}

float EncounterBuilder::encounterMultiplier(int monsterCount) {
    if (monsterCount <= 0)  return 0.0f;
    if (monsterCount == 1)  return 1.0f;
    if (monsterCount == 2)  return 1.5f;
    if (monsterCount <= 6)  return 2.0f;
    if (monsterCount <= 10) return 2.5f;
    if (monsterCount <= 14) return 3.0f;
    return 4.0f;
}

// ---------------------------------------------------------------------------
// Budget / difficulty evaluation
// ---------------------------------------------------------------------------

EncounterBudget EncounterBuilder::calculateBudget(const Party& party) {
    EncounterBudget budget;
    for (const auto& member : party.getMembers()) {
        if (!member.isAlive) continue;
        budget.easyXp   += xpThreshold(member.characterLevel, EncounterDifficulty::Easy);
        budget.mediumXp += xpThreshold(member.characterLevel, EncounterDifficulty::Medium);
        budget.hardXp   += xpThreshold(member.characterLevel, EncounterDifficulty::Hard);
        budget.deadlyXp += xpThreshold(member.characterLevel, EncounterDifficulty::Deadly);
    }
    return budget;
}

EncounterDifficulty EncounterBuilder::evaluateDifficulty(const Encounter& enc,
                                                           const Party& party) {
    int xp     = enc.adjustedXp();
    auto budget = calculateBudget(party);

    if (xp >= budget.deadlyXp) return EncounterDifficulty::Deadly;
    if (xp >= budget.hardXp)   return EncounterDifficulty::Hard;
    if (xp >= budget.mediumXp) return EncounterDifficulty::Medium;
    return EncounterDifficulty::Easy;
}

bool EncounterBuilder::isValidForDifficulty(const Encounter& enc,
                                              const Party& party,
                                              EncounterDifficulty target) {
    return evaluateDifficulty(enc, party) == target;
}

} // namespace Phyxel::Core
