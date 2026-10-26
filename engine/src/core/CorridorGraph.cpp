#include "core/CorridorGraph.h"

#include "core/CivPlan.h"
#include "core/WorldForgePlan.h"
#include "core/WorldSheet.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <queue>
#include <set>

namespace Phyxel {

namespace {

// The V1 road cost model REUSED at the macro cell (WorldForgePlan.cpp: (1 + 15·grade)², river /
// shore penalties); the macro cell is 512 u, so a "river" cell is a crossing of a ≥ 0.5 km²
// drainage channel and the penalty buys the bridge M4c will place.
constexpr float kSlopeAversion = 15.0f;
constexpr float kPenaltyRiver = 30.0f;
constexpr float kPenaltyShore = 5.0f;
constexpr float kCoastStripU = 2.0f;   // the civ plan's land rule (macro base above the beach band)

struct Grid {
    int n = 0;
    float cellU = 512.0f, originU = 0.0f;
    std::vector<float> h;
    std::vector<uint8_t> blocked, river, shore;
    std::vector<uint8_t> onNet;   // cells already on the network (the reuse discount)
    glm::vec2 centre(int c) const { return {originU + (c % n + 0.5f) * cellU, originU + (c / n + 0.5f) * cellU}; }
};

std::vector<int> astar(const Grid& g, int start, int goal, float reuse, float& outCost, float& outMaxGrade) {
    outCost = 1e30f; outMaxGrade = 0.0f;
    const int N = g.n * g.n;
    if (start < 0 || goal < 0 || start >= N || goal >= N) return {};
    if (g.blocked[static_cast<size_t>(start)] || g.blocked[static_cast<size_t>(goal)]) return {};
    std::vector<float> gc(static_cast<size_t>(N), 1e30f);
    std::vector<int> parent(static_cast<size_t>(N), -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    const int gx = goal % g.n, gz = goal / g.n;
    auto heur = [&](int i) {
        const float dx = static_cast<float>(i % g.n - gx), dz = static_cast<float>(i / g.n - gz);
        return std::sqrt(dx * dx + dz * dz) * reuse;   // admissible under the discount
    };
    gc[static_cast<size_t>(start)] = 0.0f;
    open.push({heur(start), start});
    static const int NB[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    while (!open.empty()) {
        const auto [f, cur] = open.top(); open.pop();
        if (cur == goal) break;
        if (f > gc[static_cast<size_t>(cur)] + heur(cur) + 1e-4f) continue;
        const int cx = cur % g.n, cz = cur / g.n;
        for (const auto& nb : NB) {
            const int tx = cx + nb[0], tz = cz + nb[1];
            if (tx < 0 || tz < 0 || tx >= g.n || tz >= g.n) continue;
            const int ti = tz * g.n + tx;
            if (g.blocked[static_cast<size_t>(ti)]) continue;
            const float stepLen = (nb[0] && nb[1]) ? 1.41421356f : 1.0f;
            const float grade = std::fabs(g.h[static_cast<size_t>(ti)] - g.h[static_cast<size_t>(cur)]) / (stepLen * g.cellU);
            float c = stepLen * (1.0f + kSlopeAversion * grade) * (1.0f + kSlopeAversion * grade);
            if (g.river[static_cast<size_t>(ti)]) c += kPenaltyRiver;
            else if (g.shore[static_cast<size_t>(ti)]) c += kPenaltyShore;
            if (g.onNet[static_cast<size_t>(ti)]) c *= reuse;   // Galin / Veloren: reuse what exists
            if (gc[static_cast<size_t>(cur)] + c < gc[static_cast<size_t>(ti)] - 1e-6f) {
                gc[static_cast<size_t>(ti)] = gc[static_cast<size_t>(cur)] + c;
                parent[static_cast<size_t>(ti)] = cur;
                open.push({gc[static_cast<size_t>(ti)] + heur(ti), ti});
            }
        }
    }
    if (gc[static_cast<size_t>(goal)] >= 1e30f) return {};
    outCost = gc[static_cast<size_t>(goal)];
    std::vector<int> path;
    for (int i = goal; i != -1; i = parent[static_cast<size_t>(i)]) path.push_back(i);
    std::reverse(path.begin(), path.end());
    for (size_t k = 1; k < path.size(); ++k) {
        const int a = path[k - 1], b = path[k];
        const bool diag = (a % g.n != b % g.n) && (a / g.n != b / g.n);
        const float run = (diag ? 1.41421356f : 1.0f) * g.cellU;
        outMaxGrade = std::max(outMaxGrade, std::fabs(g.h[static_cast<size_t>(b)] - g.h[static_cast<size_t>(a)]) / run);
    }
    return path;
}

std::vector<glm::vec2> chaikin(std::vector<glm::vec2> pts, int passes) {
    for (int pass = 0; pass < passes && pts.size() >= 3; ++pass) {
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
    return pts;
}

}  // namespace

const char* CorridorGraph::className(int cls) {
    static const char* names[4] = {"none", "track", "road", "highway"};
    return (cls >= 0 && cls < 4) ? names[cls] : "?";
}

std::shared_ptr<const CorridorGraph> CorridorGraph::bake(const Inputs& in, const WorldForgeParams& paramsIn) {
    auto out = std::shared_ptr<CorridorGraph>(new CorridorGraph());
    const auto t0 = std::chrono::steady_clock::now();
    if (!in.macro || !in.civ) return out;
    const WorldForgeParams P = paramsIn.clamped();
    const MacroSheet& M = *in.macro;
    const int n = M.cells();
    const size_t N = static_cast<size_t>(n) * n;
    out->m_cells = n; out->m_cellU = M.cellU(); out->m_originU = M.originU();
    out->m_cellClass.assign(N, 0);
    out->m_cellSegs.resize(N);

    Grid g;
    g.n = n; g.cellU = M.cellU(); g.originU = M.originU();
    g.h.resize(N); g.blocked.assign(N, 0); g.river.assign(N, 0); g.shore.assign(N, 0); g.onNet.assign(N, 0);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const MacroCell& c = M.cell(i, j);
            const size_t k = static_cast<size_t>(j) * n + i;
            g.h[k] = c.baseHeight;
            const bool sub = c.baseHeight <= in.seaLevel;
            const bool basin = c.filled > c.baseHeight + 1e-3f;
            g.blocked[k] = (sub || basin) ? 1 : 0;   // sea and lake floors are never a corridor
            g.river[k] = (!sub && c.drainageArea >= in.macroRiverAreaM2) ? 1 : 0;
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const size_t k = static_cast<size_t>(j) * n + i;
            if (g.blocked[k]) continue;
            const bool nearSea = (i > 0 && M.cell(i - 1, j).baseHeight <= in.seaLevel + kCoastStripU) ||
                                 (i + 1 < n && M.cell(i + 1, j).baseHeight <= in.seaLevel + kCoastStripU) ||
                                 (j > 0 && M.cell(i, j - 1).baseHeight <= in.seaLevel + kCoastStripU) ||
                                 (j + 1 < n && M.cell(i, j + 1).baseHeight <= in.seaLevel + kCoastStripU);
            g.shore[k] = nearSea ? 1 : 0;
        }

    // ── Demand, highest class first ────────────────────────────────────────────────────────
    const auto& sites = in.civ->sites();
    auto live = [&](int id) { return id >= 0 && id < static_cast<int>(sites.size()) && sites[static_cast<size_t>(id)].state != CivPlan::Abandoned; };
    auto classFor = [&](int a, int b) {
        const int t = std::max(sites[static_cast<size_t>(a)].tier, sites[static_cast<size_t>(b)].tier);
        if (t <= CivPlan::City) return static_cast<int>(Highway);
        if (t <= CivPlan::Village) return static_cast<int>(Road);
        return static_cast<int>(Track);
    };
    struct Demand { int a, b, cls; };
    std::vector<Demand> demand;
    // seats to each other: a chain by nearest unvisited seat from the first city (a deterministic
    // spanning order; the reuse discount then merges parallel runs), plus every city pair
    std::vector<int> seats;
    for (const auto& pr : in.civ->provinces()) if (live(pr.seat)) seats.push_back(pr.seat);
    if (seats.size() >= 2) {
        std::vector<uint8_t> done(seats.size(), 0);
        size_t cur = 0; done[0] = 1;
        for (size_t step = 1; step < seats.size(); ++step) {
            float best = 1e30f; size_t bi = 0;
            for (size_t k = 0; k < seats.size(); ++k) {
                if (done[k]) continue;
                const float d = glm::length(sites[static_cast<size_t>(seats[k])].pos - sites[static_cast<size_t>(seats[cur])].pos);
                if (d < best) { best = d; bi = k; }
            }
            demand.push_back({seats[cur], seats[bi], classFor(seats[cur], seats[bi])});
            done[bi] = 1; cur = bi;
        }
        for (size_t x = 0; x < seats.size(); ++x)
            for (size_t y = x + 1; y < seats.size(); ++y)
                if (sites[static_cast<size_t>(seats[x])].tier == CivPlan::City && sites[static_cast<size_t>(seats[y])].tier == CivPlan::City)
                    demand.push_back({seats[x], seats[y], Highway});
    }
    // every other live site to its province seat
    for (const auto& pr : in.civ->provinces())
        for (int m : pr.members)
            if (m != pr.seat && live(m) && live(pr.seat)) demand.push_back({m, pr.seat, classFor(m, pr.seat)});
    std::stable_sort(demand.begin(), demand.end(), [](const Demand& x, const Demand& y) { return x.cls > y.cls; });

    // ── Route in that order with the reuse discount ────────────────────────────────────────
    std::set<std::pair<int, int>> seen;
    for (const Demand& d : demand) {
        const auto key = std::make_pair(std::min(d.a, d.b), std::max(d.a, d.b));
        if (!seen.insert(key).second) continue;
        float cost = 0.0f, maxGrade = 0.0f;
        const int ca = sites[static_cast<size_t>(d.a)].cell, cb = sites[static_cast<size_t>(d.b)].cell;
        std::vector<int> path = astar(g, ca, cb, P.routeReuseFraction, cost, maxGrade);
        if (path.empty()) {
            ++out->m_unroutable;
            out->m_refusals.push_back("no corridor from site " + std::to_string(d.a) + " to site " + std::to_string(d.b) + " (sea or basin in every way)");
            continue;
        }
        Route r;
        r.a = d.a; r.b = d.b; r.cls = d.cls; r.cost = cost; r.maxGrade = maxGrade;
        int reused = 0;
        std::vector<glm::vec2> pts;
        pts.reserve(path.size());
        for (size_t k = 0; k < path.size(); ++k) {
            const size_t c = static_cast<size_t>(path[k]);
            if (g.onNet[c]) ++reused;
            g.onNet[c] = 1;
            out->m_cellClass[c] = static_cast<uint8_t>(std::max<int>(out->m_cellClass[c], d.cls));
            pts.push_back(g.centre(path[k]));
            if (k) r.lengthU += glm::length(pts[k] - pts[k - 1]);
        }
        r.reusedFraction = path.empty() ? 0.0f : static_cast<float>(reused) / static_cast<float>(path.size());
        r.cells = std::move(path);
        r.polyline = chaikin(pts, 2);
        out->m_totalLengthU += r.lengthU;
        out->m_routes.push_back(std::move(r));
    }

    // ── The per-cell segment index (D28 stamping + the box band), the shared / junction census ─
    for (size_t ri = 0; ri < out->m_routes.size(); ++ri) {
        const auto& r = out->m_routes[ri];
        for (size_t k = 0; k + 1 < r.polyline.size(); ++k) {
            const int si = static_cast<int>(out->m_segs.size());
            out->m_segs.push_back({r.polyline[k], r.polyline[k + 1], r.cls, static_cast<int>(ri)});
            const glm::vec2 lo = glm::min(r.polyline[k], r.polyline[k + 1]), hi = glm::max(r.polyline[k], r.polyline[k + 1]);
            const int i0 = std::max(0, static_cast<int>(std::floor((lo.x - g.originU) / g.cellU)) - 1);
            const int j0 = std::max(0, static_cast<int>(std::floor((lo.y - g.originU) / g.cellU)) - 1);
            const int i1 = std::min(n - 1, static_cast<int>(std::floor((hi.x - g.originU) / g.cellU)) + 1);
            const int j1 = std::min(n - 1, static_cast<int>(std::floor((hi.y - g.originU) / g.cellU)) + 1);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) out->m_cellSegs[static_cast<size_t>(j) * n + i].push_back(si);
        }
    }
    std::vector<int> useCount(N, 0);
    for (const auto& r : out->m_routes) for (int c : r.cells) ++useCount[static_cast<size_t>(c)];
    for (size_t k = 0; k < N; ++k) if (useCount[k] >= 2) ++out->m_sharedCells;
    for (size_t k = 0; k < N; ++k) {
        if (!out->m_cellClass[k]) continue;
        int deg = 0;
        const int i = static_cast<int>(k % n), j = static_cast<int>(k / n);
        for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) {
            if (!di && !dj) continue;
            const int ni = i + di, nj = j + dj;
            if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
            if (out->m_cellClass[static_cast<size_t>(nj) * n + ni]) ++deg;
        }
        if (deg >= 3) ++out->m_junctionCells;
    }
    out->m_bakeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

int CorridorGraph::classAt(float worldX, float worldZ) const {
    if (m_cells <= 0) return None;
    const int i = static_cast<int>(std::floor((worldX - m_originU) / m_cellU));
    const int j = static_cast<int>(std::floor((worldZ - m_originU) / m_cellU));
    if (i < 0 || j < 0 || i >= m_cells || j >= m_cells) return None;
    return m_cellClass[static_cast<size_t>(j) * m_cells + i];
}

float CorridorGraph::distanceToCorridorU(float worldX, float worldZ, int* clsOut, int* routeOut) const {
    if (clsOut) *clsOut = None;
    if (routeOut) *routeOut = -1;
    if (m_cells <= 0) return 1e30f;
    const int i = static_cast<int>(std::floor((worldX - m_originU) / m_cellU));
    const int j = static_cast<int>(std::floor((worldZ - m_originU) / m_cellU));
    if (i < 0 || j < 0 || i >= m_cells || j >= m_cells) return 1e30f;
    const glm::vec2 p(worldX, worldZ);
    // Two routes sharing a trunk have separately smoothed polylines tens of u apart, so the
    // NEAREST segment's class is not the class of the road under the point (audit 2026-10-22):
    // the class is the highest one whose own half-width reaches the point; the distance returned
    // is to that segment (the nearest segment when none reaches).
    float best = 1e30f; int bestCls = None, bestRoute = -1;
    float within = 1e30f; int withinCls = None, withinRoute = -1;
    for (int si : m_cellSegs[static_cast<size_t>(j) * m_cells + i]) {
        const Seg& s = m_segs[static_cast<size_t>(si)];
        const glm::vec2 ab = s.b - s.a;
        const float len2 = glm::dot(ab, ab);
        const float t = len2 > 0.0f ? glm::clamp(glm::dot(p - s.a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        const float d = glm::length(p - (s.a + ab * t));
        if (d < best || (d == best && s.cls > bestCls)) { best = d; bestCls = s.cls; bestRoute = s.route; }
        if (d <= WorldForgePlan::roadHalfWidth(s.cls) && (s.cls > withinCls || (s.cls == withinCls && d < within))) { within = d; withinCls = s.cls; withinRoute = s.route; }
    }
    if (withinCls > None) { if (clsOut) *clsOut = withinCls; if (routeOut) *routeOut = withinRoute; return within; }
    if (clsOut) *clsOut = bestCls;
    if (routeOut) *routeOut = bestRoute;
    return best;
}

nlohmann::json CorridorGraph::toJson() const {
    nlohmann::json j;
    j["routes_count"] = m_routes.size();
    j["total_length_u"] = m_totalLengthU;
    j["shared_cells"] = m_sharedCells;
    j["junction_cells"] = m_junctionCells;
    j["unroutable"] = m_unroutable;
    j["bake_ms"] = m_bakeMs;
    nlohmann::json routes = nlohmann::json::array();
    for (const auto& r : m_routes) {
        nlohmann::json line = nlohmann::json::array();
        for (size_t k = 0; k < r.polyline.size(); k += 4) line.push_back({r.polyline[k].x, r.polyline[k].y});   // decimated
        if (!r.polyline.empty() && (r.polyline.size() - 1) % 4) line.push_back({r.polyline.back().x, r.polyline.back().y});
        routes.push_back({{"a", r.a}, {"b", r.b}, {"class", className(r.cls)}, {"length_u", r.lengthU}, {"max_grade", r.maxGrade},
                          {"cost", r.cost}, {"reused_fraction", r.reusedFraction}, {"cells", r.cells.size()}, {"polyline", line}});
    }
    j["routes"] = routes;
    j["refusals"] = m_refusals;
    return j;
}

}  // namespace Phyxel
