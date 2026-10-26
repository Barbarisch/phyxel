#include "core/WorldForgePlan.h"

#include "utils/Logger.h"

#include <algorithm>
#include <climits>
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

float WorldForgePlan::cutLimit(int cls) {
    // The cut depth at which a road stops cutting and climbs a short pitch instead. Audit
    // 2026-10-26: this is NOT a repose derivation - the bank below a cut is carried by the
    // shoulder, which gradeBlendAt now widens to cut / kReposeSlope so the bank IS at repose
    // whatever the depth. The trigger depth stays (half-width + shoulder) x repose = 3.15 /
    // 3.85 / 4.2 u, relabelled DESIGN: the depth whose repose bank would be as wide as the
    // corridor itself (section 10 row).
    return (roadHalfWidth(cls) + kShoulderU) * kReposeSlope;
}

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
constexpr size_t kRasterSegmentCap = 0xFFFE;  // uint16 segment index in the raster
static size_t g_rasterSegmentCap = kRasterSegmentCap;   // M0: test hook lowers it

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
    std::vector<uint8_t> carved;   // M2b-2: carved-river class per cell
    std::vector<float> corr;       // M4a: cost multiplier inside the continent corridor band (1 = none)
    int idx(int cx, int cz) const { return (cz - z0) * nx + (cx - x0); }
    bool contains(int cx, int cz) const {
        return cx >= x0 && cz >= z0 && cx < x0 + nx && cz < z0 + nz;
    }
    glm::vec2 cellCentre(int cx, int cz) const {
        return {originX + (cx + 0.5f) * cellSize, originZ + (cz + 0.5f) * cellSize};
    }
};

// M4b (D26): a step whose rise/run exceeds the class cap costs kOverCapPenalty times more, so the
// search takes the oblique traverse (a switchback is an emergent zig-zag of such steps) wherever
// one exists and only climbs straight where nothing else reaches the goal (DESIGN, reported per
// route as maxGrade). The k-neighbourhood (Galin 2010: every offset within radius 3 cells with
// gcd 1, 32 directions) gives the search long oblique steps whose grade is low across a slope that
// every 8-neighbour step would climb straight.
constexpr float kOverCapPenalty = 25.0f;
constexpr float kSwitchbackBandU = 256.0f;   // M4b-3: the half-width the legs fold inside (= the corridor band, DESIGN)
constexpr int   kNeighbourRadius = 3;

