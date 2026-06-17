#pragma once

#include "core/InitiativeTracker.h"
#include "core/DiceSystem.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Which combat ruleset the game runs. Locked per-game (read once from
/// game.json `combat.mode`); the engine does not switch mid-session.
///   - RealTime : the souls-style FSM combat (CombatBehavior / CombatSystem).
///   - TurnBased: D&D-style initiative + action economy (InitiativeTracker).
enum class CombatMode { RealTime, TurnBased };

CombatMode  combatModeFromString(const std::string& s);  ///< "turn_based" → TurnBased, else RealTime
const char* combatModeToString(CombatMode m);

/// CombatDirector — the single source of truth for "are we in combat, whose
/// turn is it, and what mode are we playing." Every other system (UI, AI,
/// input) queries the director rather than holding its own combat flags.
///
/// It owns the InitiativeTracker and the participant→side mapping, and runs the
/// encounter lifecycle. In TurnBased mode it drives initiative + rounds; in
/// RealTime mode it only tracks the in-combat flag and combatant sides (turn
/// queries are inert). It stays headless — it operates on entity-ID strings and
/// has no scene/render dependency, so it remains unit-testable like the systems
/// it wraps. The host decides who lives/dies and feeds that back via
/// removeCombatant(); the director reacts (auto-ending when a side is wiped).
class CombatDirector {
public:
    /// One creature entering an encounter.
    struct Combatant {
        std::string entityId;
        bool isPlayerSide   = false;  ///< player + allies vs. enemies
        int  initiativeBonus = 0;     ///< DEX mod etc., used when auto-rolling initiative
        int  speed          = 30;     ///< movement in feet (per turn)
    };

    // -----------------------------------------------------------------------
    // Mode (set once from game.json; default RealTime so existing games are inert)
    // -----------------------------------------------------------------------
    void       setMode(CombatMode m) { m_mode = m; }
    CombatMode mode() const          { return m_mode; }
    bool       isTurnBased() const   { return m_mode == CombatMode::TurnBased; }

    // -----------------------------------------------------------------------
    // Encounter lifecycle
    // -----------------------------------------------------------------------

    /// Begin an encounter. In TurnBased mode this rolls initiative (1d20 +
    /// each combatant's bonus) and sorts the turn order; in RealTime mode it
    /// just marks combat active and records sides. No-op if already in combat.
    void beginEncounter(const std::vector<Combatant>& combatants, DiceSystem& dice);

    /// Convenience overload using an internal DiceSystem.
    void beginEncounter(const std::vector<Combatant>& combatants);

    /// End the encounter and clear all combat state.
    void endEncounter();

    bool inCombat() const { return m_inCombat; }

    // -----------------------------------------------------------------------
    // Turn flow (TurnBased only — inert in RealTime)
    // -----------------------------------------------------------------------

    /// Advance to the next participant's turn. Returns the new current entity
    /// id (empty in RealTime or when not in combat).
    const std::string& advanceTurn();

    const std::string& currentEntityId() const;
    int                currentRound() const;
    bool               isEntityTurn(const std::string& entityId) const;

    /// True when it is currently a player-side combatant's turn.
    bool isPlayerTurn() const;

    // -----------------------------------------------------------------------
    // Sides & survival
    // -----------------------------------------------------------------------

    bool isPlayerSide(const std::string& entityId) const;

    /// Remove a combatant (death / fled). Auto-ends the encounter if this
    /// leaves one side with no combatants.
    void removeCombatant(const std::string& entityId);

    bool playerSideWiped() const;  ///< true once every player-side combatant is gone
    bool enemySideWiped() const;   ///< true once every enemy-side combatant is gone

    int playerSideCount() const;
    int enemySideCount() const;

    // -----------------------------------------------------------------------
    // Direct access for systems that still consume the tracker
    // (CombatAISystem, renderCombatHUD). Prefer the director's own queries.
    // -----------------------------------------------------------------------
    InitiativeTracker&       initiative()       { return m_tracker; }
    const InitiativeTracker& initiative() const { return m_tracker; }

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    CombatMode        m_mode     = CombatMode::RealTime;
    InitiativeTracker m_tracker;
    bool              m_inCombat = false;

    // entityId → isPlayerSide. Kept independent of the tracker so RealTime
    // mode (which never populates the tracker) still answers side queries.
    std::unordered_map<std::string, bool> m_side;

    static const std::string s_empty;
};

} // namespace Core
} // namespace Phyxel
