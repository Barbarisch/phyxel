#include "core/MonsterDefinition.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace Phyxel::Core {

// ============================================================================
// CreatureSize helpers
// ============================================================================

const char* creatureSizeName(CreatureSize s) {
    switch (s) {
        case CreatureSize::Tiny:       return "Tiny";
        case CreatureSize::Small:      return "Small";
        case CreatureSize::Medium:     return "Medium";
        case CreatureSize::Large:      return "Large";
        case CreatureSize::Huge:       return "Huge";
        case CreatureSize::Gargantuan: return "Gargantuan";
    }
    return "Medium";
}

CreatureSize creatureSizeFromString(const char* s) {
    std::string lower(s);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "tiny")       return CreatureSize::Tiny;
    if (lower == "small")      return CreatureSize::Small;
    if (lower == "large")      return CreatureSize::Large;
    if (lower == "huge")       return CreatureSize::Huge;
    if (lower == "gargantuan") return CreatureSize::Gargantuan;
    return CreatureSize::Medium;
}

// ============================================================================
// MonsterAttack
// ============================================================================

nlohmann::json MonsterAttack::toJson() const {
    nlohmann::json j = {
        {"name",          name},
        {"isWeaponAttack",isWeaponAttack},
        {"toHitBonus",    toHitBonus},
        {"damageDice",    damageDice},
        {"damageType",    damageType},
        {"reach",         reach},
        {"isRanged",      isRanged},
        {"rangeNormal",   rangeNormal},
        {"rangeLong",     rangeLong},
        {"requiresSave",  requiresSave},
        {"description",   description}
    };
    if (requiresSave) {
        j["saveAbility"]   = saveAbility;
        j["saveDC"]        = saveDC;
        j["effectOnFail"]  = effectOnFail;
        j["effectDuration"]= effectDuration;
    }
    return j;
}

MonsterAttack MonsterAttack::fromJson(const nlohmann::json& j) {
    MonsterAttack a;
    a.name          = j.value("name",          "");
    a.isWeaponAttack= j.value("isWeaponAttack", true);
    a.toHitBonus    = j.value("toHitBonus",     0);
    a.damageDice    = j.value("damageDice",     "1d4");
    a.damageType    = j.value("damageType",     "bludgeoning");
    a.reach         = j.value("reach",          5);
    a.isRanged      = j.value("isRanged",       false);
    a.rangeNormal   = j.value("rangeNormal",    0);
    a.rangeLong     = j.value("rangeLong",      0);
    a.requiresSave  = j.value("requiresSave",   false);
    a.saveAbility   = j.value("saveAbility",    "");
    a.saveDC        = j.value("saveDC",         0);
    a.effectOnFail  = j.value("effectOnFail",   "");
    a.effectDuration= j.value("effectDuration", -1.0f);
    a.description   = j.value("description",   "");
    return a;
}

// ============================================================================
// MonsterDefinition helpers
// ============================================================================

int MonsterDefinition::proficiencyBonus() const {
    // D&D 5e CR → proficiency bonus table
    if (challengeRating <= 4.0f)  return 2;
    if (challengeRating <= 8.0f)  return 3;
    if (challengeRating <= 12.0f) return 4;
    if (challengeRating <= 16.0f) return 5;
    if (challengeRating <= 20.0f) return 6;
    if (challengeRating <= 24.0f) return 7;
    if (challengeRating <= 28.0f) return 8;
    return 9;
}

int MonsterDefinition::rollHP() const {
    if (hitPointDice.empty()) return std::max(1, averageHP);
    auto result = DiceSystem::rollExpression(hitPointDice);
    return std::max(1, result.total);
}

bool MonsterDefinition::isImmuneTo(const std::string& conditionStr) const {
    for (const auto& ci : conditionImmunities) {
        if (_stricmp(ci.c_str(), conditionStr.c_str()) == 0) return true;
    }
    return false;
}

