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
    /// E2: a group settles only if its free surface (the top particle per column, averaged over a
    /// settlePatchM patch) is level to within this many cells: what stops a body settling at a
    /// slosh's turning point. 1.25 (6i, fitted): the top of the water is resolved to one particle
    /// layer, and a resting sheet 1.6 cells thick reads 0.97-0.99 cells uneven (1 or 2 layers per
    /// column). The level deposit lays the water flat, so the old "below wakeHeadCells" bound (from
    /// the per-particle deposit) no longer applies; one cell is also the resolution of the cells.
    float settleLevelCells = 1.25f;
    /// E2: the free surface for that test is each column's volume averaged over a patch this many
    /// metres wide (6h): 1 m = the 3 x 3 cells fitted at 1/3 m. A patch fixed in cells (3 x 3) read
    /// a level 1/9 m U-tube as 4-8 cells uneven, so it never settled.
    float settlePatchM = 1.0f;
    /// E2: wake() lifts cell water whose head excess over a neighbour exceeds this many cells. 0 = off.
    float wakeHeadCells = 0.0f;
    /// 6i video (2026-11-07), REJECTED as a default, kept as a knob: contact wakes a cell only if it is at least this
    /// fraction full, so a drop joins a thin film instead of lifting it as a grid of specks. Measured on the live-rig
    /// replica at 1/3 m: at 0.5 the films stay cells, the CA spreads them wide and thin, and a later wake lifts them
    /// whole as thousands of near-empty particles. It had not settled at 36 s (5.6 k particles, 2.5 k+ under a
    /// quarter cell); with liftAtCentroid it blew up (34 k particles, 675 cells/s). At 0 it settles by 18 s.
    float wakeContactMinFill = 0.0f;
    /// 6i video, REJECTED as a default, kept as a knob: a lifted cell's particle starts at its water's centroid
    /// (y + fill / 2), not the cell centre. Near-empty cells then start on the floor's boundary particles and get
    /// thrown (see wakeContactMinFill).
    bool liftAtCentroid = false;
    /// --- 6i speck-fix knobs (2026-11-07): all four REJECTED as defaults (off), kept with their tests. ---
    /// Measured together on the live-rig replica at 1/3 m vs the old rules: settles by 24 s (old 16 s) and woke 692
    /// particles under a quarter cell (old 60); in the 1/3 m video the ledge still shows a lattice, of 9 bigger cubes
    /// instead of ~40 specks. The lattice comes from the CA spreading the film to a uniform depth: any deterministic
    /// packing of a uniform film tiles. See MaterialSimPlan 6i.
    /// 6i speck fix: wake() packs a body's cells under this fraction full into particles of about
    /// one cell's water each (face-connected clusters, at most 2 cells from their seed, grown to >= one cell; a leftover joins the nearest
    /// particle within 2 cells), placed at the water's horizontal centroid, inside the member cell nearest it, at its centre height.
    /// 0 = off (one particle per cell: a woken film becomes a grid of sliver particles).
    float packThinBelow = 0.0f;
    /// 6i speck fix: only a particle at least this fast (m/s, instantaneous) wakes cell water by contact. A particle at
    /// rest will settle into the cells itself; letting it wake them made two films on the live-rig ledge lift each other
    /// in turn forever once packed (replica, 1/3 m: a packed particle resting on the ledge lifted the neighbouring film
    /// every ~0.5 s from 19 s to the end). 0 = any particle.
    float wakeContactMinSpeed = 0.0f;
    /// 6i: a particle GROUP of at most this many members whose particle sits in, or right above, cell water of its
    /// liquid is absorbed into it (deposited by settle() whatever its speed) and never wakes that body by contact. A drop
    /// joins a pool or film; whole-body wake stays for real inflows. Replica, 1/3 m, packing on: drips off the ledge
    /// film re-woke the 2,142-cell pool by contact every ~1 s to the end. 0 = off.
    int absorbGroupMax = 0;
    /// 6i: the pressure trigger reads only cells at least this fraction full. A part-filled cell is a free surface: water
    /// under real head fills its cell. Replica, 1/3 m: a film hanging down the ledge wall, connected to the film on top,
    /// read as one cell+ of head and was lifted every 2 frames for seconds (659 sliver particles). 0 = any cell.
    float wakePressureMinFill = 0.0f;
    /// 6k (owner, 2026-11-08): the region-limited wake. > 0: a trigger cell lifts only the cells of its body within
    /// this many metres of it (face-connected steps through the body), not the whole body. 0 = off (whole body).
    float wakeRadiusM = 0.0f;
    /// 6k: stepCells() makes the empty cells that hold a particle walls to the CA for the step (restored after; they
    /// hold nothing, so the ledger is untouched), and wake() counts them as full for its pressure and falling triggers.
    /// Without it cell water pours into a woken hole through its particles. false = off (the CA ignores particles).
    bool caSeesParticles = false;
    /// 6k: wakeContactMinSpeed compares the particle's time-averaged velocity (the settleTau EMA, bias-corrected for
    /// its age) instead of the instantaneous one. Measured (RegionalWakeTrace, 1/3 m): ~10 of 3,000 woken particles
    /// jitter past 0.5 m/s at any instant, and each touch lifted another ball of the pool. false = instantaneous.
    bool wakeContactAveraged = false;
    /// 6k: a sponge at the particle/cell interface. Cell water is water at rest, so a wave that reaches it should leave the
    /// simulation (it would run on across the lake as ripples the CA does not have), not reflect. A particle with cell
    /// water of its liquid within spongeCells cells has its velocity multiplied by exp(-dt / spongeTau) each frame.
    /// Measured (PoolDropPhases, 1/9 m, no sponge): the woken region sloshed against its cell wall, its motion woke more
    /// wall by contact and pressure, and it grew from 1.6 k to 50 k particles in 10 s. 0 = off.
    float spongeTau = 0.0f;
    /// 6k: the pressure trigger counts a part-filled neighbour only if it has at least this fraction of a cell of room. A
    /// void of 2-4 % of a cell two cells under the surface read as 1.07 cells of head and woke ~1,000 cells to fill it
    /// (PoolDropPhases, 1/9 m); the CA's pressure rule fills such voids itself. 0 = any room (the old rule).
    float wakePressureMinRoom = 0.0f;
    /// 6k cost: wake() scans for falling and pressure triggers only in bricks (8^3 cells) that changed in the core's last
    /// two ticks (cells or heads), or hold a particle, dilated by one brick. A trigger that persisted through two quiet
    /// ticks would have fired already and been lifted, so the result equals the full scan (pinned per tick by
    /// WakeSkipQuietEqualsTheFullScan). Measured: the full scan cost ~47 ms a tick at 1/9 m (Debug), at rest too. false = full scan.
    bool wakeSkipQuiet = false;
    int spongeCells = 2;
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
    /// Step the cells one CA tick (core.step(1)); with caSeesParticles the cells held by particles are walls for it.
    StepStats stepCells(MatSimCore& core);
    /// 6k diagnostics: the first pressure trigger cells of the last wake (cell, its neighbour, head and the neighbour's level, units).
    struct PressureTrigger { IVec3 cell, nb; uint32_t head, ex, fill, nbFill; };
    struct WakeStats { size_t contact = 0, falling = 0, pressure = 0, particles = 0, packed = 0; std::vector<PressureTrigger> firstPressure; };   ///< trigger cells in the last wake(); particles made; of them packed from thin cells
    const WakeStats& lastWakeStats() const { return wakeStats_; }
    /// Why groups did not settle in the last settle(): groups by blocker, and the largest group.
    struct SettleStats {
        size_t groups = 0, busySpeed = 0, busyLevel = 0, settled = 0, absorbed = 0;
        size_t bigMembers = 0, bigSlow = 0;      ///< the largest group: members, slow members
        float bigSpread = -1.f;                  ///< its surface spread (cells; < 0 = not measured: busy on speed)
        double msGroups = 0, msSurface = 0, msDeposit = 0;   ///< wall time of the three parts
        float loCol[3] = {0, 0, -1}, hiCol[3] = {0, 0, -1};   ///< the largest group: (x, z, patch surface) of its lowest / highest column
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
    void sponge(const MatSimCore& core, double dtFrame);   ///< 6k: damp particles next to cell water

    ParticleConfig cfg_;
    MatSimParticlesGpu* gpu_ = nullptr;
    std::vector<Particle> p_;
    int lastSubsteps_ = 0;
    WakeStats wakeStats_;
    SettleStats settleStats_;
    // Work arrays (per substep).
    std::vector<float> px_, py_, pz_, lambda_, rho_;
    std::vector<uint32_t> cellStart_, cellCount_, sorted_;
    std::vector<uint32_t> gCell_;   ///< groups(): per box cell
    IVec3 gLo_{}, gDim_{};
    float rho0_ = 0.0f;
    uint32_t full_ = 0;
};

} // namespace Phyxel::Core::MatSim
