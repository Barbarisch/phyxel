#pragma once
// WaterCore — the small-scale water solver (docs/WaterCore.md §4, §15).
//
// One ACTIVE VOLUME's grid plus the tick that moves water through it. No engine dependencies
// beyond glm: solids arrive through a SolidQuery by world position, sources by world position, so
// the unit tests drive it with synthetic lambdas and the engine binds the micro occupancy pool.
//
// Units: 1 cell = h world units (h in {1, 1/3, 1/9, 1/27, 1/81}); mass in m^3 (f * h^3); time in s.
// Invariants this interface promises (each is a WaterCoreTest):
//   P1  sum(f) changes only by sources - sinks (transport is conservative by construction)
//   P2  still water stays still; a resting column is hydrostatic
//   P5  no flux through a Solid or Unknown (hold) face, at every resolution
//   P7  a quiet volume sleeps and costs nothing; results are bit-deterministic
#include <algorithm>
#include "core/water/MovingSolids.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Phyxel {
namespace Core {
namespace Water {

/// Occupancy of a CELL at the grid's own resolution. Unknown is a HOLD: a wall for flux this tick
/// (the debris solver's SS_FROZEN_UNKNOWN rule) that releases when the query learns the cell.
enum class Occ : uint8_t { Air = 0, Solid = 1, Unknown = 2 };

/// Occupancy by world CELL coordinate (world position / h, floored).
using SolidQuery = std::function<Occ(const glm::ivec3& cellWorld)>;

struct GridSpec {
    glm::ivec3 origin{0, 0, 0};   ///< in cells: world = origin * h
    glm::ivec3 dims{0, 0, 0};
    float      h = 1.0f;          ///< cell size, world units
};

enum class CellKind : uint8_t { Empty = 0, Surface = 1, Liquid = 2 };

/// SoA storage for one active volume. Face velocities live on a MAC grid: u(x,y,z) is the
/// x-velocity on the face between cells (x-1) and x, so u has nx+1 entries along x; likewise v, w.
class WaterGrid {
public:
    explicit WaterGrid(const GridSpec& spec);

    const GridSpec& spec() const { return m_spec; }
    int nx() const { return m_spec.dims.x; }
    int ny() const { return m_spec.dims.y; }
    int nz() const { return m_spec.dims.z; }
    float h() const { return m_spec.h; }
    float cellVolume() const { return m_spec.h * m_spec.h * m_spec.h; }
    size_t cellCount() const { return static_cast<size_t>(nx()) * ny() * nz(); }
    bool inBounds(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < nx() && y < ny() && z < nz(); }
    size_t idx(int x, int y, int z) const { return static_cast<size_t>(x) + static_cast<size_t>(nx()) * (static_cast<size_t>(y) + static_cast<size_t>(ny()) * z); }

    float  f(int x, int y, int z) const { return m_f[idx(x, y, z)]; }
    float& f(int x, int y, int z)       { return m_f[idx(x, y, z)]; }
    Occ    occ(int x, int y, int z) const { return m_occ[idx(x, y, z)]; }
    Occ&   occ(int x, int y, int z)       { return m_occ[idx(x, y, z)]; }
    CellKind kind(int x, int y, int z) const { return m_kind[idx(x, y, z)]; }
    /// docs/WaterCore.md 20: the moving-solid volume fraction of each cell (0..1; 0 = no body) and the rate
    /// (m^3/s) at which the cell must shed the water that no longer fits - set by WaterSolver::setMovingSolids.
    /// With no body every rule that reads them reduces to its old form exactly (1 - 0 - f == 1 - f, f + 0 == f).
    float  s(int x, int y, int z) const { return m_s[idx(x, y, z)]; }
    float  q(int x, int y, int z) const { return m_q[idx(x, y, z)]; }
    /// The fill the SURFACE reads (20.5): water plus the body volume it surrounds - the level round a
    /// submerged body is (water + body) / area - but a body in AIR (no water in its cell, f below the
    /// 1 mm film) is not water. Clamped to 1.
    float levelFill(int x, int y, int z) const {
        const size_t i = idx(x, y, z);
        const float fv = m_f[i];
        const float v = fv + (fv >= kSurfaceMinDepth / m_spec.h ? m_s[i] : 0.0f);
        return v < 1.0f ? v : 1.0f;
    }
    std::vector<float>& sData() { return m_s; }
    const std::vector<float>& sData() const { return m_s; }
    std::vector<float>& qData() { return m_q; }
    const std::vector<float>& qData() const { return m_q; }