std::vector<int> astarRoute(const RoutingGrid& g, int startIdx, int goalIdx, float& outCost, float cap = 0.0f) {
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
    // M4b-2 audit: the corridor discount (g.corr < 1) lowers step costs, so the Euclidean
    // heuristic must be scaled by the smallest multiplier to stay admissible (CorridorGraph did;
    // this search did not - a weighted A* up to 3x inside the band, valid but not least-cost)
    float heurScale = 1.0f;
    for (const float m : g.corr) heurScale = std::min(heurScale, m);
    auto heur = [&](int i) {
        const int cx = i % g.nx, cz = i / g.nx;
        const float dx = float(cx - gx), dz = float(cz - gz);
        return std::sqrt(dx * dx + dz * dz) * heurScale;
    };
    gCost[startIdx] = 0.0f;
    open.push({heur(startIdx), startIdx});
    static const int NB[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                 {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    // the k-neighbourhood (cap > 0): every offset within the radius whose gcd is 1 (a local, not
    // a static - two plans may bake on two threads)
    std::vector<std::pair<int, int>> kNB;
    if (cap > 0.0f)
        for (int dz = -kNeighbourRadius; dz <= kNeighbourRadius; ++dz)
            for (int dx = -kNeighbourRadius; dx <= kNeighbourRadius; ++dx) {
                if (!dx && !dz) continue;
                int a = std::abs(dx), b = std::abs(dz);
                while (b) { const int t = a % b; a = b; b = t; }
                if (a == 1) kNB.push_back({dx, dz});
            }
    const size_t nNb = cap > 0.0f ? kNB.size() : 8;
    while (!open.empty()) {
        const auto [f, cur] = open.top();
        open.pop();
        if (cur == goalIdx) break;
        if (f > gCost[cur] + heur(cur) + 1e-4f) continue;   // stale entry
        const int cx = cur % g.nx, cz = cur / g.nx;
        for (size_t k = 0; k < nNb; ++k) {
            const int ox = cap > 0.0f ? kNB[k].first : NB[k][0], oz = cap > 0.0f ? kNB[k].second : NB[k][1];
            const int tx = cx + ox, tz = cz + oz;
            if (tx < 0 || tz < 0 || tx >= g.nx || tz >= g.nz) continue;
            const int ti = tz * g.nx + tx;
            if (g.wet[ti]) continue;
            const float stepLen = std::sqrt(static_cast<float>(ox * ox + oz * oz));
            // a long step's grade is the steepest rise between CONSECUTIVE cells along it, not the
            // endpoint difference: endpoints at equal heights with a 20 u bump between them read as
            // flat otherwise and the capped envelope then cuts the bump (measured 2026-10-26: 26.9 u)
            bool carved = g.carved[ti] != 0, river = g.order[ti] >= 1, shore = g.shore[ti] != 0, blocked = false;
            const int steps = std::max(std::abs(ox), std::abs(oz));
            const float subLen = stepLen / static_cast<float>(steps) * g.cellSize;
            float grade = 0.0f, prevH = g.height[cur];
            for (int m = 1; m <= steps; ++m) {
                const int mx = cx + (ox * m) / steps, mz = cz + (oz * m) / steps;
                const int mi = (m == steps) ? ti : mz * g.nx + mx;
                if (g.wet[mi]) { blocked = true; break; }
                grade = std::max(grade, std::fabs(g.height[mi] - prevH) / subLen);
                prevH = g.height[mi];
                if (m < steps) { carved = carved || g.carved[mi]; river = river || g.order[mi] >= 1; shore = shore || g.shore[mi]; }
            }
            if (blocked) continue;
            float c = stepLen * (1.0f + kSlopeAversion * grade) * (1.0f + kSlopeAversion * grade);
            if (carved) c += kPenaltyRiverMajor;          // M2b-2: the class, not the order
            else if (river) c += kPenaltyRiverMinor;
            else if (shore) c += kPenaltyShore;
            if (!g.corr.empty()) c *= g.corr[ti];          // M4a: follow the continent corridor
            if (cap > 0.0f && grade > cap) c *= kOverCapPenalty;   // M4b: only where no gentler way exists
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
// ── M4b-3 (Galin: switchbacks as edges): where a corridor-driven road's coarse line climbs
// steeper than its class cap over a stretch, the stretch is replaced by legs that cross the slope
// at the cap, folded inside the corridor band (+-W) with hairpins at the band edges. The face is
// taken as planar along the stretch (gradient s = climb / run): a leg at angle theta to the contour
// climbs s*sin(theta) per unit length, so sin(theta) = cap / s; each leg spans the band (2W across)
// and advances 2W*tan(theta) along the stretch; the legs needed = run / advance. A leg that would
// cross water, or a stretch too short for one leg, keeps the straight pitch (reported). Hairpins
// are the sharp reversals at the band edges; the 16 u Chaikin resample rounds them. DESIGN inside
// Galin's form: W = the corridor band (256 u), the planar-face assumption (rough faces leave small
// cuts the envelope handles). Returns the number of stretches replaced; legsLenU sums the legs.
struct SwitchbackStats { int stretches = 0, replaced = 0, water = 0, tooShort = 0; float climbReplacedU = 0.0f, climbKeptU = 0.0f; };
int insertSwitchbacks(std::vector<glm::vec2>& pts, float cap, float bandW,
                      const std::function<float(float, float)>& heightAt,
                      const std::function<bool(float, float)>& wetAt, float& legsLenU, SwitchbackStats* stats = nullptr) {
    legsLenU = 0.0f;
    if (pts.size() < 3 || cap <= 0.0f) return 0;
    std::vector<float> h(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) h[i] = heightAt(pts[i].x, pts[i].y);
    std::vector<glm::vec2> out;
    out.reserve(pts.size() * 2);
    int inserted = 0;
    size_t i = 0;
    while (i < pts.size()) {
        // a pitched stretch: consecutive steps steeper than the cap, same sign
        size_t j = i;
        float run = 0.0f;
        while (j + 1 < pts.size()) {
            const float len = glm::length(pts[j + 1] - pts[j]);
            if (len < 1e-3f) break;
            const float grade = (h[j + 1] - h[j]) / len;
            const float grade0 = (h[i + 1 < pts.size() ? i + 1 : i] - h[i]) / std::max(1e-3f, glm::length(pts[std::min(i + 1, pts.size() - 1)] - pts[i]));
            if (std::fabs(grade) <= cap || (grade > 0) != (grade0 > 0)) break;
            run += len;
            ++j;
        }
        if (j == i) { out.push_back(pts[i]); ++i; continue; }
        const float climb = h[j] - h[i];
        const float s = std::fabs(climb) / std::max(1e-3f, run);
        bool replaced = false, anyWet = false, anyShort = false;
        if (stats) ++stats->stretches;
        if (s > cap) {
            const glm::vec2 axis = glm::normalize(pts[j] - pts[i]);
            const glm::vec2 across(-axis.y, axis.x);
            const float sinT = std::min(1.0f, cap / s);
            const float tanT = sinT / std::sqrt(std::max(1e-6f, 1.0f - sinT * sinT));
            for (float W = bandW; W >= bandW * 0.25f && !replaced; W *= 0.5f) {
                const float advance = 2.0f * W * tanT;   // along the stretch per leg
                const float along = glm::length(pts[j] - pts[i]);
                // at least two legs: one leg would be the straight line itself (both ends on the
                // axis); a short stretch becomes a V out to the band edge and back (diagnostics
                // 2026-10-26: 12 of 16 mountain stretches were single 128 u steps skipped as "short")
                const int legs = std::max(2, static_cast<int>(std::ceil(along / std::max(1.0f, advance))));
                (void)anyShort;
                // build the legs: from pts[i], alternate the band side, advancing evenly
                std::vector<glm::vec2> zig;
                zig.push_back(pts[i]);
                float side = 1.0f;
                for (int k = 1; k <= legs; ++k) {
                    const float t = static_cast<float>(k) / legs;
                    const glm::vec2 base = pts[i] + axis * (along * t);
                    zig.push_back(k == legs ? pts[j] : base + across * (side * W));
                    side = -side;
                }
                // water check along every leg at 16 u
                bool wet = false;
                for (size_t k = 0; k + 1 < zig.size() && !wet; ++k) {
                    const float len = glm::length(zig[k + 1] - zig[k]);
                    for (float t = 0.0f; t <= len; t += 16.0f)
                        if (wetAt(zig[k].x + (zig[k + 1].x - zig[k].x) * t / std::max(1.0f, len), zig[k].y + (zig[k + 1].y - zig[k].y) * t / std::max(1.0f, len))) { wet = true; break; }
                }
                if (wet) { anyWet = true; continue; }
                for (size_t k = 0; k + 1 < zig.size(); ++k) { out.push_back(zig[k]); legsLenU += glm::length(zig[k + 1] - zig[k]); }
                replaced = true;
                ++inserted;
            }
        }
        if (!replaced) for (size_t k = i; k < j; ++k) out.push_back(pts[k]);
        if (stats) {
            if (replaced) { ++stats->replaced; stats->climbReplacedU += std::fabs(climb); }
            else { stats->climbKeptU += std::fabs(climb); if (anyWet) ++stats->water; else if (anyShort) ++stats->tooShort; }
        }
        i = j;
    }
    pts = std::move(out);
    return inserted;
}

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
    const ChannelFn& channelAt, const LevelFn& waterLevelAtFn, const CorridorFn& corridorMulAt,
    const std::vector<DemandEdge>* demand) {
    const auto t0 = std::chrono::steady_clock::now();
    // M2c: site, shore and route rules read the rim-corrected level when the generator gives one.
    const LevelFn levelAt = waterLevelAtFn ? waterLevelAtFn
                                           : LevelFn([&hydro](float x, float z) { return hydro.waterLevelAt(x, z); });
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
            const float wl = levelAt(p.x, p.y);
            if (wl > h - 0.1f) continue;                            // wet bake cell (lake/ocean)
            if (flow.carvedRiverAt(p.x, p.y)) continue;             // carved river runs here (M2b-2: the class)
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
    // M3a: a pinned site that came with a tier keeps it (the civ plan's tier, mapped to the three
    // the realizer knows); pins are the first `sitePins.size()` entries of `picked`, in order.
    for (size_t i = 0; i < picked.size() && i < P.sitePins.size() && i < P.pinTiers.size(); ++i)
        if (picked[i].pinned && !P.pinTiers[i].empty()) tierOf[i] = P.pinTiers[i];

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
        if (!picked[i].pinned || P.pinRefine) {   // M3a: civ-seeded pins refine too (D18)
            float bestSd = 1e30f;
            glm::vec2 bestPos = pos;
            for (int oj = 0; oj < 8; ++oj)
                for (int oi = 0; oi < 8; ++oi) {
                    const glm::vec2 cand = pos + glm::vec2((oi - 3.5f) * 16.0f, (oj - 3.5f) * 16.0f);
                    // A pinned site was seated outside the candidate scan by design (civ seeds
                    // cover the whole box, far beyond regionRadius): the radius bounds the SCAN,
                    // never a pin's refinement (red 2026-10-22: 0 of 5 civ seeds refined).
                    if (!picked[i].pinned && glm::length(cand - regionCentre) > P.regionRadius) continue;
                    const float h = heightAt(cand.x, cand.y);
                    if (h < seaLvl + 2.0f || levelAt(cand.x, cand.y) > h - 0.1f)
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
    SwitchbackStats swStatsTotal;   // M4b-3 diagnostics over every road
    if (nSites >= 2) {
        // Routing subgrid: site bbox in cell coords + margin, clamped to the bake grid.
        // M4a: the route search runs on a 128 u routing cell (V1 routed on 128 u bake cells; the
        // version-2 meso bake is 32 u, 16x the cells, and A* on the full grid cost 10+ s per demand
        // in Debug — measured 2026-10-22). `rcell` is the routing cell; the realized line is
        // resampled at 16 u and the crossings are found on the carve-accurate channel anyway.
        const int stride = std::max(1, static_cast<int>(std::lround(128.0f / cell)));
        const float rcell = cell * static_cast<float>(stride);
        const int rnx = (nx + stride - 1) / stride, rnz = (nz + stride - 1) / stride;
        int cx0 = rnx, cz0 = rnz, cx1 = 0, cz1 = 0;
        for (const auto& s : plan->m_sites) {
            const int cx = static_cast<int>(std::floor((s.pos.x - ox) / rcell));
            const int cz = static_cast<int>(std::floor((s.pos.y - oz) / rcell));
            cx0 = std::min(cx0, cx); cx1 = std::max(cx1, cx);
            cz0 = std::min(cz0, cz); cz1 = std::max(cz1, cz);
        }
        if (demand)   // M4a: the box-edge exits are routing endpoints too
            for (const auto& d : *demand)
                for (const glm::vec2* p : {d.a < 0 ? &d.pa : nullptr, d.b < 0 ? &d.pb : nullptr}) {
                    if (!p) continue;
                    const int cx = static_cast<int>(std::floor((p->x - ox) / rcell));
                    const int cz = static_cast<int>(std::floor((p->y - oz) / rcell));
                    cx0 = std::min(cx0, cx); cx1 = std::max(cx1, cx);
                    cz0 = std::min(cz0, cz); cz1 = std::max(cz1, cz);
                }
        constexpr int kMargin = 16;
        RoutingGrid g;
        g.x0 = std::max(0, cx0 - kMargin);
        g.z0 = std::max(0, cz0 - kMargin);
        g.nx = std::min(rnx - 1, cx1 + kMargin) - g.x0 + 1;
        g.nz = std::min(rnz - 1, cz1 + kMargin) - g.z0 + 1;
        g.originX = ox; g.originZ = oz; g.cellSize = rcell;
        const int gn = g.nx * g.nz;
        g.height.resize(gn); g.wet.assign(gn, 0); g.shore.assign(gn, 0); g.order.resize(gn); g.carved.assign(gn, 0);
        for (int cz = g.z0; cz < g.z0 + g.nz; ++cz)
            for (int cx = g.x0; cx < g.x0 + g.nx; ++cx) {
                const glm::vec2 p = g.cellCentre(cx, cz);
                const int i = g.idx(cx, cz);
                g.height[i] = heightAt(p.x, p.y);
                g.wet[i] = levelAt(p.x, p.y) > g.height[i] + 0.1f ? 1 : 0;
                g.order[i] = flow.orderAt(p.x, p.y);
                g.carved[i] = flow.carvedRiverAt(p.x, p.y) ? 1 : 0;
                if (corridorMulAt) { if (g.corr.empty()) g.corr.assign(gn, 1.0f); g.corr[i] = corridorMulAt(p.x, p.y); }
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
            const int cx = glm::clamp(static_cast<int>(std::floor((s.pos.x - ox) / rcell)),
                                      g.x0, g.x0 + g.nx - 1);
            const int cz = glm::clamp(static_cast<int>(std::floor((s.pos.y - oz) / rcell)),
                                      g.z0, g.z0 + g.nz - 1);
            return g.idx(cx, cz);
        };

        struct Pair {
            int a, b;
            float cost;
            std::vector<int> path;
            glm::vec2 pa{0.0f}, pb{0.0f};   // M4a: the endpoint when a / b is -1 (a box-edge exit)
            int cls = 0;                     // M4a: the corridor's class (0 = by endpoint tiers)
            std::vector<glm::vec2> pts;      // M4b: the refined fine-grid line (world XZ); empty = use `path` on the coarse grid
        };
        std::vector<Pair> pairs;
        auto pointCell = [&](const glm::vec2& p) {
            const int cx = glm::clamp(static_cast<int>(std::floor((p.x - ox) / rcell)), g.x0, g.x0 + g.nx - 1);
            const int cz = glm::clamp(static_cast<int>(std::floor((p.y - oz) / rcell)), g.z0, g.z0 + g.nz - 1);
            return g.idx(cx, cz);
        };
        std::vector<const Pair*> chosen;
        if (demand) {
            // M4a (D25): only what the corridor graph asked for — no all-pairs, no MST, no loops.
            // A demand whose pinned endpoint is beyond this plan's sites (the pin count was clamped)
            // is skipped; an unroutable demand is logged, never silently dropped.
            for (const auto& d : *demand) {
                if (d.a >= static_cast<int>(nSites) || d.b >= static_cast<int>(nSites)) continue;
                Pair pr{d.a, d.b, 1e30f, {}, d.pa, d.pb, d.cls};
                const int ca = d.a >= 0 ? siteCell(plan->m_sites[static_cast<size_t>(d.a)]) : pointCell(d.pa);
                const int cb = d.b >= 0 ? siteCell(plan->m_sites[static_cast<size_t>(d.b)]) : pointCell(d.pb);
                const float capHere = P.gradeCap[glm::clamp(d.cls, 1, 3)];
                pr.path = astarRoute(g, ca, cb, pr.cost, capHere);   // M4b: the class cap
                if (pr.path.empty()) {
                    LOG_WARN_FMT("WorldForge", "[WORLDFORGE] demand " << d.a << " -> " << d.b << " unroutable inside the box (water in every way)");
                    continue;
                }
                // M4b: refine on the BAKE grid inside a band around the coarse line ("solved on
                // macro for corridors then refined on meso", the M4 contract). The 128 u search
                // cannot see the fine band's bumps, so its line crossed them and the capped
                // envelope cut trenches (measured 2026-10-26: max cut 8.9 u -> 36.4 u on the
                // baseline, 178 u on Mountains). The fine search sees every 32 u cell and takes
                // the oblique line around a bump where one exists inside the band.
                // Fine-band refinement: OFF. Measured 2026-10-26: 320-360 s Debug per baseline box
                // plan (vs ~100 s) for a max cut of 26.9 u instead of 36.4 u - the capped envelope
                // still cut trenches wherever the band held no gentler line. The bounded-cut
                // envelope in the profile pass is what bounds the earthwork; the fine search is
                // kept for M4b-2 (switchbacks), which needs it inside a steep stretch only.
                constexpr bool kFineBandRefine = false;
                if (stride > 1 && kFineBandRefine) {
                    constexpr int kBandCoarseCells = 2;   // +-2 coarse cells = +-256 u, the corridor band (DESIGN)
                    const int bandFine = kBandCoarseCells * stride;
                    int fx0 = INT_MAX, fz0 = INT_MAX, fx1 = INT_MIN, fz1 = INT_MIN;
                    for (const int ci : pr.path) {
                        const int cx = (g.x0 + ci % g.nx) * stride + stride / 2, cz = (g.z0 + ci / g.nx) * stride + stride / 2;
                        fx0 = std::min(fx0, cx - bandFine); fz0 = std::min(fz0, cz - bandFine);
                        fx1 = std::max(fx1, cx + bandFine); fz1 = std::max(fz1, cz + bandFine);
                    }
                    RoutingGrid f;
                    f.x0 = std::max(0, fx0); f.z0 = std::max(0, fz0);
                    f.nx = std::min(nx - 1, fx1) - f.x0 + 1; f.nz = std::min(nz - 1, fz1) - f.z0 + 1;
                    f.originX = ox; f.originZ = oz; f.cellSize = cell;
                    const int fn = f.nx * f.nz;
                    f.height.assign(fn, 0.0f); f.wet.assign(fn, 1); f.shore.assign(fn, 0); f.order.assign(fn, 0); f.carved.assign(fn, 0);
                    if (!g.corr.empty()) f.corr.assign(fn, 1.0f);
                    // the band: every fine cell within bandFine of a coarse path cell centre
                    for (const int ci : pr.path) {
                        const int cx = (g.x0 + ci % g.nx) * stride + stride / 2, cz = (g.z0 + ci / g.nx) * stride + stride / 2;
                        for (int dz = -bandFine; dz <= bandFine; ++dz)
                            for (int dx = -bandFine; dx <= bandFine; ++dx) {
                                if (dx * dx + dz * dz > bandFine * bandFine) continue;
                                const int tx = cx + dx, tz = cz + dz;
                                if (!f.contains(tx, tz)) continue;
                                const int fi = f.idx(tx, tz);
                                if (f.wet[fi] != 1 || f.height[fi] != 0.0f) continue;   // already sampled (height 0 with wet 1 = unsampled)
                                const glm::vec2 p = f.cellCentre(tx, tz);
                                f.height[fi] = heightAt(p.x, p.y);
                                f.wet[fi] = levelAt(p.x, p.y) > f.height[fi] + 0.1f ? 1 : 0;
                                f.order[fi] = flow.orderAt(p.x, p.y);
                                f.carved[fi] = flow.carvedRiverAt(p.x, p.y) ? 1 : 0;
                                if (!f.corr.empty()) f.corr[fi] = corridorMulAt ? corridorMulAt(p.x, p.y) : 1.0f;
                                if (f.height[fi] == 0.0f) f.height[fi] = 1e-6f;   // keep the sampled mark distinct
                            }
                    }
                    for (int cz = f.z0; cz < f.z0 + f.nz; ++cz)
                        for (int cx = f.x0; cx < f.x0 + f.nx; ++cx) {
                            const int i = f.idx(cx, cz);
                            if (f.wet[i]) continue;
                            if ((f.contains(cx + 1, cz) && f.wet[f.idx(cx + 1, cz)]) || (f.contains(cx - 1, cz) && f.wet[f.idx(cx - 1, cz)]) ||
                                (f.contains(cx, cz + 1) && f.wet[f.idx(cx, cz + 1)]) || (f.contains(cx, cz - 1) && f.wet[f.idx(cx, cz - 1)]))
                                f.shore[i] = 1;
                        }
                    auto fineCellOf = [&](const glm::vec2& p) {
                        const int cx = glm::clamp(static_cast<int>(std::floor((p.x - ox) / cell)), f.x0, f.x0 + f.nx - 1);
                        const int cz = glm::clamp(static_cast<int>(std::floor((p.y - oz) / cell)), f.z0, f.z0 + f.nz - 1);
                        return f.idx(cx, cz);
                    };
                    const glm::vec2 pStart = d.a >= 0 ? glm::vec2(plan->m_sites[static_cast<size_t>(d.a)].pos) : d.pa;
                    const glm::vec2 pGoal = d.b >= 0 ? glm::vec2(plan->m_sites[static_cast<size_t>(d.b)].pos) : d.pb;
                    int fs = fineCellOf(pStart), fg = fineCellOf(pGoal);
                    // an endpoint cell that sampled wet (a site centre on a pond edge, an exit on a bank)
                    // falls back to the coarse path's first / last cell centre
                    if (f.wet[fs]) fs = fineCellOf(g.cellCentre(g.x0 + pr.path.front() % g.nx, g.z0 + pr.path.front() / g.nx));
                    if (f.wet[fg]) fg = fineCellOf(g.cellCentre(g.x0 + pr.path.back() % g.nx, g.z0 + pr.path.back() / g.nx));
                    float fineCost = 1e30f;
                    const std::vector<int> fine = astarRoute(f, fs, fg, fineCost, capHere);
                    if (!fine.empty()) {
                        pr.pts.reserve(fine.size());
                        for (const int fi : fine) pr.pts.push_back(f.cellCentre(f.x0 + fi % f.nx, f.z0 + fi / f.nx));
                        pr.cost = fineCost;
                    } else {
                        LOG_WARN_FMT("WorldForge", "[WORLDFORGE] demand " << d.a << " -> " << d.b << " kept its coarse line (no fine route inside the band)");
                    }
                }
                pairs.push_back(std::move(pr));
            }
            for (const auto& pr : pairs) chosen.push_back(&pr);
        } else
        for (size_t a = 0; a < nSites; ++a)
            for (size_t b = a + 1; b < nSites; ++b) {
                Pair pr{static_cast<int>(a), static_cast<int>(b), 1e30f, {}};
                pr.path = astarRoute(g, siteCell(plan->m_sites[a]), siteCell(plan->m_sites[b]),
                                     pr.cost);
                if (!pr.path.empty()) pairs.push_back(std::move(pr));
            }
        if (!demand) {
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
        // (`chosen` is declared above the demand branch - a second declaration here shadowed it
        // and threw the MST away: 0 roads on every version-1 plan, caught by the pinned hashes
        // 2026-10-22 right after the M4a commit)
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
        }   // !demand

        // ── Realize each chosen edge: smooth, resample, trim at footprints, mark crossings ───
        for (const Pair* pr : chosen) {
            WorldForgeRoad road;
            road.a = pr->a;
            road.b = pr->b;
            road.cls = pr->cls > 0 ? pr->cls
                                   : std::min(tierRank(plan->m_sites[static_cast<size_t>(pr->a)].tier),
                                              tierRank(plan->m_sites[static_cast<size_t>(pr->b)].tier));
            std::vector<glm::vec2> pts;
            pts.reserve(pr->path.size() + 2);
            pts.push_back(pr->a >= 0 ? glm::vec2(plan->m_sites[static_cast<size_t>(pr->a)].pos) : pr->pa);
            if (!pr->pts.empty()) {
                for (const glm::vec2& p : pr->pts) pts.push_back(p);   // M4b: the refined fine line
            } else
            for (const int ci : pr->path) {
                const int cx = g.x0 + (ci % g.nx), cz = g.z0 + (ci / g.nx);
                pts.push_back(g.cellCentre(cx, cz));
            }
            pts.push_back(pr->b >= 0 ? glm::vec2(plan->m_sites[static_cast<size_t>(pr->b)].pos) : pr->pb);
            if (demand) {   // M4b-3: switchbacks on the pitched stretches of a corridor-driven road
                float legs = 0.0f;
                road.switchbacks = insertSwitchbacks(pts, P.gradeCap[glm::clamp(road.cls, 1, 3)], kSwitchbackBandU, heightAt,
                                                     [&](float x, float z) { return levelAt(x, z) > heightAt(x, z) + 0.1f || flow.carvedRiverAt(x, z); }, legs, &swStatsTotal);
                road.switchbackLengthU = legs;
            }
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
                while (pr->a >= 0 && first + 2 < cl.size() && insideFootprint(cl[first + 1], plan->m_sites[static_cast<size_t>(pr->a)]))
                    ++first;
                size_t last = cl.size() - 1;
                while (pr->b >= 0 && last > first + 1 && insideFootprint(cl[last - 1], plan->m_sites[static_cast<size_t>(pr->b)]))
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
            bool runCarved = false;
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
                        if (ch.carved) runCarved = true;   // M2b-2: a bridge needs a CARVED crossing
                    } else if (runMaxOrder >= 1) {
                        road.crossings.push_back({runPos, runMaxOrder, runCarved});
                        runMaxOrder = 0;
                        runCarved = false;
                    }
                }
            }
            if (runMaxOrder >= 1) road.crossings.push_back({runPos, runMaxOrder, runCarved});
            plan->m_roads.push_back(std::move(road));
        }
    }

    if (demand)
        LOG_INFO_FMT("WorldForge", "[SWITCHBACKS] pitched stretches " << swStatsTotal.stretches << ", replaced " << swStatsTotal.replaced
                     << " (climb " << swStatsTotal.climbReplacedU << " u), kept straight: water " << swStatsTotal.water << ", too short " << swStatsTotal.tooShort
                     << " (climb kept " << swStatsTotal.climbKeptU << " u)");
    // ── Bridge spans (placer #44): every order>=3 crossing gets a flat plank deck ────────────
    // Endpoints are found by marching along the road's local tangent from the crossing point
    // until the CARVE-ACCURATE channel (meander-warped channelAt, not the raw cell line) has
    // been dry for 3 consecutive 1 u steps — the banks — plus a 2 u shoulder onto each. The
    // deck is FLAT at the higher bank's REAL emitted surface (surfaceYAt = sampleColumn), so
    // the deck meets the road the generator actually produces. A channel too wide to clear
    // within 96 u yields NO deck — surfaced in the log, never a half-bridge.
    for (const auto& road : plan->m_roads) {
        for (const auto& cross : road.crossings) {
            if (!cross.carved) continue;   // M2b-2: creeks get no deck (the class, not the order)
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

    // ── Road grade profiles: slope-limited cut envelope + bridge-deck pins ──────────────────
    // Per centerline vertex: start from the REAL emitted surface, take the LOWER envelope
    // with ascent limited to kMaxRoadGrade in both walk directions (cuts through bumps),
    // then raise toward bridge-deck pin cones (approaches climb to MEET the deck instead of
    // stepping onto it — this supersedes the 2026-08-20-morning abutment ramp, which never
    // fired once grading lifted the approach surface). Channel vertices take the lower of
    // their neighbors so a carved bed never drags the profile into the river. Both terms
    // are kMaxRoadGrade-Lipschitz, so their max is too — every step along the graded line
    // is walkable by construction. Consumed by sampleColumn via RoadHit.gradeY.
    {
        // M4b (D26): a corridor-driven (demand) road's envelope slope is its class cap; the pinned
        // version-1 path keeps kMaxRoadGrade. `curCap` is set before every use below.
        auto capOfRoad = [&](const WorldForgeRoad& road) {
            return demand ? P.gradeCap[glm::clamp(road.cls, 1, 3)] : kMaxRoadGrade;
        };
        float curCap = kMaxRoadGrade;
        auto stepLimOf = [&curCap](const std::vector<glm::vec2>& cl, size_t i, size_t j) {
            return curCap * glm::length(cl[j] - cl[i]);
        };
        auto lowerEnvelope = [&](const std::vector<glm::vec2>& cl, std::vector<float>& h) {
            const size_t n = h.size();
            for (size_t i = 1; i < n; ++i)
                h[i] = std::min(h[i], h[i - 1] + stepLimOf(cl, i - 1, i));
            for (size_t i = n - 1; i-- > 0;)
                h[i] = std::min(h[i], h[i + 1] + stepLimOf(cl, i, i + 1));
        };
        for (auto& road : plan->m_roads) {
            const auto& cl = road.centerline;
            const size_t n = cl.size();
            if (n < 2) continue;
            curCap = capOfRoad(road);
            std::vector<float> h(n);
            std::vector<bool> wet(n, false);
            for (size_t i = 0; i < n; ++i) {
                h[i] = static_cast<float>(surfaceYAt(static_cast<int>(std::lround(cl[i].x)),
                                                     static_cast<int>(std::lround(cl[i].y))));
                const FlowField::ChannelHit ch = channelAt(cl[i].x, cl[i].y);
                wet[i] = ch.hit && ch.depth >= 0.15f;
            }
            road.naturalY = h;   // M4b: the pre-grading surface (earthwork = profile - this)
            // A carved-bed vertex must not pull the envelope down: borrow the nearest dry
            // height.
            for (size_t i = 0; i < n; ++i) {
                if (!wet[i]) continue;
                float dry = -1e30f;
                for (size_t j = 1; j < n; ++j) {
                    if (i >= j && !wet[i - j]) { dry = h[i - j]; break; }
                    if (i + j < n && !wet[i + j]) { dry = h[i + j]; break; }
                }
                if (dry > -1e29f) h[i] = dry;
            }
            lowerEnvelope(cl, h);
            road.gradeProfile = std::move(h);
        }
        // M4b-2: corridor-driven plans blend the grade field at realization (gradeBlendAt), so the
        // O(roads^2 x verts^2) junction reconciliation below is the version-1 path's only (it was
        // the box plan's cost driver at 30+ roads, measured 2026-10-22: 95-135 s Debug).
        if (!demand)
        // Junction reconciliation: where two corridors overlap, the SINGLE emitted surface
        // takes the nearest road's grade — so both profiles must agree there or a walker
        // crossing the nearest-road boundary steps the disagreement (measured 2 cubes at a
        // mountain junction, red). Both take the LOWER height (min of Lipschitz functions
        // stays Lipschitz), then the envelope re-limits each road.
        for (size_t r1 = 0; r1 < plan->m_roads.size(); ++r1) {
            auto& roadA = plan->m_roads[r1];
            for (size_t r2 = r1 + 1; r2 < plan->m_roads.size(); ++r2) {
                auto& roadB = plan->m_roads[r2];
                const float reach = roadHalfWidth(roadA.cls) + roadHalfWidth(roadB.cls) + 3.0f;
                for (size_t i = 0; i < roadA.centerline.size(); ++i) {
                    // Nearest point of B to A's vertex i.
                    float best = 1e30f;
                    size_t bj = 0;
                    float bt = 0.0f;
                    for (size_t j = 0; j + 1 < roadB.centerline.size(); ++j) {
                        const glm::vec2 a = roadB.centerline[j], b = roadB.centerline[j + 1];
                        const glm::vec2 ab = b - a;
                        const float len2 = glm::dot(ab, ab);
                        const float t = len2 > 0.0f
                                            ? glm::clamp(glm::dot(roadA.centerline[i] - a, ab) /
                                                             len2, 0.0f, 1.0f)
                                            : 0.0f;
                        const float d = glm::length(roadA.centerline[i] - (a + ab * t));
                        if (d < best) { best = d; bj = j; bt = t; }
                    }
                    if (best > reach || roadB.gradeProfile.size() <= bj + 1 ||
                        roadA.gradeProfile.size() <= i)
                        continue;
                    const float hB = roadB.gradeProfile[bj] * (1.0f - bt) +
                                     roadB.gradeProfile[bj + 1] * bt;
                    const float m = std::min(roadA.gradeProfile[i], hB);
                    const float capAB = std::min(capOfRoad(roadA), capOfRoad(roadB));
                    roadA.gradeProfile[i] = m;
                    roadB.gradeProfile[bj] = std::min(roadB.gradeProfile[bj],
                                                      m + capAB * bt * glm::length(
                                                              roadB.centerline[bj + 1] -
                                                              roadB.centerline[bj]));
                    roadB.gradeProfile[bj + 1] = std::min(
                        roadB.gradeProfile[bj + 1],
                        m + capAB * (1.0f - bt) *
                                glm::length(roadB.centerline[bj + 1] - roadB.centerline[bj]));
                }
            }
        }
        for (auto& road : plan->m_roads) {
            if (road.gradeProfile.size() < 2) continue;
            curCap = capOfRoad(road);
            lowerEnvelope(road.centerline, road.gradeProfile);
            if (demand && road.naturalY.size() == road.gradeProfile.size()) {
                // M4b bounded earthwork: the class-capped envelope may not cut deeper than the
                // road's cutLimit (lateral room x repose) - where it would, the profile stays at
                // that floor and the grade becomes a SHORT PITCH steeper than the cap (USFS: short
                // pitches steeper than the ruling grade), limited to the legacy 0.5 by a second
                // envelope so every step stays walkable. Pitches are reported, never silent:
                // RouteFieldTest counts the over-cap length and ties each one to an active floor.
                const float floorU = cutLimit(road.cls);
                for (size_t i = 0; i < road.gradeProfile.size(); ++i)
                    road.gradeProfile[i] = std::max(road.gradeProfile[i], road.naturalY[i] - floorU);
                curCap = kMaxRoadGrade;
                lowerEnvelope(road.centerline, road.gradeProfile);
                curCap = capOfRoad(road);
            }
        }
        plan->m_blendGrades = demand != nullptr;
        // Deck pin cones — pinned by PROJECTION, not vertex-to-span distance: a short span
        // can fall entirely between 16 u vertices and a distance test then pins NOTHING,
        // leaving the old multi-cube deck mount (measured 5 cubes, red). Pin every vertex
        // from the one nearest bank A to the one nearest bank B, banks included, so the
        // approach fills up to the deck; the cone spreads outward at the grade limit and
        // the final profile is the max of envelope and cone.
        for (auto& road : plan->m_roads) {
            const auto& cl = road.centerline;
            const size_t n = cl.size();
            if (n < 2 || road.gradeProfile.size() != n) continue;
            curCap = capOfRoad(road);
            // TWO-SIDED pins: a low bank FILLS up to the deck (lower cone, max) and a high
            // approach CUTS down to it (upper cone, min) — a bank 5 above the deck was a
            // 5-cube drop onto the span, measured red. clamp of Lipschitz functions stays
            // Lipschitz, so every step remains within the grade limit.
            std::vector<float> pin(n, -1e30f), cap(n, 1e30f);
            for (const auto& span : plan->m_bridges) {
                auto nearestVert = [&](const glm::vec2& p, float maxD) -> int {
                    float best = maxD;
                    int bi = -1;
                    for (size_t i = 0; i < n; ++i) {
                        const float d = glm::length(cl[i] - p);
                        if (d < best) { best = d; bi = static_cast<int>(i); }
                    }
                    return bi;
                };
                const int ia = nearestVert(span.a, 32.0f), ib = nearestVert(span.b, 32.0f);
                if (ia < 0 || ib < 0) continue;   // span belongs to another road
                for (int i = std::min(ia, ib); i <= std::max(ia, ib); ++i) {
                    pin[i] = std::max(pin[i], span.deckY);
                    cap[i] = std::min(cap[i], span.deckY);
                }
            }
            auto stepLim = [&](size_t i, size_t j) { return stepLimOf(cl, i, j); };
            for (size_t i = 1; i < n; ++i) {
                pin[i] = std::max(pin[i], pin[i - 1] - stepLim(i - 1, i));
                cap[i] = std::min(cap[i], cap[i - 1] + stepLim(i - 1, i));
            }
            for (size_t i = n - 1; i-- > 0;) {
                pin[i] = std::max(pin[i], pin[i + 1] - stepLim(i, i + 1));
                cap[i] = std::min(cap[i], cap[i + 1] + stepLim(i, i + 1));
            }
            for (size_t i = 0; i < n; ++i)
                road.gradeProfile[i] =
                    std::min(std::max(road.gradeProfile[i], pin[i]), cap[i]);
        }
    }

    // ── Road raster: nearest-segment index per 8 u cell over the network bbox ────────────────
    size_t truncated = 0;
    for (size_t r = 0; r < plan->m_roads.size(); ++r) {
        const auto& cl = plan->m_roads[r].centerline;
        for (size_t i = 0; i + 1 < cl.size(); ++i) {
            if (plan->m_segments.size() >= g_rasterSegmentCap) { ++truncated; continue; }   // M0: counted, never silent
            plan->m_segments.push_back({cl[i], cl[i + 1], static_cast<uint16_t>(r),
                                        static_cast<uint8_t>(plan->m_roads[r].cls),
                                        static_cast<uint16_t>(i)});
        }
    }
    plan->m_rasterTruncatedSegments = truncated;
    if (truncated > 0)
        LOG_WARN_FMT("WorldForge", "[WORLDFORGE] road raster segment cap (" << g_rasterSegmentCap
                     << ") reached: " << truncated << " segment(s) are NOT indexed - roads past the cap are"
                     " invisible to roadAt (fewer roads, or a wider raster index, before this plan is used)");
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
        struct RoadHit { uint32_t cell; uint16_t road; uint16_t seg; float d; };
        std::vector<RoadHit> roadHits;   // M4b-2 audit: every (cell, road) within the blend's reach
        for (size_t si = 0; si < plan->m_segments.size(); ++si) {
            const auto& s = plan->m_segments[si];
            const float reach = roadHalfWidth(s.cls) + 2.0f * rcell;
            // the blend's reach: half-width + the widest repose shoulder + a cell of slack
            const float blendReach = roadHalfWidth(s.cls) + kShoulderMaxU + rcell;
            {
                const glm::vec2 blo = glm::min(s.a, s.b) - glm::vec2(blendReach);
                const glm::vec2 bhi = glm::max(s.a, s.b) + glm::vec2(blendReach);
                const int bx0 = std::max(0, static_cast<int>((blo.x - R.originX) / rcell));
                const int bz0 = std::max(0, static_cast<int>((blo.y - R.originZ) / rcell));
                const int bx1 = std::min(R.cellsX - 1, static_cast<int>((bhi.x - R.originX) / rcell));
                const int bz1 = std::min(R.cellsZ - 1, static_cast<int>((bhi.y - R.originZ) / rcell));
                for (int iz = bz0; iz <= bz1; ++iz)
                    for (int ix = bx0; ix <= bx1; ++ix) {
                        const glm::vec2 c(R.originX + (ix + 0.5f) * rcell, R.originZ + (iz + 0.5f) * rcell);
                        const float d = distToSegment(c, s.a, s.b);
                        if (d > blendReach) continue;
                        roadHits.push_back({static_cast<uint32_t>(static_cast<size_t>(iz) * R.cellsX + ix), s.roadIdx, static_cast<uint16_t>(si), d});
                    }
            }
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
        // the per-cell road index: one entry per (cell, road) = that road's nearest segment
        std::sort(roadHits.begin(), roadHits.end(), [](const RoadHit& a, const RoadHit& b) {
            if (a.cell != b.cell) return a.cell < b.cell;
            if (a.road != b.road) return a.road < b.road;
            if (a.d != b.d) return a.d < b.d;
            return a.seg < b.seg;
        });
        R.cellRoadsStart.assign(R.nearestSeg.size() + 1, 0);
        R.cellRoadSeg.clear();
        R.cellRoadSeg.reserve(roadHits.size() / 4 + 1);
        {
            size_t k = 0;
            for (size_t cell = 0; cell < R.nearestSeg.size(); ++cell) {
                R.cellRoadsStart[cell] = static_cast<uint32_t>(R.cellRoadSeg.size());
                uint16_t lastRoad = 0xFFFF;
                while (k < roadHits.size() && roadHits[k].cell == cell) {
                    if (roadHits[k].road != lastRoad) { R.cellRoadSeg.push_back(roadHits[k].seg); lastRoad = roadHits[k].road; }
                    ++k;
                }
            }
            R.cellRoadsStart[R.nearestSeg.size()] = static_cast<uint32_t>(R.cellRoadSeg.size());
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
        const glm::vec2 ab = b.b - b.a;
        const float len2 = glm::dot(ab, ab);
        if (len2 <= 0.0f) continue;
        const float len = std::sqrt(len2);
        const glm::vec2 dirn = ab / len;
        const float tRaw = glm::dot(p - b.a, ab) / len2;
        const float lateral = std::abs(glm::dot(p - b.a, glm::vec2(-dirn.y, dirn.x)));
        const float half = roadHalfWidth(b.cls);
        if (lateral > half) continue;

        // Beyond a span end: no deck (the old clamped-distance test grew a floating deck
        // DISC around each endpoint). Mounting a deck from a low bank is the GRADE
        // PROFILE's job — deck pins raise the approach surfaceY to meet the deck
        // (superseding the short-lived per-column abutment ramp).
        if (tRaw < 0.0f || tRaw > 1.0f) continue;

        hit.deckY = b.deckY;
        hit.cls = b.cls;
        // Deck-edge band -> parapet, from TRUE lateral distance on the span INTERIOR only
        // (an endpoint-arc band would be a parapet ACROSS the walkway entrance — the red
        // test's walkway-violation assertion caught exactly this).
        hit.rail = lateral > half - 1.0f;
        // Piers: interior stations every ~kPierSpacing along the span, DERIVED here per
        // query (nothing baked, planHash unchanged — pre-pier ledgers stay valid). A span
        // shorter than 2x the spacing carries no station.
        const int n = std::max(0, static_cast<int>(std::floor(len / kPierSpacing)) - 1);
        if (n > 0 && lateral <= 1.5f) {
            const float along = tRaw * len;
            for (int i = 0; i < n; ++i) {
                const float station = len * static_cast<float>(i + 1) / static_cast<float>(n + 1);
                if (std::abs(along - station) <= 1.5f) {
                    hit.pier = true;
                    break;
                }
            }
        }
        return hit;
    }
    return hit;
}

WorldForgePlan::GradeBlend WorldForgePlan::gradeBlendAt(float worldX, float worldZ, float naturalY, std::vector<int>* roadsOut) const {
    GradeBlend out;
    constexpr bool kAuditFixes = true;   // audit 2026-10-26: both road slots, the repose shoulder, the core road (false = the audited behaviour, red)
    if (roadsOut) roadsOut->clear();
    if (m_raster.cellsX <= 0 || m_raster.cellsZ <= 0) return out;
    const int cx = static_cast<int>(std::floor((worldX - m_raster.originX) / m_raster.cellSize));
    const int cz = static_cast<int>(std::floor((worldZ - m_raster.originZ) / m_raster.cellSize));
    const glm::vec2 p(worldX, worldZ);
    // candidate roads: every road the point's own raster cell indexes (the index reach covers
    // half-width + the widest shoulder + a cell), each with its nearest-segment hint
    int roads[64], hints[64]; int nRoads = 0;
    auto addRoad = [&](uint16_t segIdx) {
        if (segIdx == 0xFFFF) return;
        const int r = m_segments[segIdx].roadIdx;
        for (int k = 0; k < nRoads; ++k) if (roads[k] == r) return;
        if (nRoads < 64) { roads[nRoads] = r; hints[nRoads] = segIdx; ++nRoads; }
    };
    if (cx < 0 || cz < 0 || cx >= m_raster.cellsX || cz >= m_raster.cellsZ) return out;
    {
        const size_t ci = static_cast<size_t>(cz) * m_raster.cellsX + cx;
        addRoad(m_raster.nearestSeg[ci]);
        if (kAuditFixes && !m_raster.cellRoadsStart.empty())
            for (uint32_t e = m_raster.cellRoadsStart[ci]; e < m_raster.cellRoadsStart[ci + 1]; ++e) addRoad(m_raster.cellRoadSeg[e]);
    }
    // Rule: where a point lies INSIDE a corridor the surface is that road's profile — the
    // highest class when corridors share a trunk (the trunk carries the highway's cap; a mean of
    // two profiles would exceed the stricter cap). Shoulders blend in around it; a cut's bank is
    // carried by a shoulder widened to cut / repose so the bank stands at the repose slope.
    float coreGrade = -1e30f; int coreCls = 0, coreRoad = INT_MAX;
    float wSum = 0.0f, gSum = 0.0f, wMax = 0.0f;
    for (int k = 0; k < nRoads; ++k) {
        float best = 1e30f, gradeY = -1e30f; int cls = 0;
        // the road's nearest segment: the hint and its neighbours along the polyline (the hint
        // is the road's nearest segment to the CELL centre; the point is within a cell of it)
        for (int di = -3; di <= 3; ++di) {
            const int i = hints[k] + di;
            if (i < 0 || i >= static_cast<int>(m_segments.size())) continue;
            const Segment& s = m_segments[static_cast<size_t>(i)];
            if (s.roadIdx != roads[k]) continue;
            const glm::vec2 ab = s.b - s.a;
            const float len2 = glm::dot(ab, ab);
            const float t = len2 > 0.0f ? glm::clamp(glm::dot(p - s.a, ab) / len2, 0.0f, 1.0f) : 0.0f;
            const float d = glm::length(p - (s.a + ab * t));
            if (d < best) {
                best = d; cls = s.cls;
                const auto& prof = m_roads[static_cast<size_t>(roads[k])].gradeProfile;
                gradeY = (s.vert + 1 < prof.size()) ? prof[s.vert] * (1.0f - t) + prof[s.vert + 1] * t : -1e30f;
            }
        }
        if (cls <= 0 || gradeY <= -1e29f) continue;
        const float half = roadHalfWidth(cls);
        const float shoulder = kAuditFixes ? std::min(kShoulderMaxU, std::max(kShoulderU, std::fabs(gradeY - naturalY) / kReposeSlope)) : kShoulderU;
        const float w = best <= half ? 1.0f : std::max(0.0f, 1.0f - (best - half) / shoulder);
        if (w <= 0.0f) continue;
        if (roadsOut) roadsOut->push_back(roads[k]);
        if (best <= half && (cls > coreCls || (cls == coreCls && roads[k] < coreRoad))) { coreCls = cls; coreRoad = roads[k]; coreGrade = gradeY; }
        wSum += w; gSum += w * gradeY; wMax = std::max(wMax, w);
    }
    if (wSum <= 0.0f) return out;
    out.gradeY = (kAuditFixes && coreCls > 0) ? coreGrade : gSum / wSum;
    out.weight = wMax;
    out.coreRoad = (kAuditFixes && coreCls > 0) ? coreRoad : -1;
    return out;
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
            // Graded road surface at the projection: linear between the segment's two
            // vertex profile heights (the profile is slope-limited per vertex, so the
            // interpolated line is walkable by construction).
            const auto& prof = m_roads[roadIdx].gradeProfile;
            if (s.vert + 1 < prof.size())
                hit.gradeY = prof[s.vert] * (1.0f - t) + prof[s.vert + 1] * t;
        }
    }
    return hit;
}

void WorldForgePlan::setRasterSegmentCapForTesting(size_t cap) {
    g_rasterSegmentCap = cap == 0 ? kRasterSegmentCap : cap;
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
            crossings.push_back({{"x", c.pos.x}, {"z", c.pos.y}, {"order", c.riverOrder}, {"carved", c.carved}});
        // Grade summary (min/max/sum, 0.1-quantized): the profile is part of the plan's
        // identity — grading changes what generation EMITS, so planHash must move with it
        // (the ledger drift guard exists for exactly this) — without bloating the payload.
        nlohmann::json grade = nlohmann::json::array();
        if (!r.gradeProfile.empty()) {
            float lo = 1e30f, hi = -1e30f;
            double sum = 0.0;
            for (const float g : r.gradeProfile) {
                lo = std::min(lo, g);
                hi = std::max(hi, g);
                sum += std::round(g * 10.0f) / 10.0;
            }
            grade = {std::round(lo * 10.0f) / 10.0, std::round(hi * 10.0f) / 10.0,
                     std::round(sum * 10.0) / 10.0};
        }
        roads.push_back({{"a", r.a}, {"b", r.b}, {"class", r.cls}, {"switchbacks", r.switchbacks}, {"switchback_length_u", r.switchbackLengthU},
                         {"points", static_cast<int>(r.centerline.size())},
                         {"centerline", line}, {"crossings", crossings}, {"grade", grade}});
    }
    j["roads"] = roads;
    nlohmann::json bridges = nlohmann::json::array();
    for (const auto& b : m_bridges)
        bridges.push_back({{"ax", b.a.x}, {"az", b.a.y}, {"bx", b.b.x}, {"bz", b.b.y},
                           {"deckY", b.deckY}, {"class", b.cls}, {"order", b.crossingOrder}});
    j["bridges"] = bridges;
    j["raster_truncated_segments"] = m_rasterTruncatedSegments;   // M0: 0 unless the uint16 cap bit
    return j;
}

uint64_t WorldForgePlan::planHash() const {
    // Plan IDENTITY only: diagnostics (raster_truncated_segments) are reported, never hashed.
    // Hashing the whole record moved every plan's hash when that field was added (M0 L4,
    // 2026-10-14: the lab's F7 hash 9693137997320541182 -> 10638243061351388589 on an
    // unchanged recipe), which marks every earlier ledger stale.
    nlohmann::json j = toJson();
    j.erase("raster_truncated_segments");
    // M2b-2 added `carved` to every crossing record (2026-10-20) and silently moved every
    // version-1 plan's hash the same way (Mountains 424242: 18197215997962677593 ->
    // 6997471292175526340 with identical sites/roads/bridges; caught 2026-10-22 by the pinned
    // version-1 hashes in WorldForgePlanTest). The class is derived from the crossing's
    // position, so it is reported, never hashed.
    if (j.contains("roads"))
        for (auto& road : j["roads"]) {
            road.erase("switchbacks");           // M4b-3 diagnostics: reported, never hashed (the pinned
            road.erase("switchback_length_u");   // version-1 hashes moved when they were added, 2026-10-26)
            if (road.contains("crossings"))
                for (auto& c : road["crossings"]) c.erase("carved");
        }
    const std::string s = j.dump();
    uint64_t h = 1469598103934665603ull;  // FNV-1a 64
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace Phyxel
