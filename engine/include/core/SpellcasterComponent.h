#pragma once

#include "core/SpellDefinition.h"
#include "core/CharacterAttributes.h"

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// SpellSlots — tracks maximum and remaining slots per level (1–9)
// ============================================================================
struct SpellSlots {
    static constexpr int MAX_SPELL_LEVEL = 9;

    int maximum[MAX_SPELL_LEVEL]   = {};
    int remaining[MAX_SPELL_LEVEL] = {};

    /// slotLevel is 1-indexed (1–9). Returns false if none available.
    bool canSpend(int slotLevel) const;
    bool spend(int slotLevel);
    bool restore(int slotLevel, int count = 1);
    void restoreAll();

    int totalRemaining() const;
    int totalMaximum()   const;

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

// ============================================================================
// SpellSlotTable — static lookup for standard 5e slot tables
// ============================================================================
struct SpellSlotTable {
    /// Returns SpellSlots configured for the given character level and casting type.
    /// castingType: "full", "half", "third", "pact"
    /// For "pact" (Warlock), all slots are at pact slot level (all in slot[pactLevel-1]).
    static SpellSlots forLevel(int characterLevel, const std::string& castingType);

    /// Warlock pact slot level (1–5) for a given warlock class level.
    static int pactSlotLevel(int warlockLevel);
};

// ============================================================================
// ConcentrationState
// ============================================================================
struct ConcentrationState {
    bool        active            = false;
    std::string spellId;
    std::string targetEntityId;
    float       remainingDuration = 0.0f;  // seconds; <0 = indefinite

    bool isPermanent() const { return remainingDuration < 0.0f; }
    void clear();

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

// ============================================================================
// KnownSpell
// ============================================================================
struct KnownSpell {
    std::string spellId;
    bool        prepared = false;  // wizards/clerics prepare; sorcerers/warlocks auto-prepared
};

// ============================================================================
// SpellcasterComponent — per-entity spellcasting state
// ============================================================================
class SpellcasterComponent {
public:
    explicit SpellcasterComponent(std::string entityId = "");

    /// Configure from class definition. castingType: "full"|"half"|"third"|"pact"
    void initialize(const std::string& castingClassId,
                    AbilityType        spellcastingAbility,
                    int                characterLevel,
                    const std::string& castingType = "full");

    // -----------------------------------------------------------------------
    // Derived stats
    // -----------------------------------------------------------------------

    /// 8 + proficiencyBonus + spellcasting ability modifier.
    int spellSaveDC(int proficiencyBonus, const CharacterAttributes& attrs) const;

    /// proficiencyBonus + spellcasting ability modifier.
    int spellAttackBonus(int proficiencyBonus, const CharacterAttributes& attrs) const;

    // -----------------------------------------------------------------------
    // Cantrips
    // -----------------------------------------------------------------------

    bool learnCantrip(const std::string& spellId);
    bool knowsCantrip(const std::string& spellId) const;
    bool forgetCantrip(const std::string& spellId);

    // -----------------------------------------------------------------------
    // Known / prepared leveled spells
    // -----------------------------------------------------------------------

    /// Add a spell to the known list. prepared=true marks it as prepared immediately.
    bool learnSpell(const std::string& spellId, bool prepared = false);

    bool forgetSpell(const std::string& spellId);

    /// Mark an already-known spell as prepared.
    bool prepareSpell(const std::string& spellId);
    bool unprepareSpell(const std::string& spellId);

    bool knowsSpell(const std::string& spellId) const;
    bool hasPrepared(const std::string& spellId) const;

    // -----------------------------------------------------------------------
    // Slot management
    // -----------------------------------------------------------------------

    /// True if the spell is known/prepared AND a slot of slotLevel is available.
    /// Cantrips require no slot (slotLevel ignored).
    bool canCast(const std::string& spellId, int slotLevel = 0) const;

    /// Spend a slot. Returns false if none remaining at that level.
    bool spendSlot(int slotLevel);

    const SpellSlots& slots() const { return m_slots; }
    SpellSlots&       slots()       { return m_slots; }

    // -----------------------------------------------------------------------
    // Concentration
    // -----------------------------------------------------------------------

    /// Begin concentrating on a spell. Breaks any existing concentration first.
    /// durationSeconds < 0 means indefinite.
    bool startConcentration(const std::string& spellId,
                            const std::string& targetEntityId,
                            float              durationSeconds);

    void breakConcentration();
    bool isConcentrating() const { return m_concentration.active; }

    const ConcentrationState& concentration() const { return m_concentration; }

    /// Tick concentration timer; breaks it when duration expires.
    void updateConcentration(float deltaTime);

    // -----------------------------------------------------------------------
    // Rests
    // -----------------------------------------------------------------------

    /// Restores pact slots (warlocks); does nothing for other casting types.
    void onShortRest(const std::string& castingType = "full");

    /// Restores all spell slots.
    void onLongRest();

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    const std::string&            entityId()           const { return m_entityId; }
    const std::string&            castingClassId()     const { return m_castingClassId; }
    AbilityType                   spellcastingAbility()const { return m_spellcastingAbility; }
    int                           characterLevel()     const { return m_characterLevel; }
    const std::vector<std::string>& cantrips()         const { return m_cantrips; }
    const std::vector<KnownSpell>&  knownSpells()      const { return m_knownSpells; }

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::string m_entityId;
    std::string m_castingClassId;
    AbilityType m_spellcastingAbility = AbilityType::Intelligence;
    int         m_characterLevel      = 1;
    std::string m_castingType         = "full";

    SpellSlots               m_slots;
    std::vector<std::string> m_cantrips;
    std::vector<KnownSpell>  m_knownSpells;
    ConcentrationState       m_concentration;

    KnownSpell* findKnown(const std::string& spellId);
    const KnownSpell* findKnown(const std::string& spellId) const;
};

} // namespace Core
} // namespace Phyxel
