#pragma once
// MatSimParticles — docs/MaterialSimPlan.md §6f, experiment E1: MatSim's own particle fluid.
// Position Based Fluids (Macklin & Mueller, SIGGRAPH 2013) over a MatSimCore box: liquid cells are
// LIFTED into particles (one per cell, carrying that cell's fill as integer units), stepped with a
// density constraint (incompressibility = pressure), and DEPOSITED back into cells integer-exactly.
// Solid cells near a particle act as static boundary particles (Akinci et al. 2012, voxel form).
// CPU reference: deterministic and sequential. Positions are in world-cell units (cell (i,j,k)
// spans [i, i+1) etc.); time is in seconds.

#include <cstdint>
#include <vector>

#include "core/matsim/MatSimCore.h"

namespace Phyxel::Core::MatSim {

struct ParticleConfig {
    int iterations = 4;            ///< density-constraint iterations per substep (0 = no pressure: the control)
    float kernelCells = 2.0f;      ///< kernel radius h, in cells
    float relaxation = 1e-3f;      ///< epsilon in lambda's denominator (dimensionless here: density is normalised)
    /// XSPH c. 0.1 (measured, 6f): the slosh period is unchanged from 0.01 (4.4 vs 4.3 s, theory
    /// 4.49), the first surge 3.04 vs 3.11 m, and the resting jitter 4x lower (median 0.28 vs 1.06 m/s).
    float viscosity = 0.1f;
    /// s_corr k (anti-clustering), dq = 0.2 h, n = 4. OFF by default: it pushes fluid pairs apart
    /// even at exact rest density with no counterpart at walls, so a full box at rest accelerated
    /// (0.28 cells/s^2 from frame 1, zero at k = 0; MatSimParticles.DISABLED_FullBoxZeroGGrowth).
    /// The unilateral density constraint already prevents the free-surface clumping it exists for.
    float tensileK = 0.0f;
    float cflCells = 0.4f;         ///< the fastest particle moves at most this many cells per substep
    int maxSubsteps = 16;
    float omega = 1.0f;            ///< Jacobi under-relaxation of each iteration's correction (1 = none)
    /// DFSPH-style velocity divergence solve per substep (Bender & Koschier 2015). 2 (measured,
    /// 6f): halves the resting churn (still tank median 1.0 -> 0.64 m/s at c 0.01; 0.29 -> 0.22 at
    /// c 0.1) with the U-tube surge and period unchanged (3.07 m, ~4.5 s).
    int divergenceIterations = 2;
    float minC = 0.0f;             ///< lower clamp of C = rho/rho0 - 1 (0 = unilateral: push only; -1 = bilateral)
    float gravity = 9.81f;         ///< m/s^2 (tests set 0 to check that a lattice at rest stays at rest)
};

struct Particle {
    float x, y, z;       ///< world-cell units
    float vx, vy, vz;    ///< cells per second
    uint32_t units;      ///< fill units carried (exact)
    uint8_t mat;
};

class MatSimParticles {
public:
    explicit MatSimParticles(ParticleConfig cfg = {}) : cfg_(cfg) {}

    /// Lift every liquid cell of `core` in the world-cell box [lo, lo+size) into one particle at the
    /// cell centre; the cell becomes empty. Returns the number lifted.
    size_t lift(MatSimCore& core, IVec3 lo, IVec3 size);
    /// Advance by `dt` seconds (CFL substeps inside). Solids are read from `core` (box outside = solid).
    void step(const MatSimCore& core, double dt);
    /// Put every particle's units back into cells (overflow carried to the nearest open room,
    /// deterministic), then clear. Returns units that found no room (0 unless the box is full).
    uint64_t deposit(MatSimCore& core);

    const std::vector<Particle>& particles() const { return p_; }
    /// Test hooks: add a particle directly; density error C = rho/rho0 - 1 over all particles
    /// (fluid + boundary neighbours, same kernel as the solver) at the current positions.
    void addParticle(const Particle& p) { p_.push_back(p); }
    void densityError(const MatSimCore& core, float& meanAbsC, float& maxC);
    uint64_t units(uint8_t mat) const;
    float maxSpeed() const;          ///< cells per second
    int lastSubsteps() const { return lastSubsteps_; }
    ParticleConfig& config() { return cfg_; }

private:
    void buildGrid(const MatSimCore& core);
    bool solidAt(const MatSimCore& core, float x, float y, float z) const;

    ParticleConfig cfg_;
    std::vector<Particle> p_;
    int lastSubsteps_ = 0;
    // Work arrays (per substep).
    std::vector<float> px_, py_, pz_, lambda_, rho_;
    std::vector<uint32_t> cellStart_, cellCount_, sorted_;
    IVec3 gLo_{}, gDim_{};
    float rho0_ = 0.0f;
    uint32_t full_ = 0;
};

} // namespace Phyxel::Core::MatSim
