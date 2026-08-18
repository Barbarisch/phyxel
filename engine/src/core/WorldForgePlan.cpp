#include "core/WorldForgePlan.h"

#include "utils/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <queue>

namespace Phyxel {

// ── Grounded per-class road spec (resources/settlement_program.json street entries) ───────────
// track  = hamlet's unpaved rural way (Dirt; width 3 = the village lane_width — a hamlet has no
//          formal street, its paths read as lanes);
// road   = village main street preset (Gravel, main_width 5);
// highway= town main street preset (Cobblestone, main_width 6).
namespace {
constexpr float kRoadWidth[4] = {0.0f, 3.0f, 5.0f, 6.0f};
constexpr const char* kRoadMaterial[4] = {"", "Dirt", "Gravel", "Cobblestone"};
}  // namespace

float WorldForgePlan::roadHalfWidth(int cls) {
    if (cls < 1 || cls > 3) return 0.0f;
    return kRoadWidth[cls] * 0.5f;
}

const char* WorldForgePlan::roadMaterial(int cls) {
    if (cls < 1 || cls > 3) return "";
    return kRoadMaterial[cls];
}

// L4-tested settlement footprints (settlement morphology v2): village 80x48 and town 140x60 are
// the live-verified sizes; hamlet 40x32 is REASONED (smaller than the smallest tested village,
// fits the 3-6 building tier band) — logged in docs/WorldForge.md grounding notes.
WorldForgePlan::TierPreset WorldForgePlan::tierPreset(const std::string& tier) {
    if (tier == "town") return {"town", 140, 60};
    if (tier == "village") return {"village", 80, 48};
    return {"hamlet", 40, 32};
}

uint32_t WorldForgePlan::siteSeed(uint32_t worldSeed, const glm::ivec2& pos) {
    // Same avalanche idiom as the terrain-noise hash (tnHash): pure in (worldSeed, position),
    // so a site's settlement build is reproducible from the world seed alone.
    uint32_t h = worldSeed ^ 0x57F0A6E1u;
    auto mix = [&h](uint32_t v) {
        h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
        h = ((h >> 16) ^ h) * 0x45D9F3Bu;
        h = ((h >> 16) ^ h) * 0x45D9F3Bu;
        h = (h >> 16) ^ h;
    };
    mix(static_cast<uint32_t>(pos.x));
    mix(static_cast<uint32_t>(pos.y));
    return h == 0 ? 1u : h;  // 0 means "derive from position" downstream; never emit it
}

// ── Bake internals ────────────────────────────────────────────────────────────────────────────
namespace {

// Score weights + shapes. REASONED values (no direct historical dataset — logged in
// docs/WorldForge.md grounding notes): relief dominates because an unbuildable site refuses
// at realization; water is the classic siting driver (settlements NEAR water, not in it);
// biome hostility discounts sand/snow/rock surfaces.
constexpr float kWRelief = 0.45f, kWWater = 0.30f, kWBiome = 0.25f;
constexpr float kReliefFullScoreSd = 8.0f;   // stddev (voxels) at which relief score hits 0
constexpr float kWaterTooCloseU = 40.0f;     // inside this = flood-plain discount
constexpr float kWaterBellFarU = 300.0f;     // full score out to here…
constexpr float kWaterDecayU = 400.0f;       // …then exponential decay scale
constexpr float kMinViableScore = 0.05f;     // below this a candidate is not worth seating

// Router costs (units: one flat cell step = ~1). Slope aversion: cost multiplies by
// (1 + kSlopeAversion·grade)², grade = rise/run — a 30% grade cell costs ~30x flat.
// River-crossing penalties are per channel cell entered: order>=3 (a real river needing a
// bridge — V1 honest gap) vs order 1-2 (a fordable creek). Values REASONED: the resulting
// detour horizon is a few dozen cells (a few km at the 128 u production cell) — bridges are
// normal, roads just prefer fewer/narrower crossings. Standing water is impassable; cells
// bordering it carry a small shore-clearance penalty so smoothed centerlines keep off wet cells.
constexpr float kSlopeAversion = 15.0f;
constexpr float kPenaltyRiverMajor = 30.0f;  // order >= 3
constexpr float kPenaltyRiverMinor = 10.0f;  // order 1-2
constexpr float kPenaltyShore = 5.0f;

// Detour-factor loop relaxation: add a non-MST road when its direct cost beats 1.4x the
// tree path (the classic route-directness threshold; REASONED, logged).
constexpr float kDetourFactor = 1.4f;

constexpr float kResampleU = 16.0f;          // centerline point spacing
constexpr float kRasterCellU = 8.0f;
constexpr int kRasterMaxCells = 2048;        // per axis; cellSize grows if the bbox needs more

int tierRank(const std::string& tier) {
    if (tier == "town") return 3;
    if (tier == "village") return 2;
    return 1;
}

float distToSegment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    const float t = len2 > 0.0f ? glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + ab * t));
}

