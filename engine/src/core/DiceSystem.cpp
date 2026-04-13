#include "core/DiceSystem.h"

#include <random>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// Internal RNG
// ---------------------------------------------------------------------------

namespace {

struct RNG {
    std::mt19937 engine;
    bool seeded = false;

    RNG() {
        std::random_device rd;
        engine.seed(rd());
    }

    int rollRange(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(engine);
    }

    float rollFloat() {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(engine);
    }
};

static RNG s_rng;

} // anonymous namespace

// ---------------------------------------------------------------------------
// DiceExpression
// ---------------------------------------------------------------------------

std::string DiceExpression::toString() const {
    std::string s = std::to_string(count) + "d" + std::to_string(static_cast<int>(die));
    if (modifier > 0) s += "+" + std::to_string(modifier);
    else if (modifier < 0) s += std::to_string(modifier);
    return s;
}

DiceExpression DiceExpression::parse(const std::string& raw) {
    DiceExpression expr;

    // Normalize: strip spaces, lowercase
    std::string s;
    for (char c : raw) {
        if (c != ' ') s += static_cast<char>(std::tolower(c));
    }

    // Find 'd'
    size_t dPos = s.find('d');
    if (dPos == std::string::npos) {
        // Pure constant like "5"
        expr.count = 0;
        expr.die = DieType::D6;
        expr.modifier = std::stoi(s);
        return expr;
    }

    // Count (before 'd')
    if (dPos == 0) {
        expr.count = 1;
    } else {
        expr.count = std::stoi(s.substr(0, dPos));
    }

    // Find modifier sign after the die number
    std::string afterD = s.substr(dPos + 1);
    size_t signPos = afterD.find_first_of("+-", 1); // skip leading '-' that's part of die number

    std::string dieStr;
    if (signPos == std::string::npos) {
        dieStr = afterD;
        expr.modifier = 0;
    } else {
        dieStr = afterD.substr(0, signPos);
        expr.modifier = std::stoi(afterD.substr(signPos));
    }

    int dieVal = std::stoi(dieStr);
    switch (dieVal) {
        case 4:   expr.die = DieType::D4;   break;
        case 6:   expr.die = DieType::D6;   break;
        case 8:   expr.die = DieType::D8;   break;
        case 10:  expr.die = DieType::D10;  break;
        case 12:  expr.die = DieType::D12;  break;
        case 20:  expr.die = DieType::D20;  break;
        case 100: expr.die = DieType::D100; break;
        default:
            throw std::invalid_argument("Unknown die type d" + dieStr);
    }

    return expr;
}

// ---------------------------------------------------------------------------
// RollResult
// ---------------------------------------------------------------------------

std::string RollResult::describe() const {
    std::ostringstream ss;
    if (dice.size() == 1) {
        ss << "[" << dice[0] << "]";
    } else {
        ss << "[";
        for (size_t i = 0; i < dice.size(); ++i) {
            if (i) ss << "+";
            ss << dice[i];
        }
        ss << "]";
    }
    if (modifier > 0) ss << "+" << modifier;
    else if (modifier < 0) ss << modifier;
    ss << " = " << total;

    if (hadAdvantage) ss << " (adv, dropped " << droppedRoll << ")";
    else if (hadDisadvantage) ss << " (disadv, dropped " << droppedRoll << ")";
    if (isCriticalSuccess) ss << " CRIT!";
    if (isCriticalFailure) ss << " FUMBLE";
    return ss.str();
}

// ---------------------------------------------------------------------------
// DiceSystem internal helper
// ---------------------------------------------------------------------------

int DiceSystem::rollOneDie(DieType die) {
    int faces = static_cast<int>(die);
    return s_rng.rollRange(1, faces);
}

// ---------------------------------------------------------------------------
// DiceSystem public API
// ---------------------------------------------------------------------------

RollResult DiceSystem::roll(DieType die, int modifier) {
    RollResult r;
    r.modifier = modifier;
    int v = rollOneDie(die);
    r.dice.push_back(v);
    r.total = v + modifier;

    if (die == DieType::D20) {
        r.isCriticalSuccess = (v == 20);
        r.isCriticalFailure = (v == 1);
    }
    return r;
}