    size_t uIdx(int x, int y, int z) const { return static_cast<size_t>(x) + static_cast<size_t>(nx() + 1) * (static_cast<size_t>(y) + static_cast<size_t>(ny()) * z); }
    size_t vIdx(int x, int y, int z) const { return static_cast<size_t>(x) + static_cast<size_t>(nx()) * (static_cast<size_t>(y) + static_cast<size_t>(ny() + 1) * z); }
    size_t wIdx(int x, int y, int z) const { return static_cast<size_t>(x) + static_cast<size_t>(nx()) * (static_cast<size_t>(y) + static_cast<size_t>(ny()) * z); }
    float  u(int x, int y, int z) const { return m_u[uIdx(x, y, z)]; }   ///< face (x-1/2, y, z), x in [0, nx]
    float& u(int x, int y, int z)       { return m_u[uIdx(x, y, z)]; }
    float  v(int x, int y, int z) const { return m_v[vIdx(x, y, z)]; }   ///< face (x, y-1/2, z), y in [0, ny]
    float& v(int x, int y, int z)       { return m_v[vIdx(x, y, z)]; }
    float  w(int x, int y, int z) const { return m_w[wIdx(x, y, z)]; }   ///< face (x, y, z-1/2), z in [0, nz]
    float& w(int x, int y, int z)       { return m_w[wIdx(x, y, z)]; }

    /// Fill a box of cells (grid-local, inclusive) with `fill`; clamps to bounds.
    void fillBox(const glm::ivec3& lo, const glm::ivec3& hi, float fill);
    /// Recompute `kind` from f: Liquid when f >= liquidThreshold, Surface when 0 < f, else Empty.
    void classify(float liquidThreshold = 0.999f);

    double totalMass() const;                         ///< sum f * h^3 (m^3), double accumulation
    double columnMass(int x, int z) const;            ///< m^3 in grid column (x, z)
    /// World Y of the free surface in column (x, z): the top-most non-empty cell's bottom + fill,
    /// or NaN when the column holds no water.
    static constexpr float kSurfaceMinDepth = 1e-3f;   ///< m: thinner water is not a surface (S11 writes spans +- 1 mm)
    float surfaceWorldY(int x, int z) const;          ///< world y of the surface: the top run's HEIGHT FUNCTION, bottom + the water in it (NaN when dry) - what is drawn (WaterCore.md 19.5)
    float wetTipWorldY(int x, int z) const;           ///< world y the water REACHES: the fill line of the highest cell holding >= 1 mm (a climbing sheet's tip; run-up, crest gates)
    glm::vec3 cellCenterWorld(int x, int y, int z) const;
    glm::ivec3 worldToCell(const glm::vec3& world) const;  ///< grid-local cell (may be out of bounds)
    double kineticEnergy() const;                     ///< sum 1/2 f h^3 |v_cell|^2 (density 1)

    /// Aggregate this grid's mass per WORLD VOXEL column (1 x 1 world units), keyed by voxel
    /// (x, z) in world voxel coords. Used by the resolution-invariance test and the debug feed.
    std::vector<std::pair<glm::ivec2, double>> massPerVoxelColumn() const;

