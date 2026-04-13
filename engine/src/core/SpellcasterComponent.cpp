#include "core/SpellcasterComponent.h"
#include "utils/Logger.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

// ============================================================================
// SpellSlots
// ============================================================================

bool SpellSlots::canSpend(int slotLevel) const {
    if (slotLevel < 1 || slotLevel > MAX_SPELL_LEVEL) return false;
    return remaining[slotLevel - 1] > 0;
}

bool SpellSlots::spend(int slotLevel) {
    if (!canSpend(slotLevel)) return false;
    --remaining[slotLevel - 1];
    return true;
}

bool SpellSlots::restore(int slotLevel, int count) {
    if (slotLevel < 1 || slotLevel > MAX_SPELL_LEVEL) return false;
    int idx = slotLevel - 1;
    remaining[idx] = std::min(maximum[idx], remaining[idx] + count);
    return true;
}

void SpellSlots::restoreAll() {
    for (int i = 0; i < MAX_SPELL_LEVEL; ++i)
        remaining[i] = maximum[i];
}

int SpellSlots::totalRemaining() const {
    int total = 0;
    for (int i = 0; i < MAX_SPELL_LEVEL; ++i) total += remaining[i];
    return total;
}

int SpellSlots::totalMaximum() const {
    int total = 0;
    for (int i = 0; i < MAX_SPELL_LEVEL; ++i) total += maximum[i];
    return total;
}

nlohmann::json SpellSlots::toJson() const {
    nlohmann::json j;
    j["maximum"]   = nlohmann::json::array();
    j["remaining"] = nlohmann::json::array();
    for (int i = 0; i < MAX_SPELL_LEVEL; ++i) {
        j["maximum"].push_back(maximum[i]);
        j["remaining"].push_back(remaining[i]);
    }
    return j;
}

void SpellSlots::fromJson(const nlohmann::json& j) {
    if (j.contains("maximum") && j["maximum"].is_array()) {
        for (int i = 0; i < MAX_SPELL_LEVEL && i < (int)j["maximum"].size(); ++i)
            maximum[i] = j["maximum"][i].get<int>();
    }
    if (j.contains("remaining") && j["remaining"].is_array()) {
        for (int i = 0; i < MAX_SPELL_LEVEL && i < (int)j["remaining"].size(); ++i)
            remaining[i] = j["remaining"][i].get<int>();
    }
}

// ============================================================================
// SpellSlotTable
// ============================================================================

// Full caster slot table [level-1][slotLevel-1] (PHB p.113, wizard table)
static const int FULL_CASTER_TABLE[20][9] = {
    {2,0,0,0,0,0,0,0,0},  // level  1
    {3,0,0,0,0,0,0,0,0},  // level  2
    {4,2,0,0,0,0,0,0,0},  // level  3
    {4,3,0,0,0,0,0,0,0},  // level  4
    {4,3,2,0,0,0,0,0,0},  // level  5
    {4,3,3,0,0,0,0,0,0},  // level  6
    {4,3,3,1,0,0,0,0,0},  // level  7
    {4,3,3,2,0,0,0,0,0},  // level  8
    {4,3,3,3,1,0,0,0,0},  // level  9
    {4,3,3,3,2,0,0,0,0},  // level 10
    {4,3,3,3,2,1,0,0,0},  // level 11
    {4,3,3,3,2,1,0,0,0},  // level 12
    {4,3,3,3,2,1,1,0,0},  // level 13
    {4,3,3,3,2,1,1,0,0},  // level 14
    {4,3,3,3,2,1,1,1,0},  // level 15
    {4,3,3,3,2,1,1,1,0},  // level 16
    {4,3,3,3,2,1,1,1,1},  // level 17
    {4,3,3,3,3,1,1,1,1},  // level 18
    {4,3,3,3,3,2,1,1,1},  // level 19
    {4,3,3,3,3,2,2,1,1},  // level 20
};

