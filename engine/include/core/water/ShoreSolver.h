#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// WaterCore Phase G (docs/WaterCore.md 18.4): the shoreline band's solver. Nonlinear shallow-water
// on a column lattice: a continuous surface height per column (what a fill-fraction grid cannot
// carry for a swell - measured, 18.4), depth-averaged velocity per column, a wet/dry front for
// run-up, the voxel surface as the bed and dry columns above the surface as walls. The ocean is a
// prescribed region (surface + depth-averaged velocity from Airy theory, SeaSwell).
//
// Scheme: CONSERVATIVE finite volume in (depth, discharge) - the velocity-form staggered scheme was
// built first and measured (18.5): it carried the swell but lost its momentum at the breaking bore,
// drew the shore DOWN 8 cm and never ran up; a bore needs the conservative form. Second-order MUSCL
// (minmod on surface, depth and velocity), hydrostatic reconstruction at the faces (Audusse et al.
// 2004: still water on any bed is exact, wet/dry fronts never go negative), HLL fluxes, Heun time
// stepping under a CFL bound, semi-implicit Manning friction. Mass is exact: the only non-flux
// changes are the prescribed columns' resets and they are counted in the ledger (`exchanged`).
//
// Lattice: columns (x, z), nx x nz, cell size h (m). World position of column (x, z) =
// origin + (x + 0.5, z + 0.5) h. Domain edges and `wall` columns reflect.
namespace Phyxel::Core::Water {

struct ShoreParams {
    float gravity = 9.81f;
    float manningN = 0.025f;     ///< bed friction (sand ~0.02-0.03); 0 = frictionless
    float dryDepth = 0.001f;     ///< m: a column shallower than this is dry (no momentum)
    float cflFraction = 0.4f;    ///< of h / (|u| + sqrt(g d)) per substep (2-D unsplit, two stages)
    int   maxSubsteps = 16;
    float breakRatio = 0.78f;    ///< McCowan: a wave breaks where its height exceeds this fraction of the depth
};

struct ShoreColumn {
    float bed = 0.0f;            ///< world Y of the bed (top face of the highest solid)
    double eta = 0.0;            ///< world Y of the surface (== bed when dry); double so the mass ledger is exact
    float u = 0.0f, w = 0.0f;    ///< depth-averaged velocity (m/s), x and z; 0 when dry
    uint8_t wall = 0;            ///< 1 = solid above the water range (reflects, never wets)
    uint8_t prescribed = 0;      ///< 1 = the ocean: eta and the velocity are set each tick
    float foam = 0.0f;           ///< 0..1 breaking indicator (decays)
};

struct ShoreStepReport {
    int substeps = 0;
    double mass = 0.0;           ///< m^3 after the step
    double exchanged = 0.0;      ///< m^3 the prescribed columns gave (+) / took (-) this step
    double clamped = 0.0;        ///< m^3 created by clamping a negative depth (must stay 0; diagnostic)
    float maxSpeed = 0.0f;
    float maxEtaChange = 0.0f;
};

class ShoreSolver {
public:
    ShoreSolver(const glm::vec2& originXZ, int nx, int nz, float h, ShoreParams params = ShoreParams{});

    int nx() const { return m_nx; }
    int nz() const { return m_nz; }
    float h() const { return m_h; }
    const glm::vec2& origin() const { return m_origin; }
    size_t idx(int x, int z) const { return static_cast<size_t>(x) + static_cast<size_t>(m_nx) * z; }
    ShoreColumn& col(int x, int z) { return m_cols[idx(x, z)]; }
    const ShoreColumn& col(int x, int z) const { return m_cols[idx(x, z)]; }
    double depth(int x, int z) const { const ShoreColumn& c = m_cols[idx(x, z)]; return c.eta - c.bed; }

    /// Prescribe a column this tick: surface and depth-averaged velocity (m/s).
    void prescribe(int x, int z, double eta, const glm::vec2& velocity);
    void clearPrescriptions();

    ShoreStepReport step(float dt);
    double totalMass() const;    ///< m^3 (sum of depth x h^2)
    const ShoreParams& params() const { return m_params; }

private:
    void substep(float dt, ShoreStepReport& r);
    /// Rates of change of (eta, qx, qz) for the state (eta, u, w): flux divergence + bed source.
    void rates(const std::vector<double>& eta, const std::vector<float>& u, const std::vector<float>& w,
               std::vector<double>& dEta, std::vector<double>& dQx, std::vector<double>& dQz) const;
    glm::vec2 m_origin;
    int m_nx, m_nz;
    float m_h;
    ShoreParams m_params;
    std::vector<ShoreColumn> m_cols;
    std::vector<glm::vec2> m_prescribedVel;   // per column, valid where prescribed
    std::vector<double> m_etaPrev;
    double m_pendingExchange = 0.0;   // surface changes made by prescribe() since the last step
    // scratch (kept to avoid per-step allocation)
    std::vector<double> m_eta0, m_eta1;
    std::vector<float> m_u0, m_w0, m_u1, m_w1;
    std::vector<double> m_dEta, m_dQx, m_dQz;
};

}  // namespace Phyxel::Core::Water