// Footprint-window relief: stddev of the surface over a 5x5 sample of a w x d window.
// The same buildability notion analyzeSite uses (window relief, not point slope).
float footprintReliefSd(const WorldForgePlan::HeightFn& heightAt, const glm::vec2& centre,
                        float w, float d) {
    float sum = 0.0f, sum2 = 0.0f;
    constexpr int N = 5;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            const float x = centre.x + (i / float(N - 1) - 0.5f) * w;
            const float z = centre.y + (j / float(N - 1) - 0.5f) * d;
            const float h = heightAt(x, z);
            sum += h;
            sum2 += h * h;
        }
    const float n = float(N * N);
    const float mean = sum / n;
    const float var = std::max(0.0f, sum2 / n - mean * mean);
    return std::sqrt(var);
}

struct SiteScoreParts {
    float relief, water, biome, total;
};

SiteScoreParts scoreAt(const glm::vec2& pos, const WorldForgePlan::HeightFn& heightAt,
                       const FlowField& flow, const WaterBodyIndex& bodies,
                       const WorldForgePlan::SurfaceMatFn& surfaceMatAt, float probeW,
                       float probeD) {
    SiteScoreParts s{};
    const float sd = footprintReliefSd(heightAt, pos, probeW, probeD);
    s.relief = std::max(0.0f, 1.0f - sd / kReliefFullScoreSd);
    // Water proximity: nearest channel (creeks count — minOrder 1) or water body bbox.
    float dWater = flow.nearestChannel(pos.x, pos.y, 512.0f, /*minOrder=*/1).dist;
    for (const auto& b : bodies.bodies()) {
        // bbox is in bake-cell coords; convert to a world rect and take point-rect distance.
        // Requires the flow/hydro grids to share geometry (they do — one bake).
        const float cell = flow.cellSize();
        const float rx0 = flow.originX() + b.bboxMin.x * cell;
        const float rz0 = flow.originZ() + b.bboxMin.y * cell;
        const float rx1 = flow.originX() + (b.bboxMax.x + 1) * cell;
        const float rz1 = flow.originZ() + (b.bboxMax.y + 1) * cell;
        const float dx = std::max({rx0 - pos.x, 0.0f, pos.x - rx1});
        const float dz = std::max({rz0 - pos.y, 0.0f, pos.y - rz1});
        dWater = std::min(dWater, std::sqrt(dx * dx + dz * dz));
    }
    if (dWater < kWaterTooCloseU) s.water = 0.25f;
    else if (dWater <= kWaterBellFarU) s.water = 1.0f;
    else s.water = std::exp(-(dWater - kWaterBellFarU) / kWaterDecayU);
    // Surface hostility: the resolved material already folds in the generator's physical
    // overrides (below-sea sand, steep rock, treeline snow).
    const std::string mat = surfaceMatAt(static_cast<int>(std::lround(pos.x)),
                                         static_cast<int>(std::lround(pos.y)));
    if (mat.rfind("Grass", 0) == 0 || mat == "Dirt") s.biome = 1.0f;
    else if (mat == "Sand" || mat == "SnowGrass") s.biome = 0.5f;
    else if (mat == "Snow" || mat == "Stone") s.biome = 0.15f;
    else s.biome = 0.7f;
    s.total = kWRelief * s.relief + kWWater * s.water + kWBiome * s.biome;
    return s;
}

// A* over a routing subgrid. Returns the path as cell indices (start..goal), empty on failure,
// and the path cost via outCost.
struct RoutingGrid {
    int x0 = 0, z0 = 0, nx = 0, nz = 0;   // subgrid in bake-cell coords
    float originX = 0.0f, originZ = 0.0f, cellSize = 1.0f;   // bake-grid geometry
    std::vector<float> height;
    std::vector<uint8_t> wet, shore;
    std::vector<int> order;
    int idx(int cx, int cz) const { return (cz - z0) * nx + (cx - x0); }
    bool contains(int cx, int cz) const {
        return cx >= x0 && cz >= z0 && cx < x0 + nx && cz < z0 + nz;
    }
    glm::vec2 cellCentre(int cx, int cz) const {
        return {originX + (cx + 0.5f) * cellSize, originZ + (cz + 0.5f) * cellSize};
    }
};

