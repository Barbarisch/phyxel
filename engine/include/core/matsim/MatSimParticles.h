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
    /// Density-constraint iterations per substep (0 = no pressure: the control). 8 (measured, 6h):
    /// at 4 the 1/9 m U-tube gains energy in bursts (KE to 3.4 J/kg, the arms' surface 1.5 m
    /// wavy, 3 of 3 runs) while 8, 12, 16 iterations or half the CFL step all stay calm (KE <= 0.09).
    /// The water column is 3x deeper in cells at 1/9 m; 4 Jacobi iterations do not hold it.
    int iterations = 8;
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
    /// s, time constant of the averaged velocity used by settle(). 1 s (measured, 6f): at rest the
    /// averaged speed's p99 is ~0.25-0.3 m/s (residual swirl); real flows (slosh, dam break) run 1-3+ m/s.
    float settleTau = 1.0f;
    /// E2: settle() deposits supported particles slower than this (m/s, averaged) into cells. 0 = off.
    float settleSpeed = 0.0f;
    /// E2: a group settles when at least this fraction of its members is slow. 0.8 with settleSpeed
    /// 0.35 m/s (measured, 6f): a resting U-tube's averaged speed has p90 0.34 m/s, a sloshing one at
    /// 5 s p90 0.44 m/s. Empirical; a body's centre-of-mass height going still would be principled.
    float settleFraction = 0.8f;
    /// E2: a group settles only if its free surface (3 x 3-column averages) is level to within this
    /// many cells; keep it below wakeHeadCells so deposited water is not woken straight back.
    float settleLevelCells = 0.8f;
    /// E2: the free surface for that test is each column's volume averaged over a patch this many
    /// metres wide (6h): 1 m = the 3 x 3 cells fitted at 1/3 m. A patch fixed in cells (3 x 3) read
    /// a level 1/9 m U-tube as 4-8 cells uneven, so it never settled.
    float settlePatchM = 1.0f;
    /// E2: wake() lifts cell water whose head excess over a neighbour exceeds this many cells. 0 = off.
    float wakeHeadCells = 0.0f;
    float gravity = 9.81f;         ///< m/s^2 (tests set 0 to check that a lattice at rest stays at rest)
};

struct Particle {
    float x, y, z;       ///< world-cell units
    float vx, vy, vz;    ///< cells per second
    uint32_t units;      ///< fill units carried (exact)
    uint8_t mat;
    float ax = 0.f, ay = 0.f, az = 0.f;   ///< time-averaged velocity (EMA, settleTau), cells/s: in-place swirl cancels
    float age = 0.f;     ///< seconds as a particle; settle() needs age >= settleTau (the average starts at 0)
};

class MatSimParticlesGpu;

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
    /// E2: deposit every particle GROUP (particles within h of each other) whose members are all
    /// slow and old enough (ParticleConfig::settleSpeed / settleTau), lowest first. Returns the
    /// number deposited. Call after step(); cell water then moves by the CA.
    size_t settle(MatSimCore& core);
    /// E2: wake a whole cell-water BODY (face-connected cells of one liquid) into particles when any
    /// of its cells is about to move (head over an open / part-empty neighbour by > wakeHeadCells,
    /// or at least a quarter cell over a non-full space) or touches a particle. Uses the core's head
    /// field (pressure must be on). Returns the number of cells lifted.
    size_t wake(MatSimCore& core);
    struct WakeStats { size_t contact = 0, falling = 0, pressure = 0; };   ///< trigger cells in the last wake()
    const WakeStats& lastWakeStats() const { return wakeStats_; }
    /// Why groups did not settle in the last settle(): groups by blocker, and the largest group.
    struct SettleStats {
        size_t groups = 0, busySpeed = 0, busyLevel = 0, settled = 0;
        size_t bigMembers = 0, bigSlow = 0;      ///< the largest group: members, slow members
        float bigSpread = -1.f;                  ///< its surface spread (cells; < 0 = not measured: busy on speed)
    };
    const SettleStats& lastSettleStats() const { return settleStats_; }
    /// Diagnostics: particles slow and old enough to settle, and the groups still busy.
    void settleBlockers(const MatSimCore& core, size_t& slow, size_t& groupsBusy);

    const std::vector<Particle>& particles() const { return p_; }
    /// Test hooks: add a particle directly; density error C = rho/rho0 - 1 over all particles
    /// (fluid + boundary neighbours, same kernel as the solver) at the current positions.
    void addParticle(const Particle& p) { p_.push_back(p); }
    void densityError(const MatSimCore& core, float& meanAbsC, float& maxC);
    uint64_t units(uint8_t mat) const;
    float maxSpeed() const;          ///< cells per second
    int lastSubsteps() const { return lastSubsteps_; }
    ParticleConfig& config() { return cfg_; }
    /// 6h: route step() to a GPU stepper (nullptr = the CPU reference). Wake, settle and deposit
    /// stay on the CPU; the stepper uploads the particles and the box cells on each call.
    void setGpu(MatSimParticlesGpu* gpu) { gpu_ = gpu; }

    // 6h: shared by the CPU step and the GPU stepper.
    float restDensity();                                             ///< rho0 (lattice sum), computed once
    int substepsFor(const MatSimCore& core, double dtFrame) const;   ///< CFL substeps for a frame
    void endFrame(double dtFrame);                                   ///< averaged velocity + age update
    std::vector<Particle>& mutableParticles() { return p_; }
    void setLastSubsteps(int n) { lastSubsteps_ = n; }

private:
    void buildGrid(const MatSimCore& core);
    std::vector<uint32_t> groups(const MatSimCore& core);   ///< group root per particle (within h)
    bool solidAt(const MatSimCore& core, float x, float y, float z) const;

    ParticleConfig cfg_;
    MatSimParticlesGpu* gpu_ = nullptr;
    std::vector<Particle> p_;
    int lastSubsteps_ = 0;
    WakeStats wakeStats_;
    SettleStats settleStats_;
    // Work arrays (per substep).
    std::vector<float> px_, py_, pz_, lambda_, rho_;
    std::vector<uint32_t> cellStart_, cellCount_, sorted_;
    IVec3 gLo_{}, gDim_{};
    float rho0_ = 0.0f;
    uint32_t full_ = 0;
};

} // namespace Phyxel::Core::MatSim
