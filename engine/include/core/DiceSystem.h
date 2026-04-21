#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Phyxel {
namespace Core {

enum class DieType { D4 = 4, D6 = 6, D8 = 8, D10 = 10, D12 = 12, D20 = 20, D100 = 100 };

/// Result of a single dice roll, including full context for display and logging.
struct RollResult {
    int total = 0;
    std::vector<int> dice;    // individual die results before modifier
    int modifier = 0;
    bool isCriticalSuccess = false;  // natural 20 on a d20
    bool isCriticalFailure = false;  // natural 1 on a d20
    bool hadAdvantage = false;
    bool hadDisadvantage = false;
    // On advantage/disadvantage: the roll that was NOT used
    int droppedRoll = 0;

    std::string describe() const;  // e.g. "[14]+3 = 17"
};

/// A parsed dice expression: NdX+M (e.g. "2d6+3", "1d20", "d8-1").
struct DiceExpression {
    int count = 1;
    DieType die = DieType::D6;
    int modifier = 0;

    std::string toString() const;  // e.g. "2d6+3"
    static DiceExpression parse(const std::string& expr);  // parse "2d6+3"
};

/// Core dice roller. All randomness in D&D RPG systems goes through here.
///
/// Usage:
///   auto result = DiceSystem::roll(DieType::D20, 5);
///   auto expr   = DiceSystem::rollExpression("2d6+3");
///   bool hit    = DiceSystem::checkDC(result.total, 15);
class DiceSystem {
public:
    // --- Basic rolls ---

    /// Roll a single die and add modifier. Tracks nat-20/nat-1 for d20.
    static RollResult roll(DieType die, int modifier = 0);

    /// Roll with advantage: roll twice, take the higher result.
    static RollResult rollAdvantage(DieType die, int modifier = 0);

    /// Roll with disadvantage: roll twice, take the lower result.
    static RollResult rollDisadvantage(DieType die, int modifier = 0);

    // --- Expression rolls ---

    /// Roll a pre-parsed dice expression (e.g. from a spell definition).
    static RollResult rollExpression(const DiceExpression& expr);

    /// Parse and roll a dice expression string (e.g. "2d6+3", "d20", "1d8-1").
    static RollResult rollExpression(const std::string& expr);

    /// Roll a dice expression with advantage applied to each die independently.
    static RollResult rollExpressionAdvantage(const DiceExpression& expr);

    /// Roll a dice expression with disadvantage applied to each die independently.
    static RollResult rollExpressionDisadvantage(const DiceExpression& expr);

    // --- Critical hit support ---

    /// Double the dice count for critical hits (nat-20 melee attacks).
    /// e.g. "2d6+3" becomes "4d6+3".
    static RollResult rollCritical(const DiceExpression& baseDamage);

    // --- DC checks ---

    /// Returns true if roll meets or beats the DC.
    static bool checkDC(int rollTotal, int dc);

    // --- Average / expected value ---

    /// Average (expected) value of an expression, used for encounter balancing.
    static float averageValue(const DiceExpression& expr);
    static float averageValue(const std::string& expr);

    // --- Uniform float ---

    /// Return a uniform float in [0, 1). Shares the same seeded RNG.
    static float rollFloat();

    // --- Seeding (for deterministic tests / replays) ---

    /// Set a fixed seed. Call with 0 to restore random seeding.
    static void setSeed(uint64_t seed);

    /// Returns true if seeded (deterministic mode).
    static bool isSeeded();

private:
    static int rollOneDie(DieType die);
};

} // namespace Core
} // namespace Phyxel
