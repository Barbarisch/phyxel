#include "core/SpellDefinition.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// Name helpers — SpellSchool
// ---------------------------------------------------------------------------

const char* spellSchoolName(SpellSchool s) {
    switch (s) {
        case SpellSchool::Abjuration:   return "Abjuration";
        case SpellSchool::Conjuration:  return "Conjuration";
        case SpellSchool::Divination:   return "Divination";
        case SpellSchool::Enchantment:  return "Enchantment";
        case SpellSchool::Evocation:    return "Evocation";
        case SpellSchool::Illusion:     return "Illusion";
        case SpellSchool::Necromancy:   return "Necromancy";
        case SpellSchool::Transmutation:return "Transmutation";
        default:                        return "Unknown";
    }
}

SpellSchool spellSchoolFromString(const char* s) {
    if (!s) throw std::invalid_argument("null school string");
    auto eq = [&](const char* c) { return _stricmp(s, c) == 0; };
    if (eq("Abjuration"))    return SpellSchool::Abjuration;
    if (eq("Conjuration"))   return SpellSchool::Conjuration;
    if (eq("Divination"))    return SpellSchool::Divination;
    if (eq("Enchantment"))   return SpellSchool::Enchantment;
    if (eq("Evocation"))     return SpellSchool::Evocation;
    if (eq("Illusion"))      return SpellSchool::Illusion;
    if (eq("Necromancy"))    return SpellSchool::Necromancy;
    if (eq("Transmutation")) return SpellSchool::Transmutation;
    throw std::invalid_argument(std::string("Unknown school: ") + s);
}

// ---------------------------------------------------------------------------
// Name helpers — CastingTime
// ---------------------------------------------------------------------------

const char* castingTimeName(CastingTime ct) {
    switch (ct) {
        case CastingTime::Action:      return "Action";
        case CastingTime::BonusAction: return "BonusAction";
        case CastingTime::Reaction:    return "Reaction";
        case CastingTime::OneMinute:   return "OneMinute";
        case CastingTime::TenMinutes:  return "TenMinutes";
        case CastingTime::OneHour:     return "OneHour";
        default:                       return "Action";
    }
}

CastingTime castingTimeFromString(const char* s) {
    if (!s) return CastingTime::Action;
    auto eq = [&](const char* c) { return _stricmp(s, c) == 0; };
    if (eq("BonusAction") || eq("Bonus Action")) return CastingTime::BonusAction;
    if (eq("Reaction"))                          return CastingTime::Reaction;
    if (eq("OneMinute")   || eq("1 Minute"))     return CastingTime::OneMinute;
    if (eq("TenMinutes")  || eq("10 Minutes"))   return CastingTime::TenMinutes;
    if (eq("OneHour")     || eq("1 Hour"))       return CastingTime::OneHour;
    return CastingTime::Action;
}

// ---------------------------------------------------------------------------
// Name helpers — SpellResolutionType
// ---------------------------------------------------------------------------

const char* spellResolutionTypeName(SpellResolutionType r) {
    switch (r) {
        case SpellResolutionType::AttackRoll:  return "AttackRoll";
        case SpellResolutionType::SavingThrow: return "SavingThrow";
        case SpellResolutionType::AutoHit:     return "AutoHit";
        case SpellResolutionType::Utility:     return "Utility";
        default:                               return "AttackRoll";
    }
}

SpellResolutionType spellResolutionTypeFromString(const char* s) {
    if (!s) return SpellResolutionType::AttackRoll;
    auto eq = [&](const char* c) { return _stricmp(s, c) == 0; };
    if (eq("SavingThrow")) return SpellResolutionType::SavingThrow;
    if (eq("AutoHit"))     return SpellResolutionType::AutoHit;
    if (eq("Utility"))     return SpellResolutionType::Utility;
    return SpellResolutionType::AttackRoll;
}

// ---------------------------------------------------------------------------
// SpellDefinition — scaling helpers
// ---------------------------------------------------------------------------

int SpellDefinition::cantripDiceMultiplier(int characterLevel) {
    if (characterLevel >= 17) return 4;
    if (characterLevel >= 11) return 3;
    if (characterLevel >= 5)  return 2;
    return 1;
}

DiceExpression SpellDefinition::cantripDiceAt(int characterLevel) const {
    DiceExpression scaled = baseDamage;
    scaled.count *= cantripDiceMultiplier(characterLevel);
    return scaled;
}

