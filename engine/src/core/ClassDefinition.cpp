#include "core/ClassDefinition.h"
#include "utils/Logger.h"

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// ClassFeature
// ---------------------------------------------------------------------------

nlohmann::json ClassFeature::toJson() const {
    return {{"id", id}, {"name", name}, {"description", description}, {"level", level}};
}

ClassFeature ClassFeature::fromJson(const nlohmann::json& j) {
    ClassFeature f;
    f.id          = j.value("id", "");
    f.name        = j.value("name", f.id);
    f.description = j.value("description", "");
    f.level       = j.value("level", 1);
    return f;
}

// ---------------------------------------------------------------------------
// ClassDefinition queries
// ---------------------------------------------------------------------------

std::vector<ClassFeature> ClassDefinition::getFeaturesAtLevel(int level) const {
    auto it = features.find(level);
    if (it != features.end()) return it->second;
    return {};
}

std::vector<ClassFeature> ClassDefinition::getAllFeaturesUpToLevel(int level) const {
    std::vector<ClassFeature> result;
    for (const auto& [lvl, feats] : features) {
        if (lvl <= level) {
            result.insert(result.end(), feats.begin(), feats.end());
        }
    }
    return result;
}

bool ClassDefinition::hasASIAtLevel(int level) const {
    return std::find(asiLevels.begin(), asiLevels.end(), level) != asiLevels.end();
}

// ---------------------------------------------------------------------------
// ClassDefinition serialization
// ---------------------------------------------------------------------------

nlohmann::json ClassDefinition::toJson() const {
    nlohmann::json j;
    j["id"]          = id;
    j["name"]        = name;
    j["hitDieFaces"] = hitDieFaces;
    j["primaryAbility"] = abilityShortName(primaryAbility);
    j["skillChoices"]   = skillChoices;
    j["spellcastingAbility"] = spellcastingAbility;
    j["spellcastingType"]    = spellcastingType;
    j["asiLevels"]  = asiLevels;

    nlohmann::json saves = nlohmann::json::array();
    for (auto a : savingThrowProficiencies) saves.push_back(abilityShortName(a));
    j["savingThrows"] = saves;

    j["armorProficiencies"]  = armorProficiencies;
    j["weaponProficiencies"] = weaponProficiencies;
    j["toolProficiencies"]   = toolProficiencies;

    nlohmann::json pool = nlohmann::json::array();
    for (auto s : skillPool) pool.push_back(ProficiencySystem::skillName(s));
    j["skillPool"] = pool;

    nlohmann::json feats = nlohmann::json::object();
    for (const auto& [lvl, fvec] : features) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& f : fvec) arr.push_back(f.toJson());
        feats[std::to_string(lvl)] = arr;
    }
    j["features"] = feats;
    return j;
}

ClassDefinition ClassDefinition::fromJson(const nlohmann::json& j) {
    ClassDefinition c;
    c.id           = j.value("id", "");
    c.name         = j.value("name", c.id);
    c.hitDieFaces  = j.value("hitDieFaces", 8);
    c.skillChoices = j.value("skillChoices", 2);
    c.spellcastingAbility = j.value("spellcastingAbility", "");
    c.spellcastingType    = j.value("spellcastingType", "");

    if (j.contains("primaryAbility")) {
        try { c.primaryAbility = abilityFromString(j["primaryAbility"].get<std::string>().c_str()); }
        catch (...) {}
    }

    if (j.contains("savingThrows")) {
        for (const auto& s : j["savingThrows"]) {
            try { c.savingThrowProficiencies.push_back(abilityFromString(s.get<std::string>().c_str())); }
            catch (...) {}
        }
    }

    if (j.contains("armorProficiencies"))
        c.armorProficiencies = j["armorProficiencies"].get<std::vector<std::string>>();
    if (j.contains("weaponProficiencies"))
        c.weaponProficiencies = j["weaponProficiencies"].get<std::vector<std::string>>();
    if (j.contains("toolProficiencies"))
        c.toolProficiencies = j["toolProficiencies"].get<std::vector<std::string>>();
    if (j.contains("asiLevels"))
        c.asiLevels = j["asiLevels"].get<std::vector<int>>();

    if (j.contains("skillPool")) {
        for (const auto& s : j["skillPool"]) {
            try { c.skillPool.push_back(ProficiencySystem::skillFromString(s.get<std::string>().c_str())); }
            catch (...) { LOG_WARN("ClassDefinition", "Unknown skill in skillPool: {}", s.get<std::string>()); }
        }
    }

    if (j.contains("features") && j["features"].is_object()) {
        for (const auto& [lvlStr, fArr] : j["features"].items()) {
            try {
                int lvl = std::stoi(lvlStr);
                std::vector<ClassFeature> fvec;
                if (fArr.is_array()) {
                    for (const auto& fj : fArr) fvec.push_back(ClassFeature::fromJson(fj));
                } else if (fArr.is_object()) {
                    fvec.push_back(ClassFeature::fromJson(fArr));
                }
                c.features[lvl] = std::move(fvec);
            } catch (...) {}
        }
    }

    return c;
}

// ---------------------------------------------------------------------------
// ClassRegistry
// ---------------------------------------------------------------------------

ClassRegistry& ClassRegistry::instance() {
    static ClassRegistry s_instance;
    return s_instance;
}

bool ClassRegistry::registerClass(const ClassDefinition& def) {
    if (def.id.empty()) {
        LOG_WARN("ClassRegistry", "Cannot register class with empty ID");
        return false;
    }
    auto [it, inserted] = m_classes.emplace(def.id, def);
    if (!inserted) {
        LOG_WARN("ClassRegistry", "Class '{}' already registered — skipping", def.id);
    }
    return inserted;
}

const ClassDefinition* ClassRegistry::getClass(const std::string& id) const {
    auto it = m_classes.find(id);
    return (it != m_classes.end()) ? &it->second : nullptr;
}

bool ClassRegistry::hasClass(const std::string& id) const {
    return m_classes.find(id) != m_classes.end();
}

std::vector<std::string> ClassRegistry::getAllClassIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_classes.size());
    for (const auto& [id, _] : m_classes) ids.push_back(id);
    return ids;
}

int ClassRegistry::loadFromDirectory(const std::string& dirPath) {
    int count = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (entry.path().extension() == ".json") {
            if (loadFromFile(entry.path().string())) ++count;
        }
    }
    if (ec) LOG_WARN("ClassRegistry", "Error iterating directory '{}': {}", dirPath, ec.message());
    return count;
}

bool ClassRegistry::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_WARN("ClassRegistry", "Could not open class file: {}", filepath);
        return false;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        loadFromJson(j);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("ClassRegistry", "Failed to parse class file '{}': {}", filepath, e.what());
        return false;
    }
}

int ClassRegistry::loadFromJson(const nlohmann::json& j) {
    int count = 0;
    if (j.is_array()) {
        for (const auto& item : j) {
            try {
                if (registerClass(ClassDefinition::fromJson(item))) ++count;
            } catch (const std::exception& e) {
                LOG_WARN("ClassRegistry", "Skipping class entry: {}", e.what());
            }
        }
    } else if (j.is_object()) {
        try {
            if (registerClass(ClassDefinition::fromJson(j))) ++count;
        } catch (const std::exception& e) {
            LOG_WARN("ClassRegistry", "Skipping class object: {}", e.what());
        }
    }
    return count;
}

void ClassRegistry::clear() {
    m_classes.clear();
}

} // namespace Core
} // namespace Phyxel
