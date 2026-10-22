#include "core/CivPlan.h"

#include "core/WorldSheet.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
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
// The macro base is NOT the realized surface: the meso relaxation and the M2e coast shaping pull a
// near-shore column down to the beach (measured 2026-10-22: 3 of 157 baseline sites stood at
// exactly sea level on the real terrain, one of them a box hamlet). Land for siting is the macro
// base above the same coastal strip the box plan's candidate scan already refuses (REUSED:
// WorldForgePlan rejects h < sea + 2 u); the sea itself keeps the strict sub-sea definition.
constexpr float kCoastStripU = 2.0f;
constexpr float kHexPack = 0.866f;   // density = 1 / (kHexPack·s²): hexagonal central-place packing
// ── M3b (D23): the growth rate per 25-year epoch at a mid suitability (0.5). GROUNDED: England
// ≈ 2.0 M (1086) → ≈ 5 M (1300) is ×2.5 over 8.5 epochs = ×1.114 per epoch; a site grows at
// kGrowth × (0.5 + score), so score 0.5 is the arc's own rate and a 1.0 site grows half again as
// fast. Decline after the shock: the first mechanism tried — viability, score × (1 − shock) under
// the plan's own kMinViable — abandoned NOTHING at −45 % (every picked site scores above 0.27) and
// 148 of 154 at −90 % (red 2026-10-22, m3b_red_green.txt): the greedy pick seats only the better
// cells, so a fixed floor is never a 20 % band. The rate is therefore an INPUT (civAbandonShare,
// GROUNDED from the deserted-village count) applied to the lowest-suitability village-and-below
// sites founded before the shock (Beresford: the deserted villages were the marginal ones); a
// mechanism that DERIVES the rate is NEEDS-RESEARCH. Cities and towns never abandon (DESIGN: the
// seats of the hierarchy survive the arc; §10 F4g).
constexpr float kGrowthPerEpoch = 0.114f;

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