// Half caster uses full-caster table at half level (rounded down, min 1 for paladin/ranger at level 2+)
// Level 1 half casters have no slots.
static const int HALF_CASTER_TABLE[20][9] = {
    {0,0,0,0,0,0,0,0,0},  // level  1 — no slots
    {2,0,0,0,0,0,0,0,0},  // level  2
    {3,0,0,0,0,0,0,0,0},  // level  3
    {3,0,0,0,0,0,0,0,0},  // level  4
    {4,2,0,0,0,0,0,0,0},  // level  5
    {4,2,0,0,0,0,0,0,0},  // level  6
    {4,3,0,0,0,0,0,0,0},  // level  7
    {4,3,0,0,0,0,0,0,0},  // level  8
    {4,3,2,0,0,0,0,0,0},  // level  9
    {4,3,2,0,0,0,0,0,0},  // level 10
    {4,3,3,0,0,0,0,0,0},  // level 11
    {4,3,3,0,0,0,0,0,0},  // level 12
    {4,3,3,1,0,0,0,0,0},  // level 13
    {4,3,3,1,0,0,0,0,0},  // level 14
    {4,3,3,2,0,0,0,0,0},  // level 15
    {4,3,3,2,0,0,0,0,0},  // level 16
    {4,3,3,3,1,0,0,0,0},  // level 17
    {4,3,3,3,1,0,0,0,0},  // level 18
    {4,3,3,3,2,0,0,0,0},  // level 19
    {4,3,3,3,2,0,0,0,0},  // level 20
};

// Third caster (arcane trickster, eldritch knight) — no slots until level 3
static const int THIRD_CASTER_TABLE[20][9] = {
    {0,0,0,0,0,0,0,0,0},  // level  1
    {0,0,0,0,0,0,0,0,0},  // level  2
    {2,0,0,0,0,0,0,0,0},  // level  3
    {3,0,0,0,0,0,0,0,0},  // level  4
    {3,0,0,0,0,0,0,0,0},  // level  5
    {3,0,0,0,0,0,0,0,0},  // level  6
    {4,2,0,0,0,0,0,0,0},  // level  7
    {4,2,0,0,0,0,0,0,0},  // level  8
    {4,2,0,0,0,0,0,0,0},  // level  9
    {4,3,0,0,0,0,0,0,0},  // level 10
    {4,3,0,0,0,0,0,0,0},  // level 11
    {4,3,0,0,0,0,0,0,0},  // level 12
    {4,3,2,0,0,0,0,0,0},  // level 13
    {4,3,2,0,0,0,0,0,0},  // level 14
    {4,3,2,0,0,0,0,0,0},  // level 15
    {4,3,3,0,0,0,0,0,0},  // level 16
    {4,3,3,0,0,0,0,0,0},  // level 17
    {4,3,3,0,0,0,0,0,0},  // level 18
    {4,3,3,1,0,0,0,0,0},  // level 19
    {4,3,3,1,0,0,0,0,0},  // level 20
};

// Warlock pact slot level by warlock class level
static const int PACT_SLOT_LEVEL[20] = {
    1,1,2,2,3,3,4,4,5,5,5,5,5,5,5,5,5,5,5,5
};

// Warlock number of pact slots by class level
static const int PACT_SLOT_COUNT[20] = {
    1,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,4,4,4,4
};

int SpellSlotTable::pactSlotLevel(int warlockLevel) {
    if (warlockLevel < 1) return 1;
    if (warlockLevel > 20) warlockLevel = 20;
    return PACT_SLOT_LEVEL[warlockLevel - 1];
}

SpellSlots SpellSlotTable::forLevel(int characterLevel, const std::string& castingType) {
    SpellSlots slots;
    if (characterLevel < 1)  characterLevel = 1;
    if (characterLevel > 20) characterLevel = 20;
    int row = characterLevel - 1;

    if (castingType == "half") {
        for (int i = 0; i < SpellSlots::MAX_SPELL_LEVEL; ++i)
            slots.maximum[i] = HALF_CASTER_TABLE[row][i];
    } else if (castingType == "third") {
        for (int i = 0; i < SpellSlots::MAX_SPELL_LEVEL; ++i)
            slots.maximum[i] = THIRD_CASTER_TABLE[row][i];
    } else if (castingType == "pact") {
        // All pact slots go into the pact slot level bucket
        int pactLvl   = PACT_SLOT_LEVEL[row];
        int pactCount = PACT_SLOT_COUNT[row];
        slots.maximum[pactLvl - 1] = pactCount;
    } else {
        // Default: full caster
        for (int i = 0; i < SpellSlots::MAX_SPELL_LEVEL; ++i)
            slots.maximum[i] = FULL_CASTER_TABLE[row][i];
    }

    slots.restoreAll();
    return slots;
}