DiceExpression SpellDefinition::damageAt(int slotLevel) const {
    DiceExpression scaled = baseDamage;
    int extraLevels = std::max(0, slotLevel - level);
    if (upcastExtraPerSlot.count > 0 || upcastExtraPerSlot.modifier != 0) {
        scaled.count    += upcastExtraPerSlot.count * extraLevels;
        scaled.modifier += upcastExtraPerSlot.modifier * extraLevels;
    }
    return scaled;
}

DiceExpression SpellDefinition::healDiceAt(int slotLevel) const {
    DiceExpression scaled = healDice;
    int extraLevels = std::max(0, slotLevel - level);
    if (upcastHealPerSlot.count > 0 || upcastHealPerSlot.modifier != 0) {
        scaled.count    += upcastHealPerSlot.count * extraLevels;
        scaled.modifier += upcastHealPerSlot.modifier * extraLevels;
    }
    return scaled;
}

// ---------------------------------------------------------------------------
// SpellDefinition — serialization
// ---------------------------------------------------------------------------

static std::string componentSetToString(const std::set<SpellComponent>& comps) {
    std::string s;
    if (comps.count(SpellComponent::Verbal))   s += "V";
    if (comps.count(SpellComponent::Somatic))  s += "S";
    if (comps.count(SpellComponent::Material)) s += "M";
    return s;
}

static std::set<SpellComponent> componentSetFromString(const std::string& s) {
    std::set<SpellComponent> comps;
    for (char c : s) {
        if (c == 'V' || c == 'v') comps.insert(SpellComponent::Verbal);
        if (c == 'S' || c == 's') comps.insert(SpellComponent::Somatic);
        if (c == 'M' || c == 'm') comps.insert(SpellComponent::Material);
    }
    return comps;
}

nlohmann::json SpellDefinition::toJson() const {
    nlohmann::json j;
    j["id"]          = id;
    j["name"]        = name;
    j["level"]       = level;
    j["school"]      = spellSchoolName(school);
    j["components"]  = componentSetToString(components);
    j["material"]    = materialDescription;
    j["castingTime"] = castingTimeName(castingTime);
    j["rangeInFeet"] = rangeInFeet;
    j["isSelf"]      = isSelf;
    j["isTouch"]     = isTouch;
    j["concentration"]       = requiresConcentration;
    j["durationSeconds"]     = durationSeconds;
    j["durationDescription"] = durationDescription;
    j["resolutionType"]      = spellResolutionTypeName(resolutionType);
    j["savingThrowAbility"]  = abilityShortName(savingThrowAbility);
    j["halfDamageOnSave"]    = halfDamageOnSave;
    j["baseDamage"]          = baseDamage.toString();
    j["damageType"]          = damageTypeToString(damageType);
    j["upcastExtraPerSlot"]  = upcastExtraPerSlot.toString();
    j["healBase"]            = healBase;
    j["healDice"]            = healDice.toString();
    j["upcastHealPerSlot"]   = upcastHealPerSlot.toString();
    j["description"]         = description;
    j["classes"]             = classes;
    return j;
}

