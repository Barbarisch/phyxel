#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// Currency — a wallet holding 5e coin denominations
//
// Exchange rates (PHB p.143):
//   1 pp = 10 gp = 20 ep = 100 sp = 1000 cp
//   1 gp =  2 ep = 10 sp = 100 cp
//   1 ep =         5 sp =  50 cp
//   1 sp =               = 10 cp
// ============================================================================
struct Currency {
    int copper   = 0;
    int silver   = 0;
    int electrum = 0;
    int gold     = 0;
    int platinum = 0;

    static constexpr int CP_PER_SP = 10;
    static constexpr int CP_PER_EP = 50;
    static constexpr int CP_PER_GP = 100;
    static constexpr int CP_PER_PP = 1000;

    // -----------------------------------------------------------------------
    // Value
    // -----------------------------------------------------------------------

    /// Total value expressed in copper pieces.
    int totalInCopper() const;

    // -----------------------------------------------------------------------
    // Spend / add
    // -----------------------------------------------------------------------

    /// True if the wallet holds at least copperCost worth of coins.
    bool canAfford(int copperCost) const;

    /// Deduct copperCost from the wallet, spending largest coins first.
    /// Returns false (no change) if insufficient funds.
    bool spend(int copperCost);

    /// Add a flat copper amount, breaking into optimal denominations.
    void add(int copperAmount);

    /// Add explicit coin counts.
    void addCoins(int cp = 0, int sp = 0, int ep = 0, int gp = 0, int pp = 0);

    // -----------------------------------------------------------------------
    // Formatting
    // -----------------------------------------------------------------------

    /// Human-readable, e.g. "15 gp, 3 sp, 2 cp" (omits zeroes).
    std::string toString() const;

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

// ============================================================================
// CurrencySystem — static helpers for currency math
// ============================================================================
class CurrencySystem {
public:
    /// Total value in copper for the given coin mix.
    static int toCopper(int cp, int sp, int ep, int gp, int pp);

    /// Convert a copper amount to optimal denomination Currency (fewest coins).
    static Currency fromCopper(int totalCp);

    /// Format a copper amount as a readable string, e.g. "15 gp, 3 sp".
    static std::string formatValue(int copperCost);

    /// Price in copper for standard item rarities (PHB guidelines).
    static int rarityBaseValueCp(int rarityIndex);  // 0=Common … 5=Artifact
};

} // namespace Core
} // namespace Phyxel