// ============================================================================
// ConcentrationState
// ============================================================================

void ConcentrationState::clear() {
    active = false;
    spellId.clear();
    targetEntityId.clear();
    remainingDuration = 0.0f;
}

nlohmann::json ConcentrationState::toJson() const {
    return {
        {"active",            active},
        {"spellId",           spellId},
        {"targetEntityId",    targetEntityId},
        {"remainingDuration", remainingDuration}
    };
}

void ConcentrationState::fromJson(const nlohmann::json& j) {
    active            = j.value("active",            false);
    spellId           = j.value("spellId",           "");
    targetEntityId    = j.value("targetEntityId",    "");
    remainingDuration = j.value("remainingDuration", 0.0f);
}

// ============================================================================
// SpellcasterComponent
// ============================================================================

SpellcasterComponent::SpellcasterComponent(std::string entityId)
    : m_entityId(std::move(entityId)) {}

void SpellcasterComponent::initialize(const std::string& castingClassId,
                                      AbilityType        spellcastingAbility,
                                      int                characterLevel,
                                      const std::string& castingType) {
    m_castingClassId      = castingClassId;
    m_spellcastingAbility = spellcastingAbility;
    m_characterLevel      = std::max(1, characterLevel);
    m_castingType         = castingType;
    m_slots               = SpellSlotTable::forLevel(m_characterLevel, castingType);
}

// -----------------------------------------------------------------------
// Derived stats
// -----------------------------------------------------------------------

int SpellcasterComponent::spellSaveDC(int proficiencyBonus,
                                       const CharacterAttributes& attrs) const {
    return 8 + proficiencyBonus + attrs.modifier(m_spellcastingAbility);
}

int SpellcasterComponent::spellAttackBonus(int proficiencyBonus,
                                            const CharacterAttributes& attrs) const {
    return proficiencyBonus + attrs.modifier(m_spellcastingAbility);
}

// -----------------------------------------------------------------------
// Cantrips
// -----------------------------------------------------------------------

bool SpellcasterComponent::learnCantrip(const std::string& spellId) {
    if (knowsCantrip(spellId)) return false;
    m_cantrips.push_back(spellId);
    return true;
}

bool SpellcasterComponent::knowsCantrip(const std::string& spellId) const {
    return std::find(m_cantrips.begin(), m_cantrips.end(), spellId) != m_cantrips.end();
}

bool SpellcasterComponent::forgetCantrip(const std::string& spellId) {
    auto it = std::find(m_cantrips.begin(), m_cantrips.end(), spellId);
    if (it == m_cantrips.end()) return false;
    m_cantrips.erase(it);
    return true;
}

// -----------------------------------------------------------------------
// Known / prepared spells
// -----------------------------------------------------------------------

KnownSpell* SpellcasterComponent::findKnown(const std::string& spellId) {
    for (auto& ks : m_knownSpells)
        if (ks.spellId == spellId) return &ks;
    return nullptr;
}

const KnownSpell* SpellcasterComponent::findKnown(const std::string& spellId) const {
    for (const auto& ks : m_knownSpells)
        if (ks.spellId == spellId) return &ks;
    return nullptr;
}

bool SpellcasterComponent::learnSpell(const std::string& spellId, bool prepared) {
    if (knowsSpell(spellId)) return false;
    m_knownSpells.push_back({spellId, prepared});
    return true;
}

bool SpellcasterComponent::forgetSpell(const std::string& spellId) {
    auto it = std::find_if(m_knownSpells.begin(), m_knownSpells.end(),
        [&](const KnownSpell& ks) { return ks.spellId == spellId; });
    if (it == m_knownSpells.end()) return false;
    m_knownSpells.erase(it);
    return true;
}

bool SpellcasterComponent::prepareSpell(const std::string& spellId) {
    auto* ks = findKnown(spellId);
    if (!ks) return false;
    ks->prepared = true;
    return true;
}

bool SpellcasterComponent::unprepareSpell(const std::string& spellId) {
    auto* ks = findKnown(spellId);
    if (!ks || !ks->prepared) return false;
    ks->prepared = false;
    return true;
}