SpellDefinition SpellDefinition::fromJson(const nlohmann::json& j) {
    SpellDefinition d;
    d.id    = j.value("id",   "");
    d.name  = j.value("name", d.id);
    d.level = j.value("level", 0);

    std::string school = j.value("school", "Evocation");
    try { d.school = spellSchoolFromString(school.c_str()); }
    catch (...) { d.school = SpellSchool::Evocation; }

    std::string comps = j.value("components", "VS");
    d.components = componentSetFromString(comps);
    d.materialDescription = j.value("material", "");

    std::string ct = j.value("castingTime", "Action");
    d.castingTime = castingTimeFromString(ct.c_str());

    d.rangeInFeet = j.value("rangeInFeet", 0);
    d.isSelf  = j.value("isSelf",  false);
    d.isTouch = j.value("isTouch", false);

    d.requiresConcentration = j.value("concentration", false);
    d.durationSeconds       = j.value("durationSeconds", 0.0f);
    d.durationDescription   = j.value("durationDescription", "Instantaneous");

    std::string rt = j.value("resolutionType", "AttackRoll");
    d.resolutionType = spellResolutionTypeFromString(rt.c_str());

    std::string saveAbility = j.value("savingThrowAbility", "DEX");
    try { d.savingThrowAbility = abilityFromString(saveAbility.c_str()); }
    catch (...) { d.savingThrowAbility = AbilityType::Dexterity; }

    d.halfDamageOnSave = j.value("halfDamageOnSave", false);

    std::string dmg = j.value("baseDamage", "");
    if (!dmg.empty()) {
        try { d.baseDamage = DiceExpression::parse(dmg); } catch (...) {}
    }

    std::string dmgType = j.value("damageType", "Force");
    try { d.damageType = damageTypeFromString(dmgType.c_str()); }
    catch (...) { d.damageType = DamageType::Force; }

    std::string upcast = j.value("upcastExtraPerSlot", "");
    if (!upcast.empty()) {
        try { d.upcastExtraPerSlot = DiceExpression::parse(upcast); } catch (...) {}
    }

    d.healBase = j.value("healBase", 0);
    std::string healStr = j.value("healDice", "");
    if (!healStr.empty()) {
        try { d.healDice = DiceExpression::parse(healStr); } catch (...) {}
    }
    std::string upcastHeal = j.value("upcastHealPerSlot", "");
    if (!upcastHeal.empty()) {
        try { d.upcastHealPerSlot = DiceExpression::parse(upcastHeal); } catch (...) {}
    }

    d.description = j.value("description", "");

    if (j.contains("classes") && j["classes"].is_array()) {
        for (const auto& c : j["classes"]) {
            if (c.is_string()) d.classes.push_back(c.get<std::string>());
        }
    }

    return d;
}

// ---------------------------------------------------------------------------
// SpellRegistry
// ---------------------------------------------------------------------------

SpellRegistry& SpellRegistry::instance() {
    static SpellRegistry s_instance;
    return s_instance;
}

void SpellRegistry::registerSpell(SpellDefinition def) {
    if (def.id.empty()) {
        LOG_WARN("SpellRegistry", "Cannot register spell with empty ID");
        return;
    }
    m_spells.emplace(def.id, std::move(def));
}

const SpellDefinition* SpellRegistry::getSpell(const std::string& id) const {
    auto it = m_spells.find(id);
    return (it != m_spells.end()) ? &it->second : nullptr;
}

std::vector<const SpellDefinition*> SpellRegistry::getSpellsForClass(const std::string& classId) const {
    std::vector<const SpellDefinition*> result;
    for (const auto& [id, def] : m_spells) {
        if (std::find(def.classes.begin(), def.classes.end(), classId) != def.classes.end())
            result.push_back(&def);
    }
    return result;
}

std::vector<const SpellDefinition*> SpellRegistry::getSpellsOfLevel(int level) const {
    std::vector<const SpellDefinition*> result;
    for (const auto& [id, def] : m_spells) {
        if (def.level == level) result.push_back(&def);
    }
    return result;
}

std::vector<const SpellDefinition*> SpellRegistry::getAllSpells() const {
    std::vector<const SpellDefinition*> result;
    result.reserve(m_spells.size());
    for (const auto& [id, def] : m_spells) result.push_back(&def);
    return result;
}

int SpellRegistry::loadFromDirectory(const std::string& dirPath) {
    int loaded = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (entry.path().extension() == ".json") {
            if (loadFromFile(entry.path().string())) ++loaded;
        }
    }
    if (ec) LOG_WARN("SpellRegistry", "Error iterating '{}': {}", dirPath, ec.message());
    return loaded;
}

bool SpellRegistry::loadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_WARN("SpellRegistry", "Could not open spell file: {}", filePath);
        return false;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        // File may be a single spell object or an array of spells
        if (j.is_array()) {
            for (const auto& spellJson : j) {
                try {
                    auto def = SpellDefinition::fromJson(spellJson);
                    if (!def.id.empty()) registerSpell(std::move(def));
                } catch (const std::exception& e) {
                    LOG_WARN("SpellRegistry", "Failed to parse spell in '{}': {}", filePath, e.what());
                }
            }
        } else if (j.is_object()) {
            auto def = SpellDefinition::fromJson(j);
            if (!def.id.empty()) registerSpell(std::move(def));
        }
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("SpellRegistry", "JSON parse error in '{}': {}", filePath, e.what());
        return false;
    }
}

} // namespace Core
} // namespace Phyxel
