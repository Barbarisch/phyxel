#include "core/CivPlan.h"

#include "core/WorldSheet.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <queue>

namespace Phyxel {

namespace {

// Temperature anchors shared with the column sampler (docs/TerrainGenerationV2.md §P1): the
// normalized climate plane is −5..+30 °C, the lapse rate 6.5 °C/km at the P0 vertical
// compression, the treeline −8 °C and the snow line 0 °C. Reused here on the macro base height.
constexpr float kTempSpanC      = 35.0f;
constexpr float kSnowTemp01     = 5.0f / kTempSpanC;
constexpr float kTreelineTemp01 = -3.0f / kTempSpanC;
constexpr float kLapse01PerVoxel = (6.5f / 1000.0f) * 15.0f / kTempSpanC;

// Emilien 2012's water bell in macro cells (DESIGN inside the form: near, not in): full score
// within kWaterFull cells of water, decaying past it, nothing beyond kWaterFar.
constexpr float kWaterFullCells = 1.0f, kWaterFarCells = 4.0f;
// Trade: the coast within kCoastTradeCells, or a river whose drainage reaches kTradeRiverAreaM2.
constexpr float kCoastTradeCells = 2.0f;
constexpr float kTradeRiverAreaM2 = 1.0e7f;   // 10 km²: the law's ~0.8 u deep, 5 u wide river
// Score weights (Veloren's weighted resource sum; the split is DESIGN, stated in the plan).
constexpr float kWRelief = 0.30f, kWWater = 0.30f, kWFood = 0.20f, kWTrade = 0.10f, kWForest = 0.05f, kWMining = 0.05f;
constexpr float kMinViable = 0.15f;
constexpr float kHexPack = 0.866f;   // density = 1 / (kHexPack·s²): hexagonal central-place packing

inline float to01(float n) { return n < -1.0f ? 0.0f : (n > 1.0f ? 1.0f : (n + 1.0f) * 0.5f); }
inline float bell(float x, float lo, float hi, float width) {   // 1 inside [lo,hi], smooth to 0 `width` outside
    if (x >= lo && x <= hi) return 1.0f;
    const float d = x < lo ? lo - x : x - hi;
    return std::max(0.0f, 1.0f - d / width);
}
uint32_t hash2(uint32_t s, int i, int j) {
    uint32_t h = s ^ 0x9E3779B9u;
    h ^= static_cast<uint32_t>(i) * 0x85EBCA6Bu; h = (h ^ (h >> 13)) * 0xC2B2AE35u;
    h ^= static_cast<uint32_t>(j) * 0x27D4EB2Fu; h = (h ^ (h >> 16)) * 0x165667B1u;
    return h ^ (h >> 15);
}
// A coarse "ore" band: value noise on an 8-cell (4 km) lattice, smoothed — a resource FIELD exists
// nowhere else in the engine, so this is a labelled DESIGN placeholder (plan M3 design check).
float miningAt(uint32_t seed, int i, int j) {
    const int gi = i >> 3, gj = j >> 3;
    const float fi = (i & 7) / 8.0f, fj = (j & 7) / 8.0f;
    auto v = [&](int a, int b) { return (hash2(seed ^ 0x51A7u, a, b) & 0xFFFF) / 65535.0f; };
    const float x0 = v(gi, gj) * (1 - fi) + v(gi + 1, gj) * fi, x1 = v(gi, gj + 1) * (1 - fi) + v(gi + 1, gj + 1) * fi;
    const float n = x0 * (1 - fj) + x1 * fj;
    return n > 0.7f ? (n - 0.7f) / 0.3f : 0.0f;
}
// Multi-source chamfer distance (cells) from the seed cells.
void chamfer(std::vector<float>& d, int n) {
    const float s2 = std::sqrt(2.0f);
    auto at = [&](int i, int j) -> float& { return d[static_cast<size_t>(j) * n + i]; };
    for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
        float v = at(i, j);
        if (i > 0) v = std::min(v, at(i - 1, j) + 1.0f);
        if (j > 0) v = std::min(v, at(i, j - 1) + 1.0f);
        if (i > 0 && j > 0) v = std::min(v, at(i - 1, j - 1) + s2);
        if (i + 1 < n && j > 0) v = std::min(v, at(i + 1, j - 1) + s2);
        at(i, j) = v;
    }
    for (int j = n - 1; j >= 0; --j) for (int i = n - 1; i >= 0; --i) {
        float v = at(i, j);
        if (i + 1 < n) v = std::min(v, at(i + 1, j) + 1.0f);
        if (j + 1 < n) v = std::min(v, at(i, j + 1) + 1.0f);
        if (i + 1 < n && j + 1 < n) v = std::min(v, at(i + 1, j + 1) + s2);
        if (i > 0 && j + 1 < n) v = std::min(v, at(i - 1, j + 1) + s2);
        at(i, j) = v;
    }
}

}  // namespace

const char* CivPlan::tierName(int tier) {
    static const char* names[kTiers] = {"city", "town", "village", "hamlet", "outpost"};
    return (tier >= 0 && tier < kTiers) ? names[tier] : "?";
}