    std::vector<float>& fData() { return m_f; }
    const std::vector<float>& fData() const { return m_f; }
    std::vector<float>& uData() { return m_u; }
    std::vector<float>& vData() { return m_v; }
    std::vector<float>& wData() { return m_w; }

private:
    GridSpec m_spec;
    std::vector<float> m_f, m_u, m_v, m_w;
    std::vector<float> m_s, m_q;   // 20: moving-solid fraction, shed rate (m^3/s)
    std::vector<Occ> m_occ;
    std::vector<CellKind> m_kind;
};

struct SolverParams {
    float  gravity = 9.81f;
    float  cflFraction = 0.5f;       ///< substep so that max|v| * dtSub <= cflFraction * h
    int    maxSubsteps = 16;
    /// A cell joins the pressure domain when its CENTRE is submerged (fill >= 0.5, the standard
    /// ghost-fluid choice); emptier cells are free surface. With 0.999 the partially filled cells
    /// at a collapsing dam front felt no horizontal pressure gradient and the front ran at a
    /// fifth of Ritter (fourth green attempt, 2026-10-08). Filling still works because the
    /// donor-cell flux carries the DONOR's fill: a half-full cell fed by a full one gains.
    float  liquidThreshold = 0.5f;
    double keWake = 1e-6;            ///< SPECIFIC kinetic energy (m^2/s^2, i.e. |v| ~ 1.4 mm/s) below which the volume counts as quiet
    double maxDeltaFQuietParticles = 0.2;    ///< one particle crossing a cell face moves 1/8 of a fill in a tick; a four-particle puddle at rest still crosses now and then (S1 Small on FLIP never slept at 1e-3), so quiet for particles is the KE criterion with a crossing allowed
    double keWakeParticles = 1e-3;   ///< the same for a PARTICLE volume (|v| ~ 4.5 cm/s): particles jostle at rest and never reach 1e-6 on their own; below this the rest damping takes them down and the volume settles into fills (Phase B2, S1 Small never slept at 1e-6)
    double maxDeltaFQuiet = 1e-5;    ///< ... and when max |delta f| per tick is below this
    int    restTicks = 30;           ///< consecutive quiet ticks before sleeping
    float  restDamping = 1.0f;       ///< 1/s, applied to velocity while the volume is in the SETTLE band (below keSettle), never to moving water. 0.5 took a poured column from the band edge to sleep in 16 s (KE decays at the velocity rate, half the energy is potential); 1.0 is the S11 budget (Phase D, 2026-10-08)
    /// m^2/s^2: below this the volume is SETTLING - restDamping stands in for the dissipation the inviscid solver lacks, and
    /// compaction closes voids (24.6). 1e-3 until 2026-10-11 (Phase D's S11 budget, "|v| ~ 3 cm/s rms"): in the Small pond
    /// that band held every wave under ~1.6 cm, so visible waves were braked as if settling - the owner's "thicker than
    /// water" (WaterCore.md 24.1). 2e-4 is just above the small-wave floor measured in WaterWaveTest.WavesKeepTheirEnergy
    /// (2-5 mm waves read ~1e-4, 6-10x their linear energy; a still pond 1e-16): the band now starts at ~7 mm waves.
    /// Why a band at all (Phase D, 2026-10-08): with damping only under keWake a poured column, a bumped pool and a dam break
    /// in a sealed 3 m tank sloshed for 100 s and never slept; S11 wants asleep within 10 s of the last motion.
    double keSettle = 2e-4;
    /// 1/s - pond friction (WaterCore.md 24.7, owner 2026-10-11 "option 2"): every substep all face velocities are scaled
    /// by (1 - a ds), so an undriven slosh halves its height every 2 ln 2 / a seconds (a velocity drag a damps an oscillator's
    /// amplitude at a / 2 - the first cut used ln 2 / N and measured a 20-40 s half-life, WaterWaveTest W-T6). The stated stand-in for what a
    /// smooth voxel box lacks (irregular banks, plants, films, rough beds): without it the box is a laboratory tank and a
    /// disturbed pond sloshes for minutes (24.6). Default 2 ln 2 / 5 s; `water_pond_friction {half_life_s}`. 0 = off.
    float  pondFriction = 0.27726f;
    float  bedManning = 0.0f;   ///< 25.39 H2: Manning n at the bed (creek windows); > 0 replaces pond friction (applyBedManning)
    /// m/s^2 along +x - a TILTED FRAME (WaterCore.md 26 P1 rig): gravity's component along a channel of slope S is g S, so a flat
    /// box with gravityX = g S carries the flow of a sloped bed without the 1/9 m staircase a sloped bed of cells would be. 0 in
    /// the engine; the P1 rigs only.
    float  gravityX = 0.0f;
    /// m/s - compaction's closing speed in MOVING water (WaterCore.md 24.11): a void under a liquid cell closes always, at this
    /// speed, so pockets never build up under a moving surface and collapse in one jerk when the volume settles (live: 30 %
    /// pocket, the surface 11 cm high, an 11 cm drop at the switch). At free fall (2.6 m/s at 1/3 m) the closing drained
    /// waves at their turning points (a 4 m wave kept 0.69 of itself per swing on a half-full cell); 0.01 kept 0.91. In the
    /// settle band compaction runs at free fall (slow closing at rest left the S3 / spawn / cavity rows unsettled).
    float  compactionSpeed = 0.01f;
    /// s - the VOLUME CORRECTION (WaterCore.md 26.21): a buried part-full liquid cell (a liquid cell above, no side face to air)
    /// asks the projection for the net inflow that returns its missing volume over this time. Without it moving water kept
    /// 4-10 % air below its surface (26.20: the drawn creek 9-26 mm above its water) - the projection treats a liquid cell as
    /// full, and compaction only runs in quiet water. 0 = off (bisecting only).
    float  volumeCorrectionTime = 0.5f;
    /// cell fraction - a deficit under this is left alone (26.21): wave motion makes deficits of a few per cent that come and
    /// go each swing, and pulling them cost the short wave on a half-full cell (mode 4 at 0.5: 0.66 -> 0.48-0.58 at bands of
    /// 0 - 1 %) and drove float rounding in the transport (the pump's books 2e-6 -> 3e-5). At 2 %: 0.62, the books 1e-6; the
    /// creek's air was 7-21 %.
    float  volumeCorrectionBand = 0.02f;
    static float pondFrictionForHalfLife(float seconds) { return seconds > 0.0f ? 1.38629436f / seconds : 0.0f; }
    /// DIAGNOSTIC ONLY (bisecting a measured defect, never shipped on): bits disable a stage of
    /// the substep. 1 compaction · 2 thin-film slope · 4 thin-film settle · 8 "first halo layer only"
    /// (all three layers overwrite thin faces, the pre-#11 behaviour) · 16 residue sweep · 32 rest damping.
    uint32_t debugDisableStages = 0;   // 64: the moving-solid wake rule (20.5, dragged air rising out)
    /// Ghost-fluid clamp: the surface is never closer than this fraction of a cell to a liquid cell's centre (1/theta
    /// stays finite); see thetaToAir. 0.1 until 2026-10-11: as a cell crosses the 0.5 cutoff the true theta passes
    /// through 0, so the surface the solver projected against jumped a tenth of a cell at every crossing and a pond
    /// resting half-way up a cell kept 28 % of a wave per swing (WaterCore.md 24.1). 0.01 keeps 1/theta <= 100.
    double thetaMin = 0.01;
    float  filmHoldDepth = 0.01f;    ///< m: a film this thin or thinner is pinned (contact-angle stand-in); only the depth above it flows under its own slope (S1: puddles hold)
    int    pcgMaxIters = 400;
    double pcgTolerance = 1e-6;      ///< relative residual
};

struct SourceSpec {
    glm::ivec3 cell{0, 0, 0};        ///< grid-local cell
    float  rate = 0.0f;              ///< m^3/s; negative = sink
    double placedTotal = 0.0;        ///< m^3 actually added (or removed, negative)
    double unplaced = 0.0;           ///< m^3 the cell could not accept this tick (reported, not lost)
    double pending = 0.0;            ///< m^3 owed by the pump and placed as soon as the outlet has room (capped at one second of rate)
    bool   outfall = false;          ///< 26.13: an OUTFALL - takes every drop in its cell and the cells above it up to the first solid, every
                                     ///< substep (the column outfall's rule at the pond's resolution); rate unused; counted in placedTotal
};

/// Phase G (docs/WaterCore.md 18.2): a sub-column whose SURFACE is prescribed at the start of
/// every tick - the ocean body boundary. Fills are written to the target height, velocity is left
/// to the solver (a wave enters only through the surface gradient), and the mass the write
/// exchanged is counted: the flux ledger of 5.3.
struct BoundarySpec {
    glm::ivec2 column{0, 0};         ///< grid-local (x, z)
    float  targetY = 0.0f;           ///< world Y of the prescribed surface
    glm::vec2 uSurface{0.0f, 0.0f};  ///< horizontal orbital velocity at the surface (m/s), Airy theory; decays with depth by cosh(k(y - bed)) / cosh(k d)
    float  wSurface = 0.0f;          ///< vertical orbital velocity at the surface (m/s); decays by sinh(k(y - bed)) / sinh(k d)
    float  k = 0.0f;                 ///< wavenumber of the dominant component (1/m); 0 = uniform profile
    double exchanged = 0.0;          ///< m^3 written into (+) or taken out of (-) the volume over its life
};

struct StepReport {
    int    substeps = 0;
    int    pcgIterations = 0;        ///< total over the tick's substeps
    double pcgResidual = 0.0;        ///< last substep's relative residual
    double kineticEnergy = 0.0;
    double totalMass = 0.0;
    double maxDeltaF = 0.0;
    double sourceAdded = 0.0;
    double sinkRemoved = 0.0;
    double boundaryExchange = 0.0;   ///< m^3 the prescribed columns exchanged this tick (+ into the volume)
    double sourceUnplaced = 0.0;
    int    quietTicks = 0;
    bool   asleep = false;
    double residueDropped = 0.0;   ///< m^3 of sub-epsilon residue with nowhere to merge this tick (sweepResidue)
    bool   restConverted = false;  ///< this tick the particle transport settled into fills (Phase B2)
};

/// How water POSITION moves (docs/WaterCore.md §4.7): Eulerian fill fractions (Phase B) or FLIP
/// particles (Phase B2). Both run on the same grid, gravity and pressure solve.
class IWaterTransport {
public:
    virtual ~IWaterTransport() = default;
    virtual const char* name() const = 0;
    /// Move f (and carry velocity) by dt using the grid's face velocities. MUST conserve sum(f)
    /// exactly: a cell's outflow never exceeds its content; inflow receives what was sent; no flux
    /// across a face whose other cell is Solid or Unknown.
    virtual void advect(WaterGrid& g, float dt) = 0;
    virtual void setLiquidThreshold(float) {}   ///< SolverParams::liquidThreshold, forwarded by the solver
    // ── Phase B2 (docs/WaterCore.md §15.9): what a particle transport needs beyond fills ──
    /// True when the transport, not the grid's f, is the mass ledger (f is then a derived field
    /// rebuilt every substep; the Eulerian-only passes - compaction, residue sweep, ceiling target -
    /// must not touch it).
    virtual bool ownsMass() const { return false; }
    /// Build the transport's state from the grid's fills (no-op for fills themselves).
    virtual void seed(WaterGrid&) {}
    /// Hand the mass back to the grid's fills (rest conversion); afterwards the transport is empty.
    virtual void settle(WaterGrid&) {}
    /// Sources/sinks for a transport that owns mass: add up to m3 at the cell (returns what was
    /// taken, the rest is owed), remove up to m3 (returns what was removed). The fill transport
    /// returns a negative number: "not mine, fill the cell".
    virtual double addVolume(WaterGrid&, const glm::ivec3& /*cell*/, double /*m3*/, const glm::vec3& /*vel*/) { return -1.0; }
    virtual double removeVolume(WaterGrid&, const glm::ivec3& /*cell*/, double /*m3*/) { return -1.0; }
    virtual size_t particleCount() const { return 0; }
    /// Total mass the transport holds (m^3); 0 for fills.
    virtual double ownedMass() const { return 0.0; }
};

/// One FLIP particle (docs/WaterCore.md §15.9). Positions are grid-local in CELL units (a cell
/// spans [i, i+1)), velocities in m/s, mass in m^3 (fixed h^3/8 except one lighter remainder per
/// seeded cell so column mass is exact). `id` is the creation index: the deterministic sort key.
struct FlipParticle {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    float mass = 0.0f;
    uint32_t id = 0;
};

/// Particle transport on the shared grid: particles carry mass and momentum; the grid is rebuilt
/// from them every substep (p2g), the solver projects it as for fills, and the projected change
/// goes back to the particles (FLIP/PIC blend) before they move. Deterministic: the particle list
/// is kept sorted by (cell, id), p2g accumulates in that order, jitter is a hash.
class FlipTransport final : public IWaterTransport {
public:
    static constexpr int    kParticlesPerCell = 8;
    static constexpr size_t kMaxParticlesPerVolume = 2000000;   ///< 250 k cells of water at 8 each (§15.9)
    explicit FlipTransport(float flipBlend = 0.95f) : m_flipBlend(flipBlend) {}
    const char* name() const override { return "flip"; }
    bool ownsMass() const override { return true; }
    void advect(WaterGrid& g, float dt) override;
    void setLiquidThreshold(float thr) override { m_liquidThreshold = thr; }
    void seed(WaterGrid& g) override;
    void settle(WaterGrid& g) override;
    double addVolume(WaterGrid& g, const glm::ivec3& cell, double m3, const glm::vec3& vel) override;
    double removeVolume(WaterGrid& g, const glm::ivec3& cell, double m3) override;
    size_t particleCount() const override { return m_particles.size(); }
    double ownedMass() const override;
    const std::vector<FlipParticle>& particles() const { return m_particles; }
    void setParticles(std::vector<FlipParticle> ps) { m_particles = std::move(ps); m_haveOldGrid = false; }   ///< the GPU backend's mirror (sorted)
    float flipBlend() const { return m_flipBlend; }
    /// Rebuild f (mass) and the face velocities from the particles (particle -> grid).
    void particlesToGrid(WaterGrid& g);
private:
    void sortParticles(const WaterGrid& g);
    float m_flipBlend;
    float m_liquidThreshold = 0.5f;
    uint32_t m_nextId = 0;
    bool m_haveOldGrid = false;
    std::vector<FlipParticle> m_particles;
    std::vector<float> m_uOld, m_vOld, m_wOld;      // the p2g velocities of the last substep (FLIP delta base)
    std::vector<float> m_uW, m_vW, m_wW;            // p2g weight accumulators
};

class EulerianTransport final : public IWaterTransport {
public:
    const char* name() const override { return "eulerian"; }
    void advect(WaterGrid& g, float dt) override;
    /// WaterCore.md 26.5 (P1): the ALL-FACES-AT-ONCE fill transport - the cause of the pond model's slow creek fixed (100 % of
    /// Manning's depth and speed vs 114 % with the face-by-face rule). THE DEFAULT since the owner signed off the pond A/B
    /// (2026-10-13); false = the old face-by-face rule, kept only as a test control and the A/B switch (water_transport).
    static inline bool s_allFacesAtOnce = true;
    /// 26.9: a QUIET surface rises by filling its cell (the donor-acceptor rule, 24.2 W1) - only where the face rises slower
    /// than this fraction of the one-cell free-fall speed sqrt(2 g h), g = 9.81 on both backends; 0 = off (W1 everywhere slowed
    /// waves, 24.6)
    /// 0.002 (measured 2026-10-14, 26.9): the settled threshold pond sleeps with no water lost, and every WaterWaveTest row
    /// keeps its number (mode 4 at top 0.5: 0.66, was 0.65; 0.005 cost it 0.59). ~5 mm/s at 1/3 m, ~3 mm/s at 1/9 m.
    static inline float s_quietRiseFraction = 0.002f;
    /// 26 P1: fill parked over full under a ceiling or at the box top since start (cell fills) - reported by the tests.
    static inline double s_overfullForTest = 0.0;
    void setLiquidThreshold(float thr) override { m_liquidThreshold = thr; }
private:
    struct FaceMove { size_t from, to; float amt; };
    std::vector<FaceMove> m_faces;   // 26 P1 scratch
    std::vector<double> m_out, m_acc;
    float m_liquidThreshold = 0.5f;   // a donor below it is a thin film: falls as a block through its floor face
    std::vector<float> m_scratch;
    std::vector<float> m_uNew, m_vNew, m_wNew;
};

// ── Phase D write-back (docs/WaterCore.md 16.2) ──────────────────────────────────────────────
// The world keeps water as COLUMN SPANS (Chunk::WaterSpanLocal, float tops). An active volume
// writes itself back as RUNS per world voxel column, a pure function of its cells:
//   * the per^2 sub-columns of each cell layer are averaged to one fill per layer, f̄(y);
//   * a layer is wet iff f̄·h >= WaterGrid::kSurfaceMinDepth (1 mm);
//   * a run is a maximal vertical sequence of wet layers; bottom = world Y of its lowest layer,
//     top = bottom + Σ f̄·h over the run; sub-millimetre layers (films in transit above a surface, a
//     forced snapshot of moving water) are folded into the column's top run, so the column is
//     MASS-EXACT by definition (Σ run depth × 1 m² = Σ f over the column, to float rounding); a
//     column with nothing but sub-millimetre water is residue, counted in `thinDropped`;
//   * the run's GEOMETRIC surface (highest wet layer's y + f̄·h) is reported against the mass top
//     as `surfaceVsMassMm`, and the sub-columns' surface spread as `spreadMm` — S11's "spans equal
//     the surface ± 1 mm" is this number, measured; an interior layer short of full shows here.
// Columns the caller marks HELD (Unknown occupancy anywhere in the column, or a non-resident
// chunk) are not written: they come back with `held = true`, no runs, and their mass counted
// separately — never a silent drop.
struct ColumnRun {
    float  bottomY = 0.0f;   ///< world Y (voxel units) of the run's base
    float  topY = 0.0f;      ///< world Y of the surface = bottomY + mass depth
    double mass = 0.0;       ///< m^3 in this run (depth × 1 m²)
};
struct ColumnRuns {
    int x = 0, z = 0;                ///< world voxel column
    std::vector<ColumnRun> runs;     ///< bottom-up
    float surfaceVsMassMm = 0.0f;    ///< max over runs |geometric surface − mass top|, mm
    float spreadMm = 0.0f;           ///< max − min sub-column surface over the column, mm (0 for per = 1)
    bool  held = false;              ///< not written (see above)
    double heldMass = 0.0;           ///< m^3 in a held column (reported, not written)
};
struct WriteBackStats {
    long   columns = 0, runs = 0, heldColumns = 0;
    double mass = 0.0;               ///< m^3 written (Σ run masses)
    double heldMass = 0.0;           ///< m^3 in held columns (stays in the volume)
    double thinDropped = 0.0;        ///< m^3 of columns holding only sub-millimetre water (no run to fold into): residue, counted, not stored
    float  surfaceVsMassMmMax = 0.0f, spreadMmMax = 0.0f;
};
/// Pure. `held(x, z)` (world voxel column) may be null = nothing held. The grid's box must be
/// voxel-aligned (every WaterCoreManager volume is).
WriteBackStats columnRunsFromGrid(const WaterGrid& grid, const std::function<bool(int, int)>& held,
                                  std::vector<ColumnRuns>& out);
/// The inverse: set every cell of `grid` from the runs (replace, not add; columns absent from
/// `runs` are cleared; runs are clipped to the grid's box). §27.8: each run is LEVEL-FILLED into the
/// voxel column's AIR cells (grid.occ) from its bottom up - still water is level, so this is the shape
/// it had; nothing goes into rock. Velocities are zeroed. Returns the m^3 placed.
double seedGridFromRuns(WaterGrid& grid, const std::vector<ColumnRuns>& runs);

class WaterSolver {
public:
    /// 26.9: the hydrostatic ghost at a clamped surface (surfaceGhost), CPU and GPU; false = the old p_air = 0 (the A/B,
    /// `/api/debug/water_transport {surface_ghost}`)
    static inline bool s_surfaceGhost = true;
    /// 26.20 diagnostic: the bed's Manning factor from the column's DEPTH-MEAN speed (one factor for every layer of a face column,
    /// the column law exactly) instead of each face's own speed. false = today. The stepped-bed rig's one variable.
    static inline bool s_bedManningColumnSpeedForTest = false;
    /// 26.21 A/B: the volume correction also under a THIN surface cell (any water above, not only a liquid cell)
    static inline bool s_volumeCorrectionUnderFilm = true;
    /// `refreshNow` false (39, A2f): skip the first refreshSolids - for a caller that writes the occupancy itself before the first step
    WaterSolver(WaterGrid& grid, SolidQuery solids, SolverParams params = SolverParams{}, bool refreshNow = true);