const char* CivPlan::stateName(int state) {
    static const char* names[3] = {"live", "declining", "abandoned"};
    return (state >= 0 && state < 3) ? names[state] : "?";
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
            const bool shore = c.baseHeight <= in.seaLevel + kCoastStripU;   // the beach band: never a site
            const bool basin = c.filled > c.baseHeight + 1e-3f;
            const bool river = c.drainageArea >= in.macroRiverAreaM2 && !sub;
            seaCell[k] = sub ? 1 : 0;
            water[k] = (sub || basin || river) ? 1 : 0;
            land[k] = (!shore && !basin) ? 1 : 0;
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
    // civDensityScale multiplies the DENSITY of every tier: the ladder's spacings shrink by
    // sqrt(scale) and the targets follow from the same formula. (Stress red 2026-10-22: scaling
    // the targets alone at the unchanged spacing placed FEWER sites at x10 than at x1 - the
    // spacing is what the density is.)
    const float spacingScale = 1.0f / std::sqrt(std::max(1e-3f, P.civDensityScale));
    int cumulative = 0;
    for (int t = 0; t < kTiers; ++t) {
        const float sKm = P.civSpacingKm[t] * spacingScale;
        plan->m_spacingCells[t] = sKm * 1000.0f / cu;
        const int cumTarget = static_cast<int>(std::floor(plan->m_landKm2 / (kHexPack * sKm * sKm)));
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
        // Stress red 2026-10-22: 50 pins on one cell seated 50 towns on top of each other, and
        // pins closer than the town spacing seated silently. A pin is refused, never nudged.
        bool dup = false; const Site* near = nullptr;
        for (const auto& p : picked) {
            if (p.cell == static_cast<int>(k)) { dup = true; break; }
            const int pi = p.cell % n, pj = p.cell / n;
            const float d = std::sqrt(static_cast<float>((pi - ci) * (pi - ci) + (pj - cj) * (pj - cj)));
            if (d < plan->m_spacingCells[Town]) { near = &p; break; }
        }
        if (dup) { plan->m_refusals.push_back("pin (" + std::to_string(pin.x) + "," + std::to_string(pin.y) + ") is already a site"); continue; }
        if (near) {
            plan->m_refusals.push_back("pin (" + std::to_string(pin.x) + "," + std::to_string(pin.y) + ") is within the town spacing of pin (" +
                                       std::to_string(static_cast<int>(near->pos.x)) + "," + std::to_string(static_cast<int>(near->pos.y)) + ")");
            continue;
        }
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
                // a same-or-higher site constrains at THIS tier's spacing; an already-seated
                // lower-tier site (a pinned town while cities are picked) at ITS OWN spacing,
                // so the invariant "every site keeps its tier's spacing from every site of its
                // tier or higher" holds for the pinned site too
                const float needD = p.tier > t ? plan->m_spacingCells[p.tier] : sp;
                const int pi = p.cell % n, pj = p.cell / n;
                const float d = std::sqrt(static_cast<float>((pi - si) * (pi - si) + (pj - sj) * (pj - sj)));
                if (d < needD) { ok = false; break; }
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

    // ── M3b: the history ledger (D21: tiers never change; D22: a ruin is a state; D23: the arc) ─
    plan->m_epochs = P.civEpochs;
    {
        const int E = P.civEpochs, shock = P.civShockEpoch;
        // Founding: cities/towns/villages stand at epoch 0 (the Domesday places); hamlets and
        // outposts are founded in score order over the epochs before the shock (the growth that
        // filled the land 1086 → 1300) — the best site of each tier first.
        int rankInTier[kTiers] = {0, 0, 0, 0, 0};
        const int spread = E == 0 ? 0 : (shock > 0 ? shock : E);
        for (auto& s : plan->m_sites) {
            const int r = rankInTier[s.tier]++;
            const int nT = plan->m_count[s.tier];
            s.founded = (s.tier >= Hamlet && spread > 0 && nT > 0)
                            ? std::min(E, static_cast<int>(std::floor(static_cast<float>(r) / nT * spread)))
                            : 0;
            s.population = std::max(kMinViable, s.score);
            s.peak = s.founded;
            s.state = Live;
            s.history.push_back({s.founded, "founded", s.population});
        }
        for (int e = 1; e <= E; ++e) {
            for (auto& s : plan->m_sites) {
                if (e < s.founded || s.state == Abandoned) continue;
                if (s.state == Declining) {
                    s.state = Abandoned;
                    s.population = 0.0f;
                    s.history.push_back({e, "abandoned", 0.0f});
                    continue;
                }
                if (e == shock) {
                    s.population *= (1.0f - P.civShockFraction);
                    s.history.push_back({e, "shock", s.population});
                    continue;
                }
                s.population *= 1.0f + kGrowthPerEpoch * (0.5f + s.score);
                s.history.push_back({e, "grew", s.population});
            }
            if (e == shock && P.civAbandonShare > 0.0f) {
                // the marginal share of each village-and-below tier declines (abandoned next epoch)
                for (int t = Village; t < kTiers; ++t) {
                    std::vector<Site*> standing;
                    for (auto& s : plan->m_sites)
                        if (s.tier == t && s.state == Live && s.founded <= e) standing.push_back(&s);
                    std::stable_sort(standing.begin(), standing.end(), [](const Site* a, const Site* b) { return a->score < b->score; });
                    const int nDecline = static_cast<int>(std::ceil(P.civAbandonShare * static_cast<float>(standing.size()) - 1e-4f));
                    for (int k = 0; k < nDecline && k < static_cast<int>(standing.size()); ++k) {
                        standing[static_cast<size_t>(k)]->state = Declining;
                        standing[static_cast<size_t>(k)]->history.push_back({e, "declined", standing[static_cast<size_t>(k)]->population});
                    }
                }
            }
        }
        for (auto& s : plan->m_sites) {
            float best = -1.0f;
            for (const auto& ev : s.history) if (ev.value > best) { best = ev.value; s.peak = ev.epoch; }
        }
        // Provinces: every land cell to its nearest city/town seat (8-neighbour Dijkstra over the
        // grid, water crossed at the same cost — a seat's reach is distance, not terrain, until M4's
        // routes exist); members = the sites whose cell carries the seat's label.
        std::vector<int> seats;
        for (const auto& s : plan->m_sites) if (s.tier <= Town) seats.push_back(s.id);
        if (!seats.empty()) {
            std::vector<float> dist(N, 1e30f);
            std::vector<int> label(N, -1);
            using QE = std::pair<float, int>;
            std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
            for (int id : seats) {
                const int c = plan->m_sites[static_cast<size_t>(id)].cell;
                dist[static_cast<size_t>(c)] = 0.0f; label[static_cast<size_t>(c)] = id; q.push({0.0f, c});
            }
            const float s2 = std::sqrt(2.0f);
            while (!q.empty()) {
                const QE top = q.top(); q.pop();
                const int c = top.second;
                if (top.first > dist[static_cast<size_t>(c)]) continue;
                const int ci = c % n, cj = c / n;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        if (!di && !dj) continue;
                        const int ni = ci + di, nj = cj + dj;
                        if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
                        const int nc = nj * n + ni;
                        const float nd = top.first + ((di && dj) ? s2 : 1.0f);
                        if (nd < dist[static_cast<size_t>(nc)]) { dist[static_cast<size_t>(nc)] = nd; label[static_cast<size_t>(nc)] = label[static_cast<size_t>(c)]; q.push({nd, nc}); }
                    }
            }
            plan->m_provinces.resize(seats.size());
            std::vector<int> provinceOfSeat(plan->m_sites.size(), -1);
            for (size_t k = 0; k < seats.size(); ++k) { plan->m_provinces[k].seat = seats[k]; provinceOfSeat[static_cast<size_t>(seats[k])] = static_cast<int>(k); }
            for (size_t k = 0; k < N; ++k)
                if (land[k] && label[k] >= 0) ++plan->m_provinces[static_cast<size_t>(provinceOfSeat[static_cast<size_t>(label[k])])].cells;
            for (const auto& s : plan->m_sites) {
                const int l = label[static_cast<size_t>(s.cell)];
                if (l >= 0) plan->m_provinces[static_cast<size_t>(provinceOfSeat[static_cast<size_t>(l)])].members.push_back(s.id);
            }
        }
    }
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
                         {"forest", s.forest}, {"mining", s.mining}, {"pinned", s.pinned},
                         {"founded", s.founded}, {"peak", s.peak}, {"state", stateName(s.state)}, {"population", s.population},
                         {"history", [&] { nlohmann::json h = nlohmann::json::array(); for (const auto& e : s.history) h.push_back({{"epoch", e.epoch}, {"event", e.kind}, {"value", e.value}}); return h; }()}});
    j["sites"] = sites;
    j["refusals"] = m_refusals;
    j["epochs"] = m_epochs;
    nlohmann::json provinces = nlohmann::json::array();
    for (const auto& p : m_provinces) provinces.push_back({{"seat", p.seat}, {"cells", p.cells}, {"members", p.members}});
    j["provinces"] = provinces;
    return j;
}

}  // namespace Phyxel
