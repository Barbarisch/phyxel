#include "core/CurrencySystem.h"

#include <algorithm>
#include <sstream>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// Currency — value
// ---------------------------------------------------------------------------

int Currency::totalInCopper() const {
    return copper
         + silver   * CP_PER_SP
         + electrum * CP_PER_EP
         + gold     * CP_PER_GP
         + platinum * CP_PER_PP;
}

// ---------------------------------------------------------------------------
// Currency — spend / add
// ---------------------------------------------------------------------------

bool Currency::canAfford(int copperCost) const {
    return totalInCopper() >= copperCost;
}

bool Currency::spend(int copperCost) {
    if (copperCost <= 0) return true;
    if (!canAfford(copperCost)) return false;

    int remaining = copperCost;

    // Deduct from smallest denomination upwards, then break larger coins if needed.
    // Strategy: convert everything to copper, subtract, convert back.
    int total = totalInCopper() - remaining;
    copper   = 0;
    silver   = 0;
    electrum = 0;
    gold     = 0;
    platinum = 0;
    add(total);
    return true;
}

void Currency::add(int copperAmount) {
    if (copperAmount <= 0) return;
    // Convert to optimal mix: pp → gp → sp → cp (skip electrum for simplicity)
    platinum += copperAmount / CP_PER_PP; copperAmount %= CP_PER_PP;
    gold     += copperAmount / CP_PER_GP; copperAmount %= CP_PER_GP;
    silver   += copperAmount / CP_PER_SP; copperAmount %= CP_PER_SP;
    copper   += copperAmount;
}

void Currency::addCoins(int cp, int sp, int ep, int gp, int pp) {
    copper   += cp;
    silver   += sp;
    electrum += ep;
    gold     += gp;
    platinum += pp;
    // Ensure no negative balances (caller's responsibility, but clamp)
    copper   = std::max(0, copper);
    silver   = std::max(0, silver);
    electrum = std::max(0, electrum);
    gold     = std::max(0, gold);
    platinum = std::max(0, platinum);
}

// ---------------------------------------------------------------------------
// Currency — formatting
// ---------------------------------------------------------------------------

std::string Currency::toString() const {
    std::ostringstream oss;
    bool any = false;
    auto append = [&](int val, const char* label) {
        if (val == 0) return;
        if (any) oss << ", ";
        oss << val << ' ' << label;
        any = true;
    };
    append(platinum, "pp");
    append(gold,     "gp");
    append(electrum, "ep");
    append(silver,   "sp");
    append(copper,   "cp");
    return any ? oss.str() : "0 cp";
}

// ---------------------------------------------------------------------------
// Currency — serialization
// ---------------------------------------------------------------------------

nlohmann::json Currency::toJson() const {
    return {{"cp", copper}, {"sp", silver}, {"ep", electrum},
            {"gp", gold},   {"pp", platinum}};
}

void Currency::fromJson(const nlohmann::json& j) {
    copper   = j.value("cp", 0);
    silver   = j.value("sp", 0);
    electrum = j.value("ep", 0);
    gold     = j.value("gp", 0);
    platinum = j.value("pp", 0);
}

// ---------------------------------------------------------------------------
// CurrencySystem
// ---------------------------------------------------------------------------

int CurrencySystem::toCopper(int cp, int sp, int ep, int gp, int pp) {
    return cp
         + sp * Currency::CP_PER_SP
         + ep * Currency::CP_PER_EP
         + gp * Currency::CP_PER_GP
         + pp * Currency::CP_PER_PP;
}

Currency CurrencySystem::fromCopper(int totalCp) {
    if (totalCp <= 0) return {};
    Currency c;
    c.add(totalCp);
    return c;
}

std::string CurrencySystem::formatValue(int copperCost) {
    return fromCopper(copperCost).toString();
}

// Rarity base value ranges (typical market price mid-point, in copper):
//   Common:    5 gp  = 500 cp
//   Uncommon: 250 gp = 25000 cp
//   Rare:    2500 gp = 250000 cp
//   VeryRare:25000 gp = 2500000 cp
//   Legendary:---  (priceless / 500000 gp)
//   Artifact:       (not for sale)
static const int RARITY_VALUE_CP[] = {
    500,        // 0: Common
    25000,      // 1: Uncommon
    250000,     // 2: Rare
    2500000,    // 3: VeryRare
    50000000,   // 4: Legendary
    0           // 5: Artifact (priceless)
};

int CurrencySystem::rarityBaseValueCp(int rarityIndex) {
    if (rarityIndex < 0 || rarityIndex > 5) return 0;
    return RARITY_VALUE_CP[rarityIndex];
}

} // namespace Core
} // namespace Phyxel