// ============================================================================
// MonsterDefinition serialization
// ============================================================================

nlohmann::json MonsterDefinition::toJson() const {
    nlohmann::json j;
    j["id"]          = id;
    j["name"]        = name;
    j["type"]        = type;
    j["subtype"]     = subtype;
    j["size"]        = creatureSizeName(size);
    j["alignment"]   = alignment;
    j["armorClass"]  = armorClass;
    j["armorSource"] = armorSource;
    j["hitPointDice"]= hitPointDice;
    j["averageHP"]   = averageHP;
    j["speed"]       = speed;
    j["attributes"]  = attributes.toJson();
    j["challengeRating"] = challengeRating;
    j["xpValue"]     = xpValue;
    j["passivePerception"] = passivePerception;
    j["darkvisionRange"]   = darkvisionRange;
    j["blindsightRange"]   = blindsightRange;
    j["truesightRange"]    = truesightRange;

    j["savingThrowProficiencies"] = nlohmann::json::array();
    for (auto a : savingThrowProficiencies)
        j["savingThrowProficiencies"].push_back(abilityShortName(a));

    j["skillProficiencies"] = nlohmann::json::object();
    for (const auto& [skill, level] : skillProficiencies)
        j["skillProficiencies"][skill] = (int)level;

    j["damageResistances"]  = damageResistances;
    j["damageImmunities"]   = damageImmunities;
    j["conditionImmunities"]= conditionImmunities;
    j["languages"]          = languages;
    j["tags"]               = tags;

    j["attacks"] = nlohmann::json::array();
    for (const auto& a : attacks) j["attacks"].push_back(a.toJson());

    j["traits"] = nlohmann::json::array();
    for (const auto& [tname, tdesc] : traits)
        j["traits"].push_back({{"name", tname}, {"description", tdesc}});

    return j;
}

static AbilityType abilityTypeFromShortStr(const std::string& s) {
    std::string u = s;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
    if (u == "STR") return AbilityType::Strength;
    if (u == "DEX") return AbilityType::Dexterity;
    if (u == "CON") return AbilityType::Constitution;
    if (u == "INT") return AbilityType::Intelligence;
    if (u == "WIS") return AbilityType::Wisdom;
    if (u == "CHA") return AbilityType::Charisma;
    return AbilityType::Strength;
}

MonsterDefinition MonsterDefinition::fromJson(const nlohmann::json& j) {
    MonsterDefinition m;
    m.id            = j.value("id",           "");
    m.name          = j.value("name",         "");
    m.type          = j.value("type",         "humanoid");
    m.subtype       = j.value("subtype",      "");
    m.size          = creatureSizeFromString(j.value("size", "Medium").c_str());
    m.alignment     = j.value("alignment",    "");
    m.armorClass    = j.value("armorClass",   10);
    m.armorSource   = j.value("armorSource",  "");
    m.hitPointDice  = j.value("hitPointDice", "");
    m.averageHP     = j.value("averageHP",    0);
    m.speed         = j.value("speed",        30);
    m.challengeRating = j.value("challengeRating", 0.0f);
    m.xpValue       = j.value("xpValue",      0);
    m.passivePerception = j.value("passivePerception", 10);
    m.darkvisionRange   = j.value("darkvisionRange",   0);
    m.blindsightRange   = j.value("blindsightRange",   0);
    m.truesightRange    = j.value("truesightRange",    0);

    if (j.contains("attributes") && j["attributes"].is_object())
        m.attributes.fromJson(j["attributes"]);

    if (j.contains("savingThrowProficiencies") && j["savingThrowProficiencies"].is_array())
        for (const auto& s : j["savingThrowProficiencies"])
            m.savingThrowProficiencies.push_back(abilityTypeFromShortStr(s.get<std::string>()));

    if (j.contains("skillProficiencies") && j["skillProficiencies"].is_object())
        for (const auto& [skill, level] : j["skillProficiencies"].items())
            m.skillProficiencies[skill] = (ProficiencyLevel)level.get<int>();

    if (j.contains("damageResistances") && j["damageResistances"].is_array())
        for (const auto& r : j["damageResistances"]) m.damageResistances.push_back(r);

    if (j.contains("damageImmunities") && j["damageImmunities"].is_array())
        for (const auto& r : j["damageImmunities"]) m.damageImmunities.push_back(r);

    if (j.contains("conditionImmunities") && j["conditionImmunities"].is_array())
        for (const auto& r : j["conditionImmunities"]) m.conditionImmunities.push_back(r);

    if (j.contains("languages") && j["languages"].is_array())
        for (const auto& l : j["languages"]) m.languages.push_back(l);

    if (j.contains("tags") && j["tags"].is_array())
        for (const auto& t : j["tags"]) m.tags.push_back(t);

    if (j.contains("attacks") && j["attacks"].is_array())
        for (const auto& a : j["attacks"]) m.attacks.push_back(MonsterAttack::fromJson(a));

    if (j.contains("traits") && j["traits"].is_array())
        for (const auto& t : j["traits"])
            m.traits.push_back({t.value("name",""), t.value("description","")});

    return m;
}

