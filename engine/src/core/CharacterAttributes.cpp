#include "core/CharacterAttributes.h"

#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// AbilityType helpers
// ---------------------------------------------------------------------------

const char* abilityShortName(AbilityType ability) {
    switch (ability) {
        case AbilityType::Strength:     return "STR";
        case AbilityType::Dexterity:    return "DEX";
        case AbilityType::Constitution: return "CON";
        case AbilityType::Intelligence: return "INT";
        case AbilityType::Wisdom:       return "WIS";
        case AbilityType::Charisma:     return "CHA";
        default:                        return "???";
    }
}

const char* abilityFullName(AbilityType ability) {
    switch (ability) {
        case AbilityType::Strength:     return "Strength";
        case AbilityType::Dexterity:    return "Dexterity";
        case AbilityType::Constitution: return "Constitution";
        case AbilityType::Intelligence: return "Intelligence";
        case AbilityType::Wisdom:       return "Wisdom";
        case AbilityType::Charisma:     return "Charisma";
        default:                        return "Unknown";
    }
}

AbilityType abilityFromString(const char* s) {
    if (!s) throw std::invalid_argument("null ability string");

    // Accept short names (case-insensitive)
    auto eq = [&](const char* cmp) {
        size_t n = strlen(cmp);
        if (strlen(s) != n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower(s[i]) != std::tolower(cmp[i])) return false;
        return true;
    };

    if (eq("STR") || eq("Strength"))     return AbilityType::Strength;
    if (eq("DEX") || eq("Dexterity"))    return AbilityType::Dexterity;
    if (eq("CON") || eq("Constitution")) return AbilityType::Constitution;
    if (eq("INT") || eq("Intelligence")) return AbilityType::Intelligence;
    if (eq("WIS") || eq("Wisdom"))       return AbilityType::Wisdom;
    if (eq("CHA") || eq("Charisma"))     return AbilityType::Charisma;

    throw std::invalid_argument(std::string("Unknown ability: ") + s);
}

// ---------------------------------------------------------------------------
// AbilityScore
// ---------------------------------------------------------------------------

int AbilityScore::total() const {
    return std::clamp(base + racial + equipment + temporary, 1, 30);
}

int AbilityScore::modifier() const {
    // Floor division: (10 → 0, 9 → -1, 8 → -1, 12 → +1, 13 → +1, 20 → +5)
    int t = total();
    // In C++, integer division truncates toward zero; for negatives we need floor.
    return (t - 10) >= 0 ? (t - 10) / 2 : (t - 11) / 2;
}

nlohmann::json AbilityScore::toJson() const {
    return {
        {"base",      base},
        {"racial",    racial},
        {"equipment", equipment},
        {"temporary", temporary},
        {"total",     total()},
        {"modifier",  modifier()}
    };
}

void AbilityScore::fromJson(const nlohmann::json& j) {
    if (j.contains("base"))      base      = j["base"].get<int>();
    if (j.contains("racial"))    racial    = j["racial"].get<int>();
    if (j.contains("equipment")) equipment = j["equipment"].get<int>();
    if (j.contains("temporary")) temporary = j["temporary"].get<int>();
}

// ---------------------------------------------------------------------------
// CharacterAttributes
// ---------------------------------------------------------------------------

const AbilityScore& CharacterAttributes::get(AbilityType type) const {
    switch (type) {
        case AbilityType::Strength:     return strength;
        case AbilityType::Dexterity:    return dexterity;
        case AbilityType::Constitution: return constitution;
        case AbilityType::Intelligence: return intelligence;
        case AbilityType::Wisdom:       return wisdom;
        case AbilityType::Charisma:     return charisma;
        default: throw std::invalid_argument("Unknown AbilityType");
    }
}

AbilityScore& CharacterAttributes::get(AbilityType type) {
    switch (type) {
        case AbilityType::Strength:     return strength;
        case AbilityType::Dexterity:    return dexterity;
        case AbilityType::Constitution: return constitution;
        case AbilityType::Intelligence: return intelligence;
        case AbilityType::Wisdom:       return wisdom;
        case AbilityType::Charisma:     return charisma;
        default: throw std::invalid_argument("Unknown AbilityType");
    }
}

void CharacterAttributes::applyStandardArray(int str, int dex, int con, int intel, int wis, int cha) {
    strength.base     = str;
    dexterity.base    = dex;
    constitution.base = con;
    intelligence.base = intel;
    wisdom.base       = wis;
    charisma.base     = cha;
}

void CharacterAttributes::setAll(int str, int dex, int con, int intel, int wis, int cha) {
    applyStandardArray(str, dex, con, intel, wis, cha);
}

nlohmann::json CharacterAttributes::toJson() const {
    return {
        {"strength",     strength.toJson()},
        {"dexterity",    dexterity.toJson()},
        {"constitution", constitution.toJson()},
        {"intelligence", intelligence.toJson()},
        {"wisdom",       wisdom.toJson()},
        {"charisma",     charisma.toJson()}
    };
}

void CharacterAttributes::fromJson(const nlohmann::json& j) {
    if (j.contains("strength"))     strength.fromJson(j["strength"]);
    if (j.contains("dexterity"))    dexterity.fromJson(j["dexterity"]);
    if (j.contains("constitution")) constitution.fromJson(j["constitution"]);
    if (j.contains("intelligence")) intelligence.fromJson(j["intelligence"]);
    if (j.contains("wisdom"))       wisdom.fromJson(j["wisdom"]);
    if (j.contains("charisma"))     charisma.fromJson(j["charisma"]);
}

} // namespace Core
} // namespace Phyxel
