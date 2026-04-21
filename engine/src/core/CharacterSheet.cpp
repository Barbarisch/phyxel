#include "core/CharacterSheet.h"
#include "core/ProficiencySystem.h"
#include "core/ClassDefinition.h"
#include "core/RaceDefinition.h"

#include <algorithm>
#include <stdexcept>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// HitDicePool
// ---------------------------------------------------------------------------

nlohmann::json HitDicePool::toJson() const {
    return {{"classId", classId}, {"faces", faces}, {"total", total}, {"remaining", remaining}};
}

void HitDicePool::fromJson(const nlohmann::json& j) {
    classId   = j.value("classId", "");
    faces     = j.value("faces", 8);
    total     = j.value("total", 0);
    remaining = j.value("remaining", 0);
}

// ---------------------------------------------------------------------------
// DeathSaveState
// ---------------------------------------------------------------------------

nlohmann::json DeathSaveState::toJson() const {
    return {{"successes", successes}, {"failures", failures}, {"stable", stable}};
}

void DeathSaveState::fromJson(const nlohmann::json& j) {
    successes = j.value("successes", 0);
    failures  = j.value("failures", 0);
    stable    = j.value("stable", false);
}

// ---------------------------------------------------------------------------
// CharacterSheet — computed queries
// ---------------------------------------------------------------------------

int CharacterSheet::totalLevel() const {
    int total = 0;
    for (const auto& cl : classes) total += cl.level;
    return std::max(1, total);
}

int CharacterSheet::proficiencyBonus() const {
    return ProficiencySystem::proficiencyBonus(totalLevel());
}

ProficiencyLevel CharacterSheet::skillProficiencyLevel(Skill skill) const {
    for (const auto& entry : skills) {
        if (entry.skill == skill) return entry.level;
    }
    return ProficiencyLevel::None;
}

bool CharacterSheet::isProficientWith(Skill skill) const {
    return skillProficiencyLevel(skill) != ProficiencyLevel::None;
}

bool CharacterSheet::hasSavingThrowProficiency(AbilityType ability) const {
    return savingThrowProficiencies.count(ability) > 0;
}

int CharacterSheet::skillBonus(Skill skill) const {
    return ProficiencySystem::skillBonus(attributes, skill, skillProficiencyLevel(skill), totalLevel());
}

int CharacterSheet::savingThrowBonus(AbilityType ability) const {
    return ProficiencySystem::savingThrowBonus(
        attributes, ability, hasSavingThrowProficiency(ability), totalLevel());
}

int CharacterSheet::passivePerception() const {
    return ProficiencySystem::passiveCheck(skillBonus(Skill::Perception));
}

// ---------------------------------------------------------------------------
// HP helpers
// ---------------------------------------------------------------------------

int CharacterSheet::takeDamage(int amount) {
    if (amount <= 0) return 0;

    // Temporary HP absorbs first
    int fromTemp = std::min(amount, temporaryHP);
    temporaryHP -= fromTemp;
    amount -= fromTemp;

    int fromCurrent = std::min(amount, currentHP);
    currentHP -= fromCurrent;
    return fromCurrent;
}

int CharacterSheet::heal(int amount) {
    if (amount <= 0 || currentHP <= 0) return 0;
    int actual = std::min(amount, maxHP - currentHP);
    currentHP += actual;
    return actual;
}

void CharacterSheet::grantTemporaryHP(int amount) {
    // Temp HP never stacks — take the higher value
    temporaryHP = std::max(temporaryHP, amount);
}

// ---------------------------------------------------------------------------
// Recalculate derived fields
// ---------------------------------------------------------------------------