// ============================================================================
// MonsterRegistry
// ============================================================================

MonsterRegistry& MonsterRegistry::instance() {
    static MonsterRegistry inst;
    return inst;
}

bool MonsterRegistry::registerMonster(const MonsterDefinition& def) {
    if (def.id.empty()) return false;
    m_monsters[def.id] = def;
    return true;
}

const MonsterDefinition* MonsterRegistry::getMonster(const std::string& id) const {
    auto it = m_monsters.find(id);
    return (it != m_monsters.end()) ? &it->second : nullptr;
}

bool MonsterRegistry::hasMonster(const std::string& id) const {
    return m_monsters.count(id) > 0;
}

std::vector<std::string> MonsterRegistry::getAllIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_monsters.size());
    for (const auto& [id, _] : m_monsters) ids.push_back(id);
    return ids;
}

std::vector<const MonsterDefinition*> MonsterRegistry::getByTag(const std::string& tag) const {
    std::vector<const MonsterDefinition*> result;
    for (const auto& [_, m] : m_monsters)
        for (const auto& t : m.tags)
            if (_stricmp(t.c_str(), tag.c_str()) == 0) { result.push_back(&m); break; }
    return result;
}

std::vector<const MonsterDefinition*> MonsterRegistry::getByCR(float minCR, float maxCR) const {
    std::vector<const MonsterDefinition*> result;
    for (const auto& [_, m] : m_monsters)
        if (m.challengeRating >= minCR && m.challengeRating <= maxCR)
            result.push_back(&m);
    return result;
}

std::vector<const MonsterDefinition*> MonsterRegistry::getByType(const std::string& type) const {
    std::vector<const MonsterDefinition*> result;
    for (const auto& [_, m] : m_monsters)
        if (_stricmp(m.type.c_str(), type.c_str()) == 0) result.push_back(&m);
    return result;
}

int MonsterRegistry::loadFromJson(const nlohmann::json& j) {
    if (j.is_object()) {
        registerMonster(MonsterDefinition::fromJson(j));
        return 1;
    }
    if (j.is_array()) {
        int count = 0;
        for (const auto& item : j)
            if (registerMonster(MonsterDefinition::fromJson(item))) ++count;
        return count;
    }
    return 0;
}

bool MonsterRegistry::loadFromFile(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return false;
    try {
        auto j = nlohmann::json::parse(f);
        return loadFromJson(j) > 0;
    } catch (...) { return false; }
}

int MonsterRegistry::loadFromDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (entry.path().extension() == ".json")
            if (loadFromFile(entry.path().string())) ++count;
    }
    return count;
}

void MonsterRegistry::clear() {
    m_monsters.clear();
}

} // namespace Phyxel::Core