RollResult DiceSystem::rollAdvantage(DieType die, int modifier) {
    int a = rollOneDie(die);
    int b = rollOneDie(die);
    int best = std::max(a, b);
    int dropped = std::min(a, b);

    RollResult r;
    r.modifier = modifier;
    r.dice.push_back(best);
    r.total = best + modifier;
    r.hadAdvantage = true;
    r.droppedRoll = dropped;
    if (die == DieType::D20) {
        r.isCriticalSuccess = (best == 20);
        r.isCriticalFailure = (best == 1);
    }
    return r;
}

RollResult DiceSystem::rollDisadvantage(DieType die, int modifier) {
    int a = rollOneDie(die);
    int b = rollOneDie(die);
    int worst = std::min(a, b);
    int dropped = std::max(a, b);

    RollResult r;
    r.modifier = modifier;
    r.dice.push_back(worst);
    r.total = worst + modifier;
    r.hadDisadvantage = true;
    r.droppedRoll = dropped;
    if (die == DieType::D20) {
        r.isCriticalSuccess = (worst == 20);
        r.isCriticalFailure = (worst == 1);
    }
    return r;
}

RollResult DiceSystem::rollExpression(const DiceExpression& expr) {
    RollResult r;
    r.modifier = expr.modifier;

    int sum = 0;
    for (int i = 0; i < expr.count; ++i) {
        int v = rollOneDie(expr.die);
        r.dice.push_back(v);
        sum += v;
    }
    r.total = sum + expr.modifier;
    return r;
}

RollResult DiceSystem::rollExpression(const std::string& expr) {
    return rollExpression(DiceExpression::parse(expr));
}

RollResult DiceSystem::rollExpressionAdvantage(const DiceExpression& expr) {
    // Advantage on each individual die
    RollResult r;
    r.modifier = expr.modifier;
    r.hadAdvantage = true;

    int sum = 0;
    for (int i = 0; i < expr.count; ++i) {
        int a = rollOneDie(expr.die);
        int b = rollOneDie(expr.die);
        int best = std::max(a, b);
        r.dice.push_back(best);
        sum += best;
    }
    r.total = sum + expr.modifier;
    return r;
}

RollResult DiceSystem::rollExpressionDisadvantage(const DiceExpression& expr) {
    RollResult r;
    r.modifier = expr.modifier;
    r.hadDisadvantage = true;

    int sum = 0;
    for (int i = 0; i < expr.count; ++i) {
        int a = rollOneDie(expr.die);
        int b = rollOneDie(expr.die);
        int worst = std::min(a, b);
        r.dice.push_back(worst);
        sum += worst;
    }
    r.total = sum + expr.modifier;
    return r;
}

RollResult DiceSystem::rollCritical(const DiceExpression& baseDamage) {
    // Critical hit: double the dice, keep modifier once
    DiceExpression doubled = baseDamage;
    doubled.count *= 2;
    RollResult r = rollExpression(doubled);
    // Re-apply modifier correctly (it was already in doubled, but we doubled count not modifier)
    // rollExpression adds modifier once — doubled.modifier == baseDamage.modifier, so correct.
    return r;
}

bool DiceSystem::checkDC(int rollTotal, int dc) {
    return rollTotal >= dc;
}

float DiceSystem::averageValue(const DiceExpression& expr) {
    float dieFaces = static_cast<float>(static_cast<int>(expr.die));
    float avgPerDie = (1.0f + dieFaces) / 2.0f;
    return expr.count * avgPerDie + static_cast<float>(expr.modifier);
}

float DiceSystem::averageValue(const std::string& expr) {
    return averageValue(DiceExpression::parse(expr));
}

float DiceSystem::rollFloat() {
    return s_rng.rollFloat();
}

void DiceSystem::setSeed(uint64_t seed) {
    if (seed == 0) {
        std::random_device rd;
        s_rng.engine.seed(rd());
        s_rng.seeded = false;
    } else {
        s_rng.engine.seed(seed);
        s_rng.seeded = true;
    }
}

bool DiceSystem::isSeeded() {
    return s_rng.seeded;
}

} // namespace Core
} // namespace Phyxel