void CharacterSheet::recalculate() {
    initiative = attributes.initiativeBonus();
    // AC recalculated externally by AttackResolver once equipment is known;
    // default (no armor) = unarmored AC
    if (armorClass < 10) armorClass = attributes.unarmoredAC();
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json CharacterSheet::toJson() const {
    nlohmann::json j;
    j["characterId"]      = characterId;
    j["name"]             = name;
    j["raceId"]           = raceId;
    j["experiencePoints"] = experiencePoints;
    j["maxHP"]            = maxHP;
    j["currentHP"]        = currentHP;
    j["temporaryHP"]      = temporaryHP;
    j["armorClass"]       = armorClass;
    j["speed"]            = speed;
    j["initiative"]       = initiative;
    j["availableASIs"]    = availableASIs;
    j["attributes"]       = attributes.toJson();
    j["deathSaves"]       = deathSaves.toJson();

    // Classes
    nlohmann::json classArr = nlohmann::json::array();
    for (const auto& cl : classes) {
        classArr.push_back({{"classId", cl.classId}, {"level", cl.level}, {"subclassId", cl.subclassId}});
    }
    j["classes"] = classArr;

    // Hit dice pools
    nlohmann::json hdArr = nlohmann::json::array();
    for (const auto& pool : hitDicePools) hdArr.push_back(pool.toJson());
    j["hitDicePools"] = hdArr;

    // Skills
    nlohmann::json skillArr = nlohmann::json::array();
    for (const auto& se : skills) {
        skillArr.push_back({
            {"skill", ProficiencySystem::skillName(se.skill)},
            {"level", static_cast<int>(se.level)}
        });
    }
    j["skills"] = skillArr;

    // Saving throws
    nlohmann::json saveArr = nlohmann::json::array();
    for (auto a : savingThrowProficiencies) saveArr.push_back(abilityShortName(a));
    j["savingThrowProficiencies"] = saveArr;

    j["armorProficiencies"]  = std::vector<std::string>(armorProficiencies.begin(), armorProficiencies.end());
    j["weaponProficiencies"] = std::vector<std::string>(weaponProficiencies.begin(), weaponProficiencies.end());
    j["toolProficiencies"]   = std::vector<std::string>(toolProficiencies.begin(), toolProficiencies.end());
    j["languages"]           = languages;
    j["featIds"]             = featIds;
    j["earnedFeatureIds"]    = earnedFeatureIds;
    return j;
}

void CharacterSheet::fromJson(const nlohmann::json& j) {
    characterId      = j.value("characterId", "");
    name             = j.value("name", "");
    raceId           = j.value("raceId", "");
    experiencePoints = j.value("experiencePoints", 0);
    maxHP            = j.value("maxHP", 0);
    currentHP        = j.value("currentHP", maxHP);
    temporaryHP      = j.value("temporaryHP", 0);
    armorClass       = j.value("armorClass", 10);
    speed            = j.value("speed", 30);
    initiative       = j.value("initiative", 0);
    availableASIs    = j.value("availableASIs", 0);

    if (j.contains("attributes")) attributes.fromJson(j["attributes"]);
    if (j.contains("deathSaves")) deathSaves.fromJson(j["deathSaves"]);

    if (j.contains("classes")) {
        classes.clear();
        for (const auto& cj : j["classes"]) {
            ClassLevel cl;
            cl.classId    = cj.value("classId", "");
            cl.level      = cj.value("level", 1);
            cl.subclassId = cj.value("subclassId", "");
            classes.push_back(cl);
        }
    }

    if (j.contains("hitDicePools")) {
        hitDicePools.clear();
        for (const auto& hj : j["hitDicePools"]) {
            HitDicePool pool;
            pool.fromJson(hj);
            hitDicePools.push_back(pool);
        }
    }

    if (j.contains("skills")) {
        skills.clear();
        for (const auto& sj : j["skills"]) {
            try {
                SkillEntry se;
                se.skill = ProficiencySystem::skillFromString(sj["skill"].get<std::string>().c_str());
                se.level = static_cast<ProficiencyLevel>(sj.value("level", 0));
                skills.push_back(se);
            } catch (...) {}
        }
    }

    if (j.contains("savingThrowProficiencies")) {
        savingThrowProficiencies.clear();
        for (const auto& s : j["savingThrowProficiencies"]) {
            try { savingThrowProficiencies.insert(abilityFromString(s.get<std::string>().c_str())); }
            catch (...) {}
        }
    }

    auto loadStrings = [&](const char* key, std::set<std::string>& target) {
        if (j.contains(key)) {
            target.clear();
            for (const auto& s : j[key]) target.insert(s.get<std::string>());
        }
    };
    loadStrings("armorProficiencies",  armorProficiencies);
    loadStrings("weaponProficiencies", weaponProficiencies);
    loadStrings("toolProficiencies",   toolProficiencies);

    if (j.contains("languages"))        languages        = j["languages"].get<std::vector<std::string>>();
    if (j.contains("featIds"))          featIds          = j["featIds"].get<std::vector<std::string>>();
    if (j.contains("earnedFeatureIds")) earnedFeatureIds = j["earnedFeatureIds"].get<std::vector<std::string>>();
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

CharacterSheet CharacterSheet::create(const std::string& characterName,
                                       const std::string& raceId,
                                       const std::string& classId) {
    CharacterSheet sheet;
    sheet.name        = characterName;
    sheet.raceId      = raceId;
    sheet.characterId = characterName + "_" + classId;

    // Apply race
    const auto* race = RaceRegistry::instance().getRace(raceId);
    if (race) {
        race->applyTo(sheet.attributes);
        sheet.speed = race->speed;
        for (const auto& lang : race->languages) sheet.languages.push_back(lang);
        for (const auto& prof : race->proficiencies) sheet.weaponProficiencies.insert(prof);
    }

    // Apply class level 1 defaults
    const auto* cls = ClassRegistry::instance().getClass(classId);
    if (cls) {
        ClassLevel cl;
        cl.classId = classId;
        cl.level   = 1;
        sheet.classes.push_back(cl);

        for (auto a : cls->savingThrowProficiencies)
            sheet.savingThrowProficiencies.insert(a);
        for (const auto& p : cls->armorProficiencies)  sheet.armorProficiencies.insert(p);
        for (const auto& p : cls->weaponProficiencies) sheet.weaponProficiencies.insert(p);
        for (const auto& p : cls->toolProficiencies)   sheet.toolProficiencies.insert(p);

        // Level-1 hit points: max hit die + CON modifier
        int conMod = sheet.attributes.constitution.modifier();
        sheet.maxHP    = cls->hitDieFaces + conMod;
        sheet.currentHP = sheet.maxHP;

        // Hit dice pool
        HitDicePool pool;
        pool.classId   = classId;
        pool.faces     = cls->hitDieFaces;
        pool.total     = 1;
        pool.remaining = 1;
        sheet.hitDicePools.push_back(pool);

        // Level-1 features
        for (const auto& feat : cls->getFeaturesAtLevel(1))
            sheet.earnedFeatureIds.push_back(feat.id);
    }

    sheet.recalculate();
    return sheet;
}

} // namespace Core
} // namespace Phyxel