    void setTransport(std::unique_ptr<IWaterTransport> t);
    IWaterTransport& transport() { return *m_transport; }
    WaterGrid& grid() { return m_grid; }
    const SolverParams& params() const { return m_params; }
    void setPondFriction(float perSecond) { m_params.pondFriction = std::max(0.0f, perSecond); }   ///< 24.7 (the GPU backend reads params() each step)
    /// 25.39 H2: a creek window's bed friction - Manning's law, the column tier's, on every horizontal face: u /= 1 + dt g n^2
    /// |u| / d^(4/3), d the water's depth over the face (the mean of the two sub-columns). Replaces pond friction when > 0.
    /// CPU only in H2a (the GPU core does not read it - a window is a CPU volume).
    void setBedManning(float n) { m_params.bedManning = std::max(0.0f, n); }
    /// 26.21 the volume correction: time (s, 0 = off) and dead band (cell fraction); the GPU backend reads params() each step
    void setVolumeCorrection(float seconds, float band) { m_params.volumeCorrectionTime = std::max(0.0f, seconds); m_params.volumeCorrectionBand = std::clamp(band, 0.0f, 1.0f); }

    /// Re-query every cell's occupancy (Unknown -> hold). Called by step(); call after the world
    /// changed to apply it before the next tick.
    void refreshSolids();