int CivPlan::tierFromName(const std::string& name) {
    for (int t = 0; t < kTiers; ++t) if (name == tierName(t)) return t;
    return -1;
}

std::shared_ptr<const CivPlan> CivPlan::bake(const Inputs& in, const WorldForgeParams& paramsIn) {
    auto plan = std::shared_ptr<CivPlan>(new CivPlan());
    const auto t0 = std::chrono::steady_clock::now();
    if (!in.macro || !paramsIn.civ) return plan;
    const WorldForgeParams P = paramsIn.clamped();
    const MacroSheet& M = *in.macro;
    const int n = M.cells();
    const float cu = M.cellU();
    const size_t N = static_cast<size_t>(n) * n;
    const float cellKm2 = cu * cu * 1e-6f;

    // ── Per-cell facts ──────────────────────────────────────────────────────────────────────
    std::vector<uint8_t> land(N, 0), water(N, 0), seaCell(N, 0);
    std::vector<float> slope(N, 0.0f), riverArea(N, 0.0f);
    int landCells = 0;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const MacroCell& c = M.cell(i, j);
            const size_t k = static_cast<size_t>(j) * n + i;
            const bool sub = c.baseHeight <= in.seaLevel;
            const bool basin = c.filled > c.baseHeight + 1e-3f;
            const bool river = c.drainageArea >= in.macroRiverAreaM2 && !sub;
            seaCell[k] = sub ? 1 : 0;
            water[k] = (sub || basin || river) ? 1 : 0;
            land[k] = (!sub && !basin) ? 1 : 0;
            riverArea[k] = c.drainageArea;
            if (land[k]) ++landCells;
            const float hx = (M.cell(std::min(n - 1, i + 1), j).baseHeight - M.cell(std::max(0, i - 1), j).baseHeight) / (2.0f * cu);
            const float hz = (M.cell(i, std::min(n - 1, j + 1)).baseHeight - M.cell(i, std::max(0, j - 1)).baseHeight) / (2.0f * cu);
            slope[k] = std::sqrt(hx * hx + hz * hz);
        }
    plan->m_landKm2 = landCells * cellKm2;
    // distance (cells) to water of any kind, and to the sea alone (trade)
    std::vector<float> dWater(N, 1e9f), dSea(N, 1e9f);
    bool anySea = false, anyWater = false;
    for (size_t k = 0; k < N; ++k) { if (water[k]) { dWater[k] = 0.0f; anyWater = true; } if (seaCell[k]) { dSea[k] = 0.0f; anySea = true; } }
    if (anyWater) chamfer(dWater, n);
    if (anySea) chamfer(dSea, n);

    // ── Suitability ─────────────────────────────────────────────────────────────────────────
    std::vector<Site> cand;
    cand.reserve(N / 4);
    const float villageCap = P.civSlopeCap[Village];
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const size_t k = static_cast<size_t>(j) * n + i;
            if (!land[k]) continue;
            const MacroCell& c = M.cell(i, j);
            Site s;
            s.cell = static_cast<int>(k);
            s.pos = glm::vec2(M.originU() + (i + 0.5f) * cu, M.originU() + (j + 0.5f) * cu);
            s.slope = slope[k];
            s.waterCells = dWater[k];
            float tRaw = 0.0f, mRaw = 0.0f;
            if (in.climate) in.climate(s.pos.x, s.pos.y, tRaw, mRaw);
            const float t01 = to01(tRaw), m01 = to01(mRaw);
            const float altitude = std::max(0.0f, c.baseHeight - in.seaLevel);
            const float effTemp = t01 - altitude * kLapse01PerVoxel;
            s.hostility = effTemp < kTreelineTemp01 ? 0.15f : (effTemp < kSnowTemp01 ? 0.5f : 1.0f);
            s.food   = bell(effTemp, 0.35f, 0.70f, 0.20f) * bell(m01, 0.30f, 0.80f, 0.25f);
            s.forest = std::max(0.0f, (m01 - 0.5f) / 0.5f) * (effTemp > kTreelineTemp01 ? 1.0f : 0.0f);
            s.mining = miningAt(in.seed, i, j);
            const float coastTrade = dSea[k] <= kCoastTradeCells ? 1.0f : 0.0f;
            const float riverTrade = riverArea[k] >= kTradeRiverAreaM2 ? 1.0f : 0.0f;
            s.trade = std::max(coastTrade, riverTrade);
            const float relief = std::max(0.0f, 1.0f - s.slope / villageCap);
            const float waterScore = s.waterCells <= kWaterFullCells ? 1.0f
                                   : std::max(0.0f, 1.0f - (s.waterCells - kWaterFullCells) / (kWaterFarCells - kWaterFullCells));
            s.score = (kWRelief * relief + kWWater * waterScore + kWFood * s.food + kWTrade * s.trade +
                       kWForest * s.forest + kWMining * s.mining) * s.hostility;
            if (s.score >= kMinViable) cand.push_back(s);
        }
    // strictly by score, index ties: deterministic
    std::stable_sort(cand.begin(), cand.end(), [](const Site& a, const Site& b) { return a.score > b.score; });

    // ── Targets from the ladder (D19) ───────────────────────────────────────────────────────
    int cumulative = 0;
    for (int t = 0; t < kTiers; ++t) {
        const float sKm = P.civSpacingKm[t];
        plan->m_spacingCells[t] = sKm * 1000.0f / cu;
        const int cumTarget = static_cast<int>(std::floor(plan->m_landKm2 / (kHexPack * sKm * sKm) * P.civDensityScale));
        plan->m_target[t] = std::max(0, cumTarget - cumulative);
        cumulative = std::max(cumulative, cumTarget);
    }

    // ── Pins: seated as towns, or refused with the reason ───────────────────────────────────
    std::vector<Site> picked;
    auto cellOf = [&](const glm::ivec2& p, int& ci, int& cj) {
        ci = static_cast<int>(std::floor((p.x - M.originU()) / cu));
        cj = static_cast<int>(std::floor((p.y - M.originU()) / cu));
        return ci >= 0 && cj >= 0 && ci < n && cj < n;
    };
    for (const auto& pin : P.sitePins) {
        int ci, cj;
        if (!cellOf(pin, ci, cj)) { plan->m_refusals.push_back("pin (" + std::to_string(pin.x) + "," + std::to_string(pin.y) + ") is outside the continent"); continue; }
        const size_t k = static_cast<size_t>(cj) * n + ci;
        if (!land[k]) { plan->m_refusals.push_back("pin (" + std::to_string(pin.x) + "," + std::to_string(pin.y) + ") is in water or a basin"); continue; }
        if (slope[k] > P.civSlopeCap[Town]) { plan->m_refusals.push_back("pin (" + std::to_string(pin.x) + "," + std::to_string(pin.y) + ") is steeper than the town cap"); continue; }
        Site s;
        s.cell = static_cast<int>(k);
        s.pos = glm::vec2(M.originU() + (ci + 0.5f) * cu, M.originU() + (cj + 0.5f) * cu);
        s.tier = Town; s.pinned = true; s.slope = slope[k]; s.waterCells = dWater[k]; s.score = 1.0f;
        picked.push_back(s);
        ++plan->m_count[Town];
    }

    // ── Greedy per tier: spacing against every site of its tier or higher ───────────────────
    std::vector<uint8_t> used(N, 0);
    for (const auto& s : picked) used[static_cast<size_t>(s.cell)] = 1;
    for (int t = 0; t < kTiers; ++t) {
        const float sp = plan->m_spacingCells[t];
        const float cap = P.civSlopeCap[t];
        int need = plan->m_target[t] - plan->m_count[t];
        for (size_t ci = 0; ci < cand.size() && need > 0; ++ci) {
            Site s = cand[ci];
            if (used[static_cast<size_t>(s.cell)] || s.slope > cap) continue;
            bool ok = true;
            const int si = s.cell % n, sj = s.cell / n;
            for (const auto& p : picked) {
                if (p.tier > t) continue;   // lower tiers do not constrain a higher one
                const int pi = p.cell % n, pj = p.cell / n;
                const float d = std::sqrt(static_cast<float>((pi - si) * (pi - si) + (pj - sj) * (pj - sj)));
                if (d < sp) { ok = false; break; }
            }
            if (!ok) continue;
            s.tier = t;
            picked.push_back(s);
            used[static_cast<size_t>(s.cell)] = 1;
            ++plan->m_count[t];
            --need;
        }
        if (need > 0)
            plan->m_refusals.push_back(std::string(tierName(t)) + ": " + std::to_string(need) + " of " +
                                       std::to_string(plan->m_target[t]) + " unplaced (no viable cell at the ladder's spacing)");
    }
    for (size_t i = 0; i < picked.size(); ++i) picked[i].id = static_cast<int>(i);
    plan->m_sites = std::move(picked);
    plan->m_bakeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return plan;
}

nlohmann::json CivPlan::toJson() const {
    nlohmann::json j;
    j["land_km2"] = m_landKm2;
    j["bake_ms"] = m_bakeMs;
    nlohmann::json tiers = nlohmann::json::array();
    for (int t = 0; t < kTiers; ++t)
        tiers.push_back({{"tier", tierName(t)}, {"target", m_target[t]}, {"count", m_count[t]}, {"spacing_cells", m_spacingCells[t]}});
    j["tiers"] = tiers;
    nlohmann::json sites = nlohmann::json::array();
    for (const auto& s : m_sites)
        sites.push_back({{"id", s.id}, {"x", s.pos.x}, {"z", s.pos.y}, {"tier", tierName(s.tier)}, {"score", s.score},
                         {"slope", s.slope}, {"water_cells", s.waterCells}, {"food", s.food}, {"trade", s.trade},
                         {"forest", s.forest}, {"mining", s.mining}, {"pinned", s.pinned}});
    j["sites"] = sites;
    j["refusals"] = m_refusals;
    return j;
}

}  // namespace Phyxel