std::vector<int> astarRoute(const RoutingGrid& g, int startIdx, int goalIdx, float& outCost) {
    outCost = 1e30f;
    const int n = g.nx * g.nz;
    if (startIdx < 0 || goalIdx < 0 || startIdx >= n || goalIdx >= n) return {};
    if (g.wet[startIdx] || g.wet[goalIdx]) return {};
    std::vector<float> gCost(n, 1e30f);
    std::vector<int> parent(n, -1);
    // (f, idx) min-heap; ties resolved by index → deterministic expansion order.
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    const int gx = goalIdx % g.nx, gz = goalIdx / g.nx;
    auto heur = [&](int i) {
        const int cx = i % g.nx, cz = i / g.nx;
        const float dx = float(cx - gx), dz = float(cz - gz);
        return std::sqrt(dx * dx + dz * dz);
    };
    gCost[startIdx] = 0.0f;
    open.push({heur(startIdx), startIdx});
    static const int NB[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                 {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    while (!open.empty()) {
        const auto [f, cur] = open.top();
        open.pop();
        if (cur == goalIdx) break;
        if (f > gCost[cur] + heur(cur) + 1e-4f) continue;   // stale entry
        const int cx = cur % g.nx, cz = cur / g.nx;
        for (const auto& nb : NB) {
            const int tx = cx + nb[0], tz = cz + nb[1];
            if (tx < 0 || tz < 0 || tx >= g.nx || tz >= g.nz) continue;
            const int ti = tz * g.nx + tx;
            if (g.wet[ti]) continue;
            const float stepLen = (nb[0] != 0 && nb[1] != 0) ? 1.41421356f : 1.0f;
            const float grade = std::fabs(g.height[ti] - g.height[cur]) / (stepLen * g.cellSize);
            float c = stepLen * (1.0f + kSlopeAversion * grade) * (1.0f + kSlopeAversion * grade);
            if (g.order[ti] >= 3) c += kPenaltyRiverMajor;
            else if (g.order[ti] >= 1) c += kPenaltyRiverMinor;
            else if (g.shore[ti]) c += kPenaltyShore;
            if (gCost[cur] + c < gCost[ti] - 1e-6f) {
                gCost[ti] = gCost[cur] + c;
                parent[ti] = cur;
                open.push({gCost[ti] + heur(ti), ti});
            }
        }
    }
    if (gCost[goalIdx] >= 1e30f) return {};
    outCost = gCost[goalIdx];
    std::vector<int> path;
    for (int i = goalIdx; i != -1; i = parent[i]) path.push_back(i);
    std::reverse(path.begin(), path.end());
    return path;
}

// Chaikin corner cutting (endpoints preserved), then arc-length resample at kResampleU.
std::vector<glm::vec2> smoothResample(std::vector<glm::vec2> pts) {
    for (int pass = 0; pass < 2 && pts.size() >= 3; ++pass) {
        std::vector<glm::vec2> out;
        out.reserve(pts.size() * 2);
        out.push_back(pts.front());
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            out.push_back(pts[i] * 0.75f + pts[i + 1] * 0.25f);
            out.push_back(pts[i] * 0.25f + pts[i + 1] * 0.75f);
        }
        out.push_back(pts.back());
        pts = std::move(out);
    }
    if (pts.size() < 2) return pts;
    std::vector<glm::vec2> out;
    out.push_back(pts.front());
    float carry = 0.0f;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const glm::vec2 a = pts[i], b = pts[i + 1];
        const float segLen = glm::length(b - a);
        if (segLen <= 1e-6f) continue;
        float t = kResampleU - carry;
        while (t < segLen) {
            out.push_back(a + (b - a) * (t / segLen));
            t += kResampleU;
        }
        carry = segLen - (t - kResampleU);
    }
    if (glm::length(out.back() - pts.back()) > 1e-3f) out.push_back(pts.back());
    return out;
}

}  // namespace