    SourceSpec& addSource(const glm::ivec3& cellLocal, float rate);
    SourceSpec& addOutfall(const glm::ivec3& cellLocal);   ///< 26.13: see SourceSpec::outfall
    /// Phase G: set (or update) the prescribed surface of a sub-column; targetY in world units.
    BoundarySpec& setBoundary(const glm::ivec2& columnLocal, float targetY);
    void clearBoundaries() { m_boundaries.clear(); }
    const std::vector<BoundarySpec>& boundaries() const { return m_boundaries; }
    std::vector<BoundarySpec>& boundariesMutable() { return m_boundaries; }
    /// Write every prescribed column's fills to its target (the CPU form of wc_boundary); returns the m^3 exchanged.
    double applyBoundaries();
    void clearSources();
    const std::vector<SourceSpec>& sources() const { return m_sources; }
    std::vector<SourceSpec>& sourcesMutable() { return m_sources; }   ///< the GPU backend writes placedTotal/pending back (WaterCoreManager::stepGpu)

    /// Add `deltaSpeed` along `dir` to every face within `radius` of `worldPos`.
    void addImpulse(const glm::vec3& worldPos, float radius, float deltaSpeed, const glm::vec3& dir);
    /// Phase E1 (docs/WaterCore.md 19): a blast's shock. Every face within `reach` of `centre` gains
    /// speed s(r) = speedAtCentre * (1 - r / reach) along the outward radial direction blended
    /// `upBias` toward +Y (DamageSystem's push law: linear falloff, blasts throw things up). Each
    /// face's added speed is clamped to `maxSpeed` (a near-dry cell given an unbounded speed blows the
    /// CFL step and the projection) and the clamps are counted. Returns the number of faces kicked.
    struct RadialKick { long faces = 0; long clamped = 0; };
    RadialKick addRadialImpulse(const glm::vec3& centre, float reach, float speedAtCentre, float upBias, float maxSpeed = 20.0f);
    /// Phase E2: give the water `momentum` (m^3 * m/s, density 1) near `pos`: the water volume V of
    /// the liquid cells within `radius` gets du = momentum / V on every face bordering them (so the
    /// cell-centred momentum sum rises by `momentum`); |du| clamped to `maxSpeed`, counted. Returns the
    /// faces touched (0 when no water is within the radius - the momentum is then not applied).
    RadialKick addMomentum(const glm::vec3& pos, float radius, const glm::vec3& momentum, float maxSpeed = 20.0f);