bool SpellcasterComponent::knowsSpell(const std::string& spellId) const {
    return findKnown(spellId) != nullptr;
}

bool SpellcasterComponent::hasPrepared(const std::string& spellId) const {
    const auto* ks = findKnown(spellId);
    return ks && ks->prepared;
}

// -----------------------------------------------------------------------
// Slot management
// -----------------------------------------------------------------------

bool SpellcasterComponent::canCast(const std::string& spellId, int slotLevel) const {
    if (knowsCantrip(spellId)) return true;  // cantrips always available
    if (!hasPrepared(spellId)) return false;
    return m_slots.canSpend(slotLevel);
}

bool SpellcasterComponent::spendSlot(int slotLevel) {
    return m_slots.spend(slotLevel);
}

// -----------------------------------------------------------------------
// Concentration
// -----------------------------------------------------------------------

bool SpellcasterComponent::startConcentration(const std::string& spellId,
                                               const std::string& targetEntityId,
                                               float              durationSeconds) {
    // Break existing concentration first
    if (m_concentration.active) breakConcentration();
    m_concentration.active            = true;
    m_concentration.spellId           = spellId;
    m_concentration.targetEntityId    = targetEntityId;
    m_concentration.remainingDuration = durationSeconds;
    return true;
}

void SpellcasterComponent::breakConcentration() {
    m_concentration.clear();
}

void SpellcasterComponent::updateConcentration(float deltaTime) {
    if (!m_concentration.active) return;
    if (m_concentration.isPermanent()) return;
    m_concentration.remainingDuration -= deltaTime;
    if (m_concentration.remainingDuration <= 0.0f)
        breakConcentration();
}

// -----------------------------------------------------------------------
// Rests
// -----------------------------------------------------------------------

void SpellcasterComponent::onShortRest(const std::string& castingType) {
    // Warlocks restore all pact slots on a short rest
    if (castingType == "pact" || m_castingType == "pact") {
        m_slots.restoreAll();
    }
}

void SpellcasterComponent::onLongRest() {
    m_slots.restoreAll();
    // Concentration spells end on a long rest
    breakConcentration();
}

// -----------------------------------------------------------------------
// Serialization
// -----------------------------------------------------------------------

nlohmann::json SpellcasterComponent::toJson() const {
    nlohmann::json j;
    j["entityId"]           = m_entityId;
    j["castingClassId"]     = m_castingClassId;
    j["spellcastingAbility"]= abilityShortName(m_spellcastingAbility);
    j["characterLevel"]     = m_characterLevel;
    j["castingType"]        = m_castingType;
    j["slots"]              = m_slots.toJson();
    j["cantrips"]           = m_cantrips;

    nlohmann::json knownArr = nlohmann::json::array();
    for (const auto& ks : m_knownSpells)
        knownArr.push_back({{"spellId", ks.spellId}, {"prepared", ks.prepared}});
    j["knownSpells"]   = knownArr;
    j["concentration"] = m_concentration.toJson();
    return j;
}

void SpellcasterComponent::fromJson(const nlohmann::json& j) {
    m_entityId       = j.value("entityId",       "");
    m_castingClassId = j.value("castingClassId", "");
    m_characterLevel = j.value("characterLevel", 1);
    m_castingType    = j.value("castingType",    "full");

    std::string ab = j.value("spellcastingAbility", "INT");
    try { m_spellcastingAbility = abilityFromString(ab.c_str()); }
    catch (...) { m_spellcastingAbility = AbilityType::Intelligence; }

    if (j.contains("slots")) m_slots.fromJson(j["slots"]);
    m_cantrips.clear();
    if (j.contains("cantrips") && j["cantrips"].is_array()) {
        for (const auto& c : j["cantrips"])
            if (c.is_string()) m_cantrips.push_back(c.get<std::string>());
    }
    m_knownSpells.clear();
    if (j.contains("knownSpells") && j["knownSpells"].is_array()) {
        for (const auto& ks : j["knownSpells"]) {
            KnownSpell entry;
            entry.spellId  = ks.value("spellId",  "");
            entry.prepared = ks.value("prepared", false);
            if (!entry.spellId.empty()) m_knownSpells.push_back(entry);
        }
    }
    if (j.contains("concentration")) m_concentration.fromJson(j["concentration"]);
}

} // namespace Core
} // namespace Phyxel