std::shared_ptr<const WorldForgePlan> WorldForgePlan::bake(
    const WorldForgeParams& params, uint32_t worldSeed, const HeightFn& heightAt,
    const HydrologyMap& hydro, const FlowField& flow, const WaterBodyIndex& bodies,
    const SurfaceMatFn& surfaceMatAt, const SurfaceYFn& surfaceYAt,
    const ChannelFn& channelAt) {
    const auto t0 = std::chrono::steady_clock::now();
    auto plan = std::shared_ptr<WorldForgePlan>(new WorldForgePlan());
    const WorldForgeParams P = params.clamped();
    plan->m_params = P;

    const float cell = hydro.cellSize();
    const float ox = hydro.originX(), oz = hydro.originZ();
    const int nx = hydro.cellsX(), nz = hydro.cellsZ();
    if (nx <= 0 || nz <= 0 || cell <= 0.0f) return plan;
    const glm::vec2 regionCentre(ox + nx * cell * 0.5f, oz + nz * cell * 0.5f);
    const float seaLvl = hydro.seaLevel();

    // ── Siting: score candidate bake cells within the region radius ──────────────────────────
    struct Cand {
        glm::vec2 pos;       // cell centre (world)
        SiteScoreParts score;
        int cellIdx;
    };
    std::vector<Cand> cands;
    for (int cz = 0; cz < nz; ++cz)
        for (int cx = 0; cx < nx; ++cx) {
            const glm::vec2 p(ox + (cx + 0.5f) * cell, oz + (cz + 0.5f) * cell);
            if (glm::length(p - regionCentre) > P.regionRadius) continue;
            const float h = heightAt(p.x, p.y);
            if (h < seaLvl + 2.0f) continue;                        // coastal strip / seabed
            const float wl = hydro.waterLevelAt(p.x, p.y);
            if (wl > h - 0.1f) continue;                            // wet bake cell (lake/ocean)
            if (flow.orderAt(p.x, p.y) >= 3) continue;              // carved river runs here
            Cand c;
            c.pos = p;
            c.cellIdx = cz * nx + cx;
            // Probe relief over the village footprint — the mid tier — so scoring does not
            // depend on the not-yet-assigned tier. Town sites re-check at realization.
            c.score = scoreAt(p, heightAt, flow, bodies, surfaceMatAt, 80.0f, 48.0f);
            cands.push_back(std::move(c));
        }

    // ── Selection: pins verbatim first, then greedy argmax with spacing constraints ──────────
    struct Picked {
        glm::vec2 pos;
        SiteScoreParts score;
        bool pinned;
    };
    std::vector<Picked> picked;
    for (const auto& pin : P.sitePins) {
        if (static_cast<int>(picked.size()) >= P.siteCount) break;
        const glm::vec2 p(static_cast<float>(pin.x), static_cast<float>(pin.y));
        picked.push_back({p, scoreAt(p, heightAt, flow, bodies, surfaceMatAt, 80.0f, 48.0f), true});
    }
    while (static_cast<int>(picked.size()) < P.siteCount) {
        int best = -1;
        float bestEff = kMinViableScore;
        for (size_t i = 0; i < cands.size(); ++i) {
            const Cand& c = cands[i];
            float dMin = 1e30f;
            for (const auto& pk : picked) dMin = std::min(dMin, glm::length(c.pos - pk.pos));
            if (dMin < P.minSpacing) continue;                       // hard spacing
            float eff = c.score.total;
            if (!picked.empty() && dMin > P.maxSpacing) eff *= 0.5f; // soft cohesion
            if (eff > bestEff + 1e-6f) {                             // strict > → lowest index wins ties
                bestEff = eff;
                best = static_cast<int>(i);
            }
        }
        if (best < 0) break;                                         // honest degradation
        picked.push_back({cands[best].pos, cands[best].score, false});
        cands.erase(cands.begin() + best);
    }
    if (picked.empty()) return plan;

    // ── Tier assignment by score rank: best = town, next two = villages, rest hamlets ────────
    std::vector<int> rank(picked.size());
    for (size_t i = 0; i < rank.size(); ++i) rank[i] = static_cast<int>(i);
    std::stable_sort(rank.begin(), rank.end(), [&](int a, int b) {
        return picked[a].score.total > picked[b].score.total;
    });
    std::vector<std::string> tierOf(picked.size());
    for (size_t r = 0; r < rank.size(); ++r)
        tierOf[rank[r]] = r == 0 ? "town" : (r <= 2 ? "village" : "hamlet");

    // ── Position refinement (non-pinned): minimize footprint relief on a 16 u offset grid ────
    // `positions` carries each site's CURRENT position (refined for already-processed sites,
    // the picked centre otherwise) so the spacing constraint below is enforced against every
    // pair's FINAL positions: when site b refines, every a<b is final — check(a,b) holds.
    std::vector<glm::vec2> positions;
    positions.reserve(picked.size());
    for (const auto& pk : picked) positions.push_back(pk.pos);
    for (size_t i = 0; i < picked.size(); ++i) {
        const TierPreset preset = tierPreset(tierOf[i]);
        WorldForgeSite site;
        site.id = static_cast<int>(i);
        site.tier = preset.name;
        site.width = preset.width;
        site.depth = preset.depth;
        glm::vec2 pos = picked[i].pos;
        if (!picked[i].pinned) {
            float bestSd = 1e30f;
            glm::vec2 bestPos = pos;
            for (int oj = 0; oj < 8; ++oj)
                for (int oi = 0; oi < 8; ++oi) {
                    const glm::vec2 cand = pos + glm::vec2((oi - 3.5f) * 16.0f, (oj - 3.5f) * 16.0f);
                    if (glm::length(cand - regionCentre) > P.regionRadius) continue;
                    const float h = heightAt(cand.x, cand.y);
                    if (h < seaLvl + 2.0f || hydro.waterLevelAt(cand.x, cand.y) > h - 0.1f)
                        continue;
                    // Refinement must not drift a pair under the hard spacing minimum
                    // (selection checked centres; each site may then move up to ~80 u).
                    bool tooClose = false;
                    for (size_t k = 0; k < positions.size(); ++k)
                        if (k != i && glm::length(cand - positions[k]) < P.minSpacing)
                            tooClose = true;
                    if (tooClose) continue;
                    const float sd = footprintReliefSd(heightAt, cand,
                                                       static_cast<float>(preset.width),
                                                       static_cast<float>(preset.depth));
                    if (sd < bestSd - 1e-6f) {                       // strict < → lowest offset wins ties
                        bestSd = sd;
                        bestPos = cand;
                    }
                }
            pos = bestPos;
            positions[i] = pos;
        }
        site.pos = glm::ivec2(static_cast<int>(std::lround(pos.x)),
                              static_cast<int>(std::lround(pos.y)));
        site.seed = siteSeed(worldSeed, site.pos);
        site.surfaceY = heightAt(static_cast<float>(site.pos.x), static_cast<float>(site.pos.y));
        site.surfaceMat = surfaceMatAt(site.pos.x, site.pos.y);
        site.score = {picked[i].score.relief, picked[i].score.water, picked[i].score.biome,
                      picked[i].score.total};
        plan->m_sites.push_back(std::move(site));
    }

    // ── Routing: A* per pair on the bake-cell grid, MST + detour-relaxed loops ────────────────
    const size_t nSites = plan->m_sites.size();
    if (nSites >= 2) {
        // Routing subgrid: site bbox in cell coords + margin, clamped to the bake grid.
        int cx0 = nx, cz0 = nz, cx1 = 0, cz1 = 0;
        for (const auto& s : plan->m_sites) {
            const int cx = static_cast<int>(std::floor((s.pos.x - ox) / cell));
            const int cz = static_cast<int>(std::floor((s.pos.y - oz) / cell));
            cx0 = std::min(cx0, cx); cx1 = std::max(cx1, cx);
            cz0 = std::min(cz0, cz); cz1 = std::max(cz1, cz);
        }
        constexpr int kMargin = 16;
        RoutingGrid g;
        g.x0 = std::max(0, cx0 - kMargin);
        g.z0 = std::max(0, cz0 - kMargin);
        g.nx = std::min(nx - 1, cx1 + kMargin) - g.x0 + 1;
        g.nz = std::min(nz - 1, cz1 + kMargin) - g.z0 + 1;
        g.originX = ox; g.originZ = oz; g.cellSize = cell;
        const int gn = g.nx * g.nz;
        g.height.resize(gn); g.wet.assign(gn, 0); g.shore.assign(gn, 0); g.order.resize(gn);
        for (int cz = g.z0; cz < g.z0 + g.nz; ++cz)
            for (int cx = g.x0; cx < g.x0 + g.nx; ++cx) {
                const glm::vec2 p = g.cellCentre(cx, cz);
                const int i = g.idx(cx, cz);
                g.height[i] = heightAt(p.x, p.y);
                g.wet[i] = hydro.waterLevelAt(p.x, p.y) > g.height[i] + 0.1f ? 1 : 0;
                g.order[i] = flow.orderAt(p.x, p.y);
            }
        for (int cz = g.z0; cz < g.z0 + g.nz; ++cz)
            for (int cx = g.x0; cx < g.x0 + g.nx; ++cx) {
                const int i = g.idx(cx, cz);
                if (g.wet[i]) continue;
                if ((g.contains(cx + 1, cz) && g.wet[g.idx(cx + 1, cz)]) ||
                    (g.contains(cx - 1, cz) && g.wet[g.idx(cx - 1, cz)]) ||
                    (g.contains(cx, cz + 1) && g.wet[g.idx(cx, cz + 1)]) ||
                    (g.contains(cx, cz - 1) && g.wet[g.idx(cx, cz - 1)]))
                    g.shore[i] = 1;
            }
        auto siteCell = [&](const WorldForgeSite& s) {
            const int cx = glm::clamp(static_cast<int>(std::floor((s.pos.x - ox) / cell)),
                                      g.x0, g.x0 + g.nx - 1);
            const int cz = glm::clamp(static_cast<int>(std::floor((s.pos.y - oz) / cell)),
                                      g.z0, g.z0 + g.nz - 1);
            return g.idx(cx, cz);
        };

        struct Pair {
            int a, b;
            float cost;
            std::vector<int> path;
        };
        std::vector<Pair> pairs;
        for (size_t a = 0; a < nSites; ++a)
            for (size_t b = a + 1; b < nSites; ++b) {
                Pair pr{static_cast<int>(a), static_cast<int>(b), 1e30f, {}};
                pr.path = astarRoute(g, siteCell(plan->m_sites[a]), siteCell(plan->m_sites[b]),
                                     pr.cost);
                if (!pr.path.empty()) pairs.push_back(std::move(pr));
            }
        // Kruskal MST over pair costs (deterministic tie order: cost, then a, then b).
        std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& x, const Pair& y) {
            if (x.cost != y.cost) return x.cost < y.cost;
            if (x.a != y.a) return x.a < y.a;
            return x.b < y.b;
        });
        std::vector<int> parent(nSites);
        for (size_t i = 0; i < nSites; ++i) parent[i] = static_cast<int>(i);
        std::function<int(int)> find = [&](int v) {
            return parent[v] == v ? v : parent[v] = find(parent[v]);
        };
        std::vector<const Pair*> chosen;
        std::vector<std::vector<std::pair<int, float>>> mstAdj(nSites);
        for (const auto& pr : pairs) {
            if (find(pr.a) == find(pr.b)) continue;
            parent[find(pr.a)] = find(pr.b);
            chosen.push_back(&pr);
            mstAdj[pr.a].push_back({pr.b, pr.cost});
            mstAdj[pr.b].push_back({pr.a, pr.cost});
        }
        // Loop relaxation: a direct road beats a 1.4x-longer tree detour.
        auto treePathCost = [&](int a, int b) {
            std::vector<float> dist(nSites, 1e30f);
            std::vector<int> stack{a};
            dist[a] = 0.0f;
            while (!stack.empty()) {
                const int v = stack.back();
                stack.pop_back();
                for (const auto& [w, c] : mstAdj[v])
                    if (dist[v] + c < dist[w]) {
                        dist[w] = dist[v] + c;
                        stack.push_back(w);
                    }
            }
            return dist[b];
        };
        for (const auto& pr : pairs) {
            if (std::find(chosen.begin(), chosen.end(), &pr) != chosen.end()) continue;
            if (pr.cost * kDetourFactor < treePathCost(pr.a, pr.b)) chosen.push_back(&pr);
        }

        // ── Realize each chosen edge: smooth, resample, trim at footprints, mark crossings ───
        for (const Pair* pr : chosen) {
            WorldForgeRoad road;
            road.a = pr->a;
            road.b = pr->b;
            road.cls = std::min(tierRank(plan->m_sites[pr->a].tier),
                                tierRank(plan->m_sites[pr->b].tier));
            std::vector<glm::vec2> pts;
            pts.reserve(pr->path.size() + 2);
            pts.push_back(glm::vec2(plan->m_sites[pr->a].pos));
            for (const int ci : pr->path) {
                const int cx = g.x0 + (ci % g.nx), cz = g.z0 + (ci / g.nx);
                pts.push_back(g.cellCentre(cx, cz));
            }
            pts.push_back(glm::vec2(plan->m_sites[pr->b].pos));
            road.centerline = smoothResample(std::move(pts));
            // Trim inside the endpoint footprints. Inset 1 (was 8): the road must REACH the
            // footprint boundary so the arrival-aligned main street can meet it — an 8-cube
            // unpaved shoulder was the longitudinal half of the street↔road junction gap
            // (RoadsReachTheFootprintEdge, red-first).
            auto insideFootprint = [&](const glm::vec2& p, const WorldForgeSite& s) {
                return std::fabs(p.x - s.pos.x) <= s.width * 0.5f + 1.0f &&
                       std::fabs(p.y - s.pos.y) <= s.depth * 0.5f + 1.0f;
            };
            {
                auto& cl = road.centerline;
                size_t first = 0;
                while (first + 2 < cl.size() && insideFootprint(cl[first + 1], plan->m_sites[pr->a]))
                    ++first;
                size_t last = cl.size() - 1;
                while (last > first + 1 && insideFootprint(cl[last - 1], plan->m_sites[pr->b]))
                    --last;
                cl = std::vector<glm::vec2>(cl.begin() + first, cl.begin() + last + 1);
            }
            // Crossings: one per contiguous run of CARVED channel along the line, labeled with
            // the run's max order at the point where that max occurred. Detection uses the
            // carve-accurate (meander-warped) channelAt — the raw FlowField cell line can sit
            // ~a channel-width away from the bed the terrain actually carves, which put bridge
            // spans on dry ground (caught red by CrossingsGetBridgeSpans).
            // 2 u steps along each segment: the carved line is only ~a channel-width wide
            // (order 3 ≈ 5 u) — the 16 u resampled points alone can straddle it entirely.
            int runMaxOrder = 0;
            glm::vec2 runPos{0.0f};
            for (size_t i = 0; i + 1 < road.centerline.size(); ++i) {
                const glm::vec2 a = road.centerline[i], b = road.centerline[i + 1];
                const float len = glm::length(b - a);
                if (len <= 1e-4f) continue;
                for (float t = 0.0f; t < len; t += 2.0f) {
                    const glm::vec2 p = a + (b - a) * (t / len);
                    const FlowField::ChannelHit ch = channelAt(p.x, p.y);
                    const int order = (ch.hit && ch.depth >= 0.15f) ? ch.order : 0;
                    if (order >= 1) {
                        if (order > runMaxOrder) {
                            runMaxOrder = order;
                            runPos = p;
                        }
                    } else if (runMaxOrder >= 1) {
                        road.crossings.push_back({runPos, runMaxOrder});
                        runMaxOrder = 0;
                    }
                }
            }
            if (runMaxOrder >= 1) road.crossings.push_back({runPos, runMaxOrder});
            plan->m_roads.push_back(std::move(road));
        }
    }

    // ── Bridge spans (placer #44): every order>=3 crossing gets a flat plank deck ────────────
    // Endpoints are found by marching along the road's local tangent from the crossing point
    // until the CARVE-ACCURATE channel (meander-warped channelAt, not the raw cell line) has
    // been dry for 3 consecutive 1 u steps — the banks — plus a 2 u shoulder onto each. The
    // deck is FLAT at the higher bank's REAL emitted surface (surfaceYAt = sampleColumn), so
    // the deck meets the road the generator actually produces. A channel too wide to clear
    // within 96 u yields NO deck — surfaced in the log, never a half-bridge.
    for (const auto& road : plan->m_roads) {
        for (const auto& cross : road.crossings) {
            if (cross.riverOrder < 3) continue;
            // Local road tangent at the crossing (nearest centerline point's neighbors).
            size_t ci = 0;
            float best = 1e30f;
            for (size_t i = 0; i < road.centerline.size(); ++i) {
                const float d = glm::length(road.centerline[i] - cross.pos);
                if (d < best) { best = d; ci = i; }
            }
            if (road.centerline.size() < 2) continue;
            const glm::vec2 tan = glm::normalize(
                road.centerline[std::min(ci + 1, road.centerline.size() - 1)] -
                road.centerline[ci > 0 ? ci - 1 : 0]);
            if (!std::isfinite(tan.x)) continue;
            auto bankFrom = [&](float sign) -> std::pair<bool, glm::vec2> {
                int dry = 0;
                for (float s = 0.0f; s <= 96.0f; s += 1.0f) {
                    const glm::vec2 pt = cross.pos + tan * (sign * s);
                    const FlowField::ChannelHit ch = channelAt(pt.x, pt.y);
                    if (ch.hit && ch.depth >= 0.15f) dry = 0;
                    else if (++dry >= 3) return {true, pt};
                }
                return {false, glm::vec2(0.0f)};
            };
            const auto [okA, bankA] = bankFrom(-1.0f);
            const auto [okB, bankB] = bankFrom(+1.0f);
            if (!okA || !okB) {
                LOG_WARN_FMT("WorldForge", "[WORLDFORGE] order-" << cross.riverOrder
                             << " crossing at (" << cross.pos.x << "," << cross.pos.y
                             << ") too wide for a 96 u deck — no bridge (gap surfaced)");
                continue;
            }
            WorldForgeBridgeSpan span;
            span.a = bankA - tan * 2.0f;   // 2 u shoulder onto each bank
            span.b = bankB + tan * 2.0f;
            span.deckY = static_cast<float>(std::max(
                surfaceYAt(static_cast<int>(std::lround(span.a.x)),
                           static_cast<int>(std::lround(span.a.y))),
                surfaceYAt(static_cast<int>(std::lround(span.b.x)),
                           static_cast<int>(std::lround(span.b.y)))));
            span.cls = road.cls;
            span.crossingOrder = cross.riverOrder;
            plan->m_bridges.push_back(span);
        }
    }

    // ── Road raster: nearest-segment index per 8 u cell over the network bbox ────────────────
    for (size_t r = 0; r < plan->m_roads.size(); ++r) {
        const auto& cl = plan->m_roads[r].centerline;
        for (size_t i = 0; i + 1 < cl.size(); ++i) {
            if (plan->m_segments.size() >= 0xFFFE) break;   // uint16 raster limit (log below)
            plan->m_segments.push_back({cl[i], cl[i + 1], static_cast<uint16_t>(r),
                                        static_cast<uint8_t>(plan->m_roads[r].cls)});
        }
    }
    if (!plan->m_segments.empty()) {
        glm::vec2 lo(1e30f), hi(-1e30f);
        for (const auto& s : plan->m_segments) {
            lo = glm::min(lo, glm::min(s.a, s.b));
            hi = glm::max(hi, glm::max(s.a, s.b));
        }
        constexpr float kPad = 32.0f;
        lo -= glm::vec2(kPad);
        hi += glm::vec2(kPad);
        float rcell = kRasterCellU;
        while ((hi.x - lo.x) / rcell > kRasterMaxCells || (hi.y - lo.y) / rcell > kRasterMaxCells)
            rcell *= 2.0f;
        auto& R = plan->m_raster;
        R.originX = lo.x;
        R.originZ = lo.y;
        R.cellSize = rcell;
        R.cellsX = static_cast<int>(std::ceil((hi.x - lo.x) / rcell)) + 1;
        R.cellsZ = static_cast<int>(std::ceil((hi.y - lo.y) / rcell)) + 1;
        R.nearestSeg.assign(static_cast<size_t>(R.cellsX) * R.cellsZ, 0xFFFF);
        std::vector<float> bestDist(R.nearestSeg.size(), 1e30f);
        for (size_t si = 0; si < plan->m_segments.size(); ++si) {
            const auto& s = plan->m_segments[si];
            const float reach = roadHalfWidth(s.cls) + 2.0f * rcell;
            const glm::vec2 slo = glm::min(s.a, s.b) - glm::vec2(reach);
            const glm::vec2 shi = glm::max(s.a, s.b) + glm::vec2(reach);
            const int ix0 = std::max(0, static_cast<int>((slo.x - R.originX) / rcell));
            const int iz0 = std::max(0, static_cast<int>((slo.y - R.originZ) / rcell));
            const int ix1 = std::min(R.cellsX - 1, static_cast<int>((shi.x - R.originX) / rcell));
            const int iz1 = std::min(R.cellsZ - 1, static_cast<int>((shi.y - R.originZ) / rcell));
            for (int iz = iz0; iz <= iz1; ++iz)
                for (int ix = ix0; ix <= ix1; ++ix) {
                    const glm::vec2 c(R.originX + (ix + 0.5f) * rcell,
                                      R.originZ + (iz + 0.5f) * rcell);
                    const float d = distToSegment(c, s.a, s.b);
                    if (d > reach) continue;
                    const size_t cellIdx = static_cast<size_t>(iz) * R.cellsX + ix;
                    if (d < bestDist[cellIdx]) {
                        bestDist[cellIdx] = d;
                        R.nearestSeg[cellIdx] = static_cast<uint16_t>(si);
                    }
                }
        }
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    LOG_INFO_FMT("WorldForge", "[WORLDFORGE] plan baked: " << plan->m_sites.size() << " sites, "
                 << plan->m_roads.size() << " roads, " << plan->m_bridges.size() << " bridges, "
                 << plan->m_segments.size() << " segments in " << ms << " ms (hash "
                 << plan->planHash() << ")");
    return plan;
}