    /// docs/WaterCore.md 20: the bodies taking up room in this volume for the next `frameSeconds` (call once
    /// per frame, before step()). Sets every cell's solid fraction s and its shed rate
    /// q = max(0, f + s - 1) V / T (20.2: the water that no longer fits, over the frame; no sink - the space a
    /// body leaves is a real cavity). q per cell is clamped to (open faces) x h^2 x kSolidMaxSpeed: a source
    /// walled in on most sides would otherwise demand an outflow speed that blows the CFL substep count and
    /// the projection; what is not shed is still there next frame (never dropped). Cells of a FRESH body
    /// (spawned / teleported) get no rate - a column ledger moves their excess up instead (20.7: no fake
    /// splash). Wakes the volume when anything moved or must be shed.
    struct SolidsReport {
        SolidRaster raster;
        long rateCells = 0, clamped = 0, freshCells = 0, wakeCells = 0;   ///< wakeCells: where trapped air may rise out (project)
        double rate = 0.0;      ///< m^3/s summed over cells (after clamps)
        double sChange = 0.0;   ///< m^3: sum |s' - s| V since the last call
    };
    static constexpr float kSolidMaxSpeed = 20.0f;   ///< m/s, the same bound the blast and momentum kicks use
    static constexpr int   kSolidWakeCells = 3;      ///< cells round a body's vacated cells where its dragged air may rise out (project) - DiagCavity: a stone's air spreads up to 3 cells from its path as the cavity collapses
    /// The wake lasts as long as trapped air needs to rise through the volume's full height: ny h / kTrappedAirRiseSpeed
    /// (a fixed 5 s let a 1.5 m pond keep 10 % of a stone's air at 6 s, still draining - DiagCavity).
    float solidWakeSeconds() const { return static_cast<float>(m_grid.ny()) * m_grid.h() / kTrappedAirRiseSpeed; }
    static constexpr float kTrappedAirRiseSpeed = 0.25f;   ///< m/s - small trapped bubbles rise at 0.2-0.3 m/s (Clift, Grace & Weber)
    SolidsReport setMovingSolids(const std::vector<MovingSolid>& bodies, float frameSeconds);
    /// The two halves of setMovingSolids (M1): the fields that do not read the water (s, the wake, fresh
    /// bodies) and the rates that do (q, the ledger marks). A GPU volume runs the first here and the
    /// second on the device, from its own fill.
    SolidsReport updateSolidFields(const std::vector<MovingSolid>& bodies, float frameSeconds);
    void computeSolidRates(float frameSeconds, SolidsReport& rep);
    const std::vector<float>& solidWake() const { return m_solidWake; }
    const std::vector<uint8_t>& solidFresh() const { return m_solidFresh; }
    /// 26.12: the inclusive grid-cell box the last updateSolidFields could change (s, the wake, fresh); outside it all three are 0
    /// and were 0. Empty when hi < lo. A GPU volume uploads only the bricks it touches.
    struct CellBox { glm::ivec3 lo{0}, hi{-1}; bool empty() const { return hi.x < lo.x || hi.y < lo.y || hi.z < lo.z; } };
    const CellBox& solidActiveBox() const { return m_solidActive; }
    /// 44.5: the cells the last updateSolidFields changed other than by the wake's aging (the bodies' cells now and before, grown by
    /// kSolidWakeCells) - a GPU volume copies only these and ages the wake on the device
    const CellBox& solidChangedBox() const { return m_solidChanged; }
    /// 26.12 test hook: updateSolidFields over the whole grid every call (the reference the box must equal bit for bit).
    static inline bool s_solidFieldsWhole = false;

