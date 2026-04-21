#include "core/RestSystem.h"
#include <algorithm>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void RestSystem::registerCharacter(
    const std::string& entityId,
    int maxHp,
    int hitDiceCount,
    DieType hitDieType,
    int characterLevel,
    const std::string& castingType)
{
    CharacterRestState state;
    state.maxHp          = maxHp;
    state.currentHp      = maxHp;
    state.maxHitDice     = hitDiceCount;
    state.currentHitDice = hitDiceCount;
    state.hitDieType     = hitDieType;
    state.characterLevel = characterLevel;
    state.castingType    = castingType;
    m_characters[entityId] = state;
}

bool RestSystem::isRegistered(const std::string& entityId) const {
    return m_characters.count(entityId) > 0;
}

// ---------------------------------------------------------------------------
// HP / hit dice accessors
// ---------------------------------------------------------------------------

void RestSystem::setCurrentHp(const std::string& entityId, int hp) {
    auto it = m_characters.find(entityId);
    if (it == m_characters.end()) return;
    it->second.currentHp = std::clamp(hp, 0, it->second.maxHp);
}

int RestSystem::getCurrentHp(const std::string& entityId) const {
    auto it = m_characters.find(entityId);
    return (it != m_characters.end()) ? it->second.currentHp : 0;
}

int RestSystem::getMaxHp(const std::string& entityId) const {
    auto it = m_characters.find(entityId);
    return (it != m_characters.end()) ? it->second.maxHp : 0;
}

void RestSystem::setCurrentHitDice(const std::string& entityId, int count) {
    auto it = m_characters.find(entityId);
    if (it == m_characters.end()) return;
    it->second.currentHitDice = std::clamp(count, 0, it->second.maxHitDice);
}

int RestSystem::getCurrentHitDice(const std::string& entityId) const {
    auto it = m_characters.find(entityId);
    return (it != m_characters.end()) ? it->second.currentHitDice : 0;
}

int RestSystem::getMaxHitDice(const std::string& entityId) const {
    auto it = m_characters.find(entityId);
    return (it != m_characters.end()) ? it->second.maxHitDice : 0;
}

// ---------------------------------------------------------------------------
// Short rest
// ---------------------------------------------------------------------------

ShortRestResult RestSystem::shortRest(
    const std::string& entityId,
    int hitDiceToSpend,
    int constitutionMod,
    DiceSystem& dice,
    SpellcasterComponent* caster)
{
    ShortRestResult result;

    auto it = m_characters.find(entityId);
    if (it == m_characters.end()) {
        result.failReason = "Character not registered";
        return result;
    }

    if (hitDiceToSpend < 0) {
        result.failReason = "hitDiceToSpend must be >= 0";
        return result;
    }

    auto& state = it->second;

    if (hitDiceToSpend > state.currentHitDice) {
        result.failReason = "Not enough hit dice";
        return result;
    }

    // Spend hit dice and roll HP recovery
    int totalHp = 0;
    for (int i = 0; i < hitDiceToSpend; ++i) {
        auto roll = dice.roll(state.hitDieType);
        totalHp += roll.total + constitutionMod;
    }
    // HP recovery is at least 0 (can't heal negative)
    totalHp = std::max(0, totalHp);

    // Apply recovery (clamped to max HP)
    int hpBefore = state.currentHp;
    state.currentHp = std::min(state.currentHp + totalHp, state.maxHp);
    state.currentHitDice -= hitDiceToSpend;

    // Warlock pact slots restore on short rest
    if (caster) {
        caster->onShortRest();
    }

    result.completed       = true;
    result.hpRecovered     = state.currentHp - hpBefore;
    result.hitDiceSpent    = hitDiceToSpend;
    result.hitDiceRemaining = state.currentHitDice;
    return result;
}

// ---------------------------------------------------------------------------
// Long rest
// ---------------------------------------------------------------------------

LongRestResult RestSystem::longRest(
    const std::string& entityId,
    SpellcasterComponent* caster)
{
    LongRestResult result;

    auto it = m_characters.find(entityId);
    if (it == m_characters.end()) {
        result.failReason = "Character not registered";
        return result;
    }

    auto& state = it->second;

    // Restore HP to maximum
    int hpBefore = state.currentHp;
    state.currentHp = state.maxHp;
    result.hpRecovered = state.currentHp - hpBefore;

    // Restore up to half of max hit dice (minimum 1)
    int restored = std::max(1, state.maxHitDice / 2);
    int missing  = state.maxHitDice - state.currentHitDice;
    int toRestore = std::min(restored, missing);
    state.currentHitDice += toRestore;
    result.hitDiceRestored = toRestore;

    // Restore spell slots
    if (caster) {
        caster->onLongRest();
        result.spellSlotsRestored = true;
    }

    result.completed = true;
    return result;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void RestSystem::removeCharacter(const std::string& entityId) {
    m_characters.erase(entityId);
}

void RestSystem::clear() {
    m_characters.clear();
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json RestSystem::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [id, s] : m_characters) {
        j[id] = {
            {"currentHp",       s.currentHp},
            {"maxHp",           s.maxHp},
            {"currentHitDice",  s.currentHitDice},
            {"maxHitDice",      s.maxHitDice},
            {"hitDieType",      static_cast<int>(s.hitDieType)},
            {"characterLevel",  s.characterLevel},
            {"castingType",     s.castingType}
        };
    }
    return j;
}

void RestSystem::fromJson(const nlohmann::json& j) {
    m_characters.clear();
    for (auto& [id, sj] : j.items()) {
        CharacterRestState s;
        s.currentHp      = sj.value("currentHp",      0);
        s.maxHp          = sj.value("maxHp",           0);
        s.currentHitDice = sj.value("currentHitDice",  0);
        s.maxHitDice     = sj.value("maxHitDice",      0);
        s.hitDieType     = static_cast<DieType>(sj.value("hitDieType", static_cast<int>(DieType::D8)));
        s.characterLevel = sj.value("characterLevel",  1);
        s.castingType    = sj.value("castingType",     "");
        m_characters[id] = s;
    }
}

} // namespace Phyxel::Core