WorldForgePlan::BridgeHit WorldForgePlan::bridgeAt(float worldX, float worldZ) const {
    BridgeHit hit;
    const glm::vec2 p(worldX, worldZ);
    for (const auto& b : m_bridges) {
        if (distToSegment(p, b.a, b.b) <= roadHalfWidth(b.cls)) {
            hit.deckY = b.deckY;
            hit.cls = b.cls;
            return hit;
        }
    }
    return hit;
}

WorldForgePlan::RoadHit WorldForgePlan::roadAt(float worldX, float worldZ) const {
    RoadHit hit;
    if (m_raster.cellsX <= 0 || m_raster.cellsZ <= 0) return hit;
    const int cx = static_cast<int>(std::floor((worldX - m_raster.originX) / m_raster.cellSize));
    const int cz = static_cast<int>(std::floor((worldZ - m_raster.originZ) / m_raster.cellSize));
    if (cx < 0 || cz < 0 || cx >= m_raster.cellsX || cz >= m_raster.cellsZ) return hit;
    const uint16_t segIdx = m_raster.nearestSeg[static_cast<size_t>(cz) * m_raster.cellsX + cx];
    if (segIdx == 0xFFFF) return hit;
    // Exact distance against the raster's candidate segment and its polyline neighbors (the
    // raster is conservative near joints; the true nearest is one of these).
    const glm::vec2 p(worldX, worldZ);
    const uint16_t roadIdx = m_segments[segIdx].roadIdx;
    for (int di = -1; di <= 1; ++di) {
        const int i = static_cast<int>(segIdx) + di;
        if (i < 0 || i >= static_cast<int>(m_segments.size())) continue;
        const Segment& s = m_segments[i];
        if (s.roadIdx != roadIdx) continue;
        const glm::vec2 ab = s.b - s.a;
        const float len2 = glm::dot(ab, ab);
        const float t = len2 > 0.0f ? glm::clamp(glm::dot(p - s.a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        const float d = glm::length(p - (s.a + ab * t));
        if (d < hit.dist) {
            hit.dist = d;
            hit.cls = s.cls;
            hit.roadIdx = roadIdx;
        }
    }
    return hit;
}

nlohmann::json WorldForgePlan::toJson() const {
    nlohmann::json j;
    j["version"] = m_params.version;
    j["params"] = m_params.toJson();
    nlohmann::json sites = nlohmann::json::array();
    for (const auto& s : m_sites) {
        sites.push_back({
            {"id", s.id},
            {"x", s.pos.x},
            {"z", s.pos.y},
            {"tier", s.tier},
            {"width", s.width},
            {"depth", s.depth},
            {"seed", s.seed},
            {"surface", s.surfaceMat},
            {"surfaceY", s.surfaceY},
            {"score", {{"relief", s.score.relief},
                       {"water", s.score.water},
                       {"biome", s.score.biome},
                       {"total", s.score.total}}},
        });
    }
    j["sites"] = sites;
    nlohmann::json roads = nlohmann::json::array();
    for (const auto& r : m_roads) {
        nlohmann::json line = nlohmann::json::array();
        // Decimate to every 4th point (+ always the last) — inspection payload, not geometry
        // of record (the raster/segments carry that).
        for (size_t i = 0; i < r.centerline.size(); i += 4)
            line.push_back({{"x", r.centerline[i].x}, {"z", r.centerline[i].y}});
        if (!r.centerline.empty() && (r.centerline.size() - 1) % 4 != 0)
            line.push_back({{"x", r.centerline.back().x}, {"z", r.centerline.back().y}});
        nlohmann::json crossings = nlohmann::json::array();
        for (const auto& c : r.crossings)
            crossings.push_back({{"x", c.pos.x}, {"z", c.pos.y}, {"order", c.riverOrder}});
        roads.push_back({{"a", r.a}, {"b", r.b}, {"class", r.cls},
                         {"points", static_cast<int>(r.centerline.size())},
                         {"centerline", line}, {"crossings", crossings}});
    }
    j["roads"] = roads;
    nlohmann::json bridges = nlohmann::json::array();
    for (const auto& b : m_bridges)
        bridges.push_back({{"ax", b.a.x}, {"az", b.a.y}, {"bx", b.b.x}, {"bz", b.b.y},
                           {"deckY", b.deckY}, {"class", b.cls}, {"order", b.crossingOrder}});
    j["bridges"] = bridges;
    return j;
}

uint64_t WorldForgePlan::planHash() const {
    const std::string s = toJson().dump();
    uint64_t h = 1469598103934665603ull;  // FNV-1a 64
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace Phyxel