    /// One engine tick of `dt` seconds, substepped internally to honour the CFL fraction.
    /// A sleeping volume returns immediately (asleep = true, nothing touched).
    StepReport step(float dt);
    const StepReport& lastReport() const { return m_last; }

    bool asleep() const { return m_asleep; }
    double pressureAt(int x, int y, int z) const { return m_p.empty() ? 0.0 : m_p[m_grid.idx(x, y, z)]; }   ///< last projection's pressure (Pa, rho = 1), diagnostics
    void wake() { m_asleep = false; m_quietTicks = 0; }

    /// Pressure from the last projection (Pa with density 1, i.e. m^2/s^2); 0 outside liquid.
    double pressure(int x, int y, int z) const;

private:
    void applySources(float dt, StepReport& r);
    void applyGravity(float dt);
    void applyThinFilmGradient(float dt);
    void settleThinFilmTopFaces();
    void project(float dt, StepReport& r);
    void extrapolateVelocity();
    void enforceSolidFaces();
    bool bodyWall(int x, int y, int z) const;   // 20.5: a cell full of body with no real water - a wall, not air
    void applyRestDamping(float dt);
    void applyPondFriction(float dt);
    void applyBedManning(float dt);
    void compactSubmergedPartials(float dt, bool settling);   // water above a partial liquid cell falls into it (free-fall capped)
    void sweepResidue(StepReport& r);   // merge sub-epsilon films, count what cannot be merged
    void resolveFreshOverflow();        // 20.5 ledger (a): a fresh body's excess moves up its own column
    int  substepsFor(float dt) const;
    double thetaToAir(int x, int y, int z, int dir) const;
    double surfaceGhost(int x, int y, int z, int dir) const;   ///< 26.9: the ghost pressure across an air face (0 unless the surface is clamped)
    double maxSpeed() const;

