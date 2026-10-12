#pragma once
// WaterCore.md 25.18 H1 - the column tier (T2) on real terrain, step 1: a column-water REGION over a box of the world.
// The H0-proven ShoreSolver (§18.5, 25.16: Manning-exact on a smooth bed, dams and redirects right, exact ledger) plus
// what a creek needs and the solver lacks: springs and outfalls with a ledger, beds read from the voxels, and the BED RULE
// the owner asked to compare (25.18-25.19):
//   Voxel     - the top of the highest solid under each column: a staircase (H0: steps pool);
//   Hydraulic - within a run of equal tops, the bed ramps from the step above it down to its own top: never below the
//               drawn ground, a staircase flows like the slope it approximates; a run with no lower exit stays flat (a pool);
//   Plane     - a rig-only stand-in for the generator's continuous height (candidate (a)) on an authored channel that has
//               no generator surface: y0 - slope x distance along a direction.
//   Generator - OPTION (a), CHOSEN (25.21): per column, the generator's continuous height where the ground is as generated,
//               the voxel top where it was edited (the manager supplies the beds, 25.22). The others stay for a future switch.
// Changing the bed keeps each column's DEPTH (its water moves with it): no water is created or destroyed by a rule switch.

#include "core/water/ShoreSolver.h"
#include "core/water/WaterSurfaceMesh.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Phyxel::Core::Water {

enum class ColumnBedRule { Voxel, Hydraulic, Plane, Generator };   // Generator = option (a), the owner's choice (25.21)
const char* columnBedRuleName(ColumnBedRule r);
bool parseColumnBedRule(const std::string& s, ColumnBedRule& out);

/// Pure: the hydraulic bed from per-column voxel tops (row-major nx * nz, x fastest; walls excluded). Exposed for tests
/// (C-T7: a run's ramp is a function of the tops alone, so it is identical wherever the run sits).
/// `exits` (optional, same size) marks columns where water leaves the region (an outfall): a run touching one has a lower
/// exit there - without it the last run of a channel, whose next step lies outside the region, never ramped (25.20).
std::vector<float> hydraulicBed(const std::vector<float>& tops, const std::vector<uint8_t>& walls, int nx, int nz,
                                const std::vector<uint8_t>* exits = nullptr);

class ColumnWater {
public:
    static constexpr float kH = 1.0f / 3.0f;   ///< m, the column pitch (the 3-D core's default cell; H0's lattice)

    /// The region covers voxels [minVoxel, maxVoxel] (inclusive); its lattice is 3 columns per voxel in x and z.
    ColumnWater(int id, const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, ShoreParams params = ShoreParams{});

    int id() const { return m_id; }
    const glm::ivec3& minVoxel() const { return m_min; }
    const glm::ivec3& maxVoxel() const { return m_max; }
    int nx() const { return m_solver.nx(); }
    int nz() const { return m_solver.nz(); }
    /// World xz of column (x, z)'s centre.
    glm::vec2 columnCentre(int x, int z) const;
    bool columnAt(float wx, float wz, int& x, int& z) const;

    /// The world's solids under each column: `tops` = world Y of the top of the highest solid in the box's height range
    /// (or the box floor when none), `walls` = 1 where the column is solid up to the box's top. Re-applies the bed rule.
    void setSolids(const std::vector<float>& tops, const std::vector<uint8_t>& walls);
    void setBedRule(ColumnBedRule r);
    /// 25.22: per-column generator beds (row-major, as the tops) - the manager builds them; empty = none (Generator then reads as Voxel).
    void setGeneratorBeds(std::vector<float> beds) { m_genBeds = std::move(beds); if (m_rule == ColumnBedRule::Generator) applyBeds(); }
    bool hasGeneratorBeds() const { return !m_genBeds.empty(); }
    ColumnBedRule bedRule() const { return m_rule; }
    void setPlane(float y0, float slope, float dirDeg) { m_planeY0 = y0; m_planeSlope = slope; m_planeDir = dirDeg; applyBeds(); }

    /// A spring at column (x, z): `rate` m^3/s enters as water welling up (no momentum). An outfall at (x, z) takes all
    /// water above the bed each step (a free overfall out of the region). Both counted in the ledger.
    void addSpring(int x, int z, double rate);
    void addOutfall(int x, int z);
    /// 25.26 K - tuning: the springs' TOTAL rate (m^3/s) kept in their present proportions; the bed roughness (Manning n).
    void setSpringTotal(double total);
    double springTotal() const { double t = 0.0; for (const auto& s : m_springs) t += s.rate; return t; }
    void setManningN(float n) { m_solver.setManningN(n); }
    float manningN() const { return m_solver.params().manningN; }
    /// 25.26 K - measuring: the column at world (x, z). `slope` = the bed's fall along the flow (central difference over
    /// +-1 column, along the velocity; along x when still); the Manning prediction for the column's own unit discharge q =
    /// depth x speed: normal depth (q n / sqrt S)^(3/5) and speed q / depth_n (a wide channel).
    struct Probe { bool inside = false, wet = false; float depth = 0, u = 0, w = 0, speed = 0, surface = 0, bed = 0, slope = 0,
                   froude = 0, manningDepth = 0, manningSpeed = 0; };
    Probe probe(float wx, float wz) const;
    void clearSpringsAndOutfalls() { m_springs.clear(); m_outfalls.clear(); if (m_rule == ColumnBedRule::Hydraulic) applyBeds(); }

    void step(float dt);

    // ledger (m^3): stored = in - out + the region's water when the ledger started
    double inflow() const { return m_in; }
    double outflow() const { return m_out; }
    double stored() const { return m_solver.totalMass(); }
    double ledgerError() const { return stored() - (m_base + m_in - m_out); }
    double lastStepMs() const { return m_ms; }
    int wetColumns() const;
    /// Measurement (25.18): mean depth and speed over the wet columns of the middle third along x; how many wet columns
    /// have their surface BELOW the drawn voxel top (hidden water - candidate (a)'s risk) and the worst such gap.
    struct Stats { double midDepth = 0.0, midSpeed = 0.0, worstHidden = 0.0; int wet = 0, hidden = 0; };
    Stats stats() const;

    /// Phase F's surface (one run per wet column: bed -> surface), drawn by the existing mesher and shader (25.15 F1).
    void publish(WaterSurfaceField& out) const;

    ShoreSolver& solver() { return m_solver; }
    const ShoreSolver& solver() const { return m_solver; }
    const std::vector<float>& voxelTops() const { return m_tops; }

private:
    void applyBeds();
    int m_id;
    glm::ivec3 m_min, m_max;
    ShoreSolver m_solver;
    ColumnBedRule m_rule = ColumnBedRule::Voxel;
    float m_planeY0 = 0.0f, m_planeSlope = 0.0f, m_planeDir = 0.0f;
    std::vector<float> m_tops;
    std::vector<float> m_genBeds;   // 25.22
    std::vector<uint8_t> m_walls;
    struct Spring { int x, z; double rate; };
    std::vector<Spring> m_springs;
    std::vector<int> m_outfalls;   // column indices
    double m_in = 0.0, m_out = 0.0, m_base = 0.0, m_ms = 0.0;
};

}  // namespace Phyxel::Core::Water
