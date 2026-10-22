#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "core/WorldForgeParams.h"

namespace Phyxel {

class MacroSheet;
class CivPlan;

// ── M4a: the continent corridor graph (docs/WorldForgeV2Plan.md M4, D25 / D27 / D28) ────────────
//
// Routes between the civ plan's LIVE sites, solved on the MACRO sheet (512 u cells) as corridors:
// demand = every site to its province seat (M3b) and the seats to each other; the cost field is
// the V1 road cost reused at the macro cell (length × (1 + 15·grade)², river / shore penalties,
// sea and basins impassable) with Galin's REUSE DISCOUNT — a step onto a cell already on the
// network costs `routeReuseFraction` of its price (Veloren 1/3, REASONED), so later routes merge
// into earlier trunks and the hierarchy forms by construction. Routes are solved highest class
// first (highway between cities, road to towns and villages, track to hamlets and outposts), each
// route's class by the lower of its two ends' tiers; a cell's class is the highest route using it.
// The graph is data (cells + polylines + classes): the hydrology box's plan routes its fine roads
// ALONG these corridors (a cost discount inside the corridor band), and outside the box the
// column sampler stamps the corridor at macro fidelity (material + drape, no grade, no crossings —
// D28), so a route is continuous across the continent. Pure in (sheet, civ plan, params).
class CorridorGraph {
public:
    enum Class : int { None = 0, Track = 1, Road = 2, Highway = 3 };
    static const char* className(int cls);

    struct Route {
        int a = -1, b = -1;             // civ site ids
        int cls = Track;
        std::vector<int> cells;         // row-major macro cells, a → b
        std::vector<glm::vec2> polyline;   // world XZ, Chaikin-smoothed cell centres (for drawing + stamping)
        float lengthU = 0.0f;           // along the cell path
        float maxGrade = 0.0f;          // steepest macro step (rise / run)
        float cost = 0.0f;              // A* cost with the reuse discount applied
        float reusedFraction = 0.0f;    // share of the route's cells that were already on the network
    };

    struct Inputs {
        const MacroSheet* macro = nullptr;
        const CivPlan* civ = nullptr;
        float seaLevel = 16.0f;
        float macroRiverAreaM2 = 5.0e5f;   // the same threshold the civ plan uses for a river cell
    };

    static std::shared_ptr<const CorridorGraph> bake(const Inputs& in, const WorldForgeParams& params);

    const std::vector<Route>& routes() const { return m_routes; }
    int   classAtCell(int cell) const { return (cell >= 0 && cell < static_cast<int>(m_cellClass.size())) ? m_cellClass[static_cast<size_t>(cell)] : None; }
    int   classAt(float worldX, float worldZ) const;                 // the macro cell under a point
    /// Distance (world units) from a point to the nearest corridor POLYLINE, and that corridor's
    /// class — the box plan's band discount and the D28 stamp read this. Only the cell's own and
    /// its neighbours' segments are tested (a per-cell segment index), so it is O(1) per column.
    float distanceToCorridorU(float worldX, float worldZ, int* clsOut = nullptr, int* routeOut = nullptr) const;
    float totalLengthU() const { return m_totalLengthU; }
    int   sharedCells() const { return m_sharedCells; }
    int   junctionCells() const { return m_junctionCells; }
    int   unroutable() const { return m_unroutable; }
    const std::vector<std::string>& refusals() const { return m_refusals; }
    float bakeMs() const { return m_bakeMs; }
    nlohmann::json toJson() const;

private:
    struct Seg { glm::vec2 a, b; int cls; int route; };
    std::vector<Route> m_routes;
    std::vector<uint8_t> m_cellClass;            // per macro cell
    std::vector<std::vector<int>> m_cellSegs;    // per macro cell: indices into m_segs touching it
    std::vector<Seg> m_segs;
    int m_cells = 0;
    float m_cellU = 512.0f, m_originU = 0.0f;
    float m_totalLengthU = 0.0f;
    int m_sharedCells = 0, m_junctionCells = 0, m_unroutable = 0;
    std::vector<std::string> m_refusals;
    float m_bakeMs = 0.0f;
};

}  // namespace Phyxel