    WaterGrid& m_grid;
    SolidQuery m_solids;
    SolverParams m_params;
    std::unique_ptr<IWaterTransport> m_transport;
    std::vector<SourceSpec> m_sources;
public:
    /// DIAGNOSTIC (26 P1): called after each stage of a substep with the stage's name - the momentum budget's probe. Unset in the engine.
    std::function<void(const char*)> stageProbeForTest;
private:
    std::vector<uint8_t> m_solidFresh;   // 20: cells of bodies that appeared this frame
    std::vector<float> m_solidWake;      // 20: seconds left in which a body's dragged air may rise out of this cell
    std::vector<float> m_sNew;           // 26.12: the raster's scratch, kept (zero outside the box it last used)
    CellBox m_solidBox;                  // 26.12: every cell with s, wake or fresh non-zero lies inside it
    CellBox m_solidChanged, m_rasterPrev;   // 44.5: see solidChangedBox; the bodies' raster cells last call
    CellBox m_solidActive;               // 26.12: the box the last call worked in
    std::vector<BoundarySpec> m_boundaries;   // Phase G
    std::vector<double> m_p, m_rhs, m_z, m_s, m_r, m_diag;   // projection scratch (double)
    std::vector<float> m_fPrev;
    StepReport m_last;
    int  m_quietTicks = 0;
    bool m_asleep = false;
};

/// Ritter's dam-break front speed for still depth h0 (frictionless, dry bed): 2 * sqrt(g * h0).
inline double ritterFrontSpeed(double h0, double g = 9.81) { return 2.0 * std::sqrt(g * h0); }
/// Torricelli discharge through an orifice of area A under head h with coefficient cd (0.62 sharp-edged).
inline double torricelliFlow(double area, double head, double cd = 0.62, double g = 9.81) { return cd * area * std::sqrt(2.0 * g * head); }

} // namespace Water
} // namespace Core
} // namespace Phyxel
