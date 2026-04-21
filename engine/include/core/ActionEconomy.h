#pragma once

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Per-turn action budget for one combat participant.
///
/// D&D 5e grants each creature on its turn:
///   - 1 Action        (Attack, Cast, Dash, Disengage, Dodge, Help, Hide, Ready, Use)
///   - 1 Bonus Action  (class features, off-hand attack, some spells)
///   - 1 Reaction      (once per round, regardless of turn order)
///   - Movement        (speed in feet; can be spent in pieces)
///   - 1 Free Object   (draw/sheathe weapon, open unlocked door, pick up item, etc.)
///
/// Reactions recharge at the start of the creature's next turn.
struct ActionBudget {
    bool action              = true;
    bool bonusAction         = true;
    bool reaction            = true;
    int  movementRemaining   = 30;   // feet (set from character speed)
    bool freeObjectInteraction = true;

    // --- Spend ---
    bool spendAction()       { if (!action)      return false; action      = false; return true; }
    bool spendBonusAction()  { if (!bonusAction) return false; bonusAction = false; return true; }
    bool spendReaction()     { if (!reaction)    return false; reaction    = false; return true; }
    bool spendFreeObject()   { if (!freeObjectInteraction) return false; freeObjectInteraction = false; return true; }

    /// Spend up to `feet` of movement. Returns actual amount spent (0 if none remaining).
    int spendMovement(int feet) {
        int spent = (feet <= movementRemaining) ? feet : movementRemaining;
        movementRemaining -= spent;
        return spent;
    }

    // --- Queries ---
    bool canAct()           const { return action; }
    bool canBonusAct()      const { return bonusAction; }
    bool canReact()         const { return reaction; }
    bool canMove()          const { return movementRemaining > 0; }
    bool canFreeInteract()  const { return freeObjectInteraction; }

    /// True when the creature has spent all resources this turn.
    bool isExhausted() const {
        return !action && !bonusAction && movementRemaining == 0;
    }

    // --- Reset (called at start of creature's turn) ---
    void reset(int speed = 30) {
        action               = true;
        bonusAction          = true;
        reaction             = true;
        movementRemaining    = speed;
        freeObjectInteraction = true;
    }

    // --- Dash: double movement for the turn (costs an action or BA depending on class) ---
    void applyDash(int speed) { movementRemaining += speed; }

    // --- Dodge: grants advantage to attackers this turn (tracked externally) ---
    // --- Disengage: movement doesn't provoke OA this turn (tracked externally) ---
    // These are state flags set by game logic; budget just tracks resource spending.

    nlohmann::json toJson() const {
        return {
            {"action",              action},
            {"bonusAction",         bonusAction},
            {"reaction",            reaction},
            {"movementRemaining",   movementRemaining},
            {"freeObjectInteraction", freeObjectInteraction}
        };
    }

    void fromJson(const nlohmann::json& j) {
        if (j.contains("action"))              action              = j["action"].get<bool>();
        if (j.contains("bonusAction"))         bonusAction         = j["bonusAction"].get<bool>();
        if (j.contains("reaction"))            reaction            = j["reaction"].get<bool>();
        if (j.contains("movementRemaining"))   movementRemaining   = j["movementRemaining"].get<int>();
        if (j.contains("freeObjectInteraction")) freeObjectInteraction = j["freeObjectInteraction"].get<bool>();
    }
};

} // namespace Core
} // namespace Phyxel
