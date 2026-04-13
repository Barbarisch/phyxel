#include "core/DamageTypes.h"
#include <stdexcept>
#include <cstring>

namespace Phyxel {
namespace Core {

const char* damageTypeToString(DamageType type) {
    switch (type) {
        case DamageType::Physical:    return "Physical";
        case DamageType::Fire:        return "Fire";
        case DamageType::Ice:         return "Ice";
        case DamageType::Poison:      return "Poison";
        case DamageType::Bludgeoning: return "Bludgeoning";
        case DamageType::Piercing:    return "Piercing";
        case DamageType::Slashing:    return "Slashing";
        case DamageType::Cold:        return "Cold";
        case DamageType::Lightning:   return "Lightning";
        case DamageType::Thunder:     return "Thunder";
        case DamageType::Acid:        return "Acid";
        case DamageType::Necrotic:    return "Necrotic";
        case DamageType::Radiant:     return "Radiant";
        case DamageType::Psychic:     return "Psychic";
        case DamageType::Force:       return "Force";
        default:                      return "Unknown";
    }
}

DamageType damageTypeFromString(const char* s) {
    if (!s) throw std::invalid_argument("null damage type string");
    auto eq = [&](const char* c) { return _stricmp(s, c) == 0; };
    if (eq("Physical"))    return DamageType::Physical;
    if (eq("Fire"))        return DamageType::Fire;
    if (eq("Ice"))         return DamageType::Ice;
    if (eq("Poison"))      return DamageType::Poison;
    if (eq("Bludgeoning")) return DamageType::Bludgeoning;
    if (eq("Piercing"))    return DamageType::Piercing;
    if (eq("Slashing"))    return DamageType::Slashing;
    if (eq("Cold"))        return DamageType::Cold;
    if (eq("Lightning"))   return DamageType::Lightning;
    if (eq("Thunder"))     return DamageType::Thunder;
    if (eq("Acid"))        return DamageType::Acid;
    if (eq("Necrotic"))    return DamageType::Necrotic;
    if (eq("Radiant"))     return DamageType::Radiant;
    if (eq("Psychic"))     return DamageType::Psychic;
    if (eq("Force"))       return DamageType::Force;
    throw std::invalid_argument(std::string("Unknown damage type: ") + s);
}

// ---------------------------------------------------------------------------
// DamageResistances
// ---------------------------------------------------------------------------

DamageResistance DamageResistances::getResistance(DamageType type) const {
    if (immune.count(type))     return DamageResistance::Immune;
    if (resistant.count(type))  return DamageResistance::Resistant;
    if (vulnerable.count(type)) return DamageResistance::Vulnerable;
    return DamageResistance::Normal;
}

static nlohmann::json typeSetToJson(const std::set<DamageType>& s) {
    nlohmann::json arr = nlohmann::json::array();
    for (auto t : s) arr.push_back(damageTypeToString(t));
    return arr;
}

static void typeSetFromJson(const nlohmann::json& arr, std::set<DamageType>& out) {
    out.clear();
    for (const auto& j : arr) {
        try { out.insert(damageTypeFromString(j.get<std::string>().c_str())); }
        catch (...) {}
    }
}

nlohmann::json DamageResistances::toJson() const {
    return {
        {"resistant",  typeSetToJson(resistant)},
        {"vulnerable", typeSetToJson(vulnerable)},
        {"immune",     typeSetToJson(immune)}
    };
}

void DamageResistances::fromJson(const nlohmann::json& j) {
    if (j.contains("resistant"))  typeSetFromJson(j["resistant"],  resistant);
    if (j.contains("vulnerable")) typeSetFromJson(j["vulnerable"], vulnerable);
    if (j.contains("immune"))     typeSetFromJson(j["immune"],     immune);
}

} // namespace Core
} // namespace Phyxel
