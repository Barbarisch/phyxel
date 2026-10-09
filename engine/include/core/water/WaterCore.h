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
    float surfaceWorldY(int x, int z) const;          ///< world y of the surface (NaN when the column holds no surface)
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
    double keSettle = 1e-3;          ///< m^2/s^2 (|v| ~ 3 cm/s rms): below this the volume is settling and restDamping stands in for the viscous + bottom-friction dissipation the inviscid solver lacks. Measured need (Phase D, 2026-10-08): with damping only under keWake, a poured column, a bumped pool and a dam break in a sealed 3 m tank at 1/3 m all sloshed at 1e-4..1e-3 for 100 s and never slept; real basins still within seconds to tens of seconds. From 3 cm/s to the 1.4 mm/s sleep line takes ln(23)/0.5 = 6 s: "asleep within 10 s of the last motion" (S11).
    /// DIAGNOSTIC ONLY (bisecting a measured defect, never shipped on): bits disable a stage of
    /// the substep. 1 compaction · 2 thin-film slope · 4 thin-film settle · 8 "first halo layer only"
    /// (all three layers overwrite thin faces, the pre-#11 behaviour) · 16 residue sweep · 32 rest damping.
    uint32_t debugDisableStages = 0;
    double thetaMin = 0.1;           ///< ghost-fluid clamp: the surface is never closer than this fraction of a cell to a liquid cell's centre (1/theta stays finite); see thetaToAir
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
    void setLiquidThreshold(float thr) override { m_liquidThreshold = thr; }
private:
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
/// `runs` are cleared; runs are clipped to the grid's box; every sub-column of a voxel column
/// gets the same fill, a flat start). Velocities are zeroed. Returns the m^3 placed.
double seedGridFromRuns(WaterGrid& grid, const std::vector<ColumnRuns>& runs);

class WaterSolver {
public:
    WaterSolver(WaterGrid& grid, SolidQuery solids, SolverParams params = SolverParams{});

    void setTransport(std::unique_ptr<IWaterTransport> t);
    IWaterTransport& transport() { return *m_transport; }
    WaterGrid& grid() { return m_grid; }
    const SolverParams& params() const { return m_params; }

    /// Re-query every cell's occupancy (Unknown -> hold). Called by step(); call after the world
    /// changed to apply it before the next tick.
    void refreshSolids();

    SourceSpec& addSource(const glm::ivec3& cellLocal, float rate);
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
    void applyRestDamping(float dt);
    void compactSubmergedPartials(float dt);   // water above a partial liquid cell falls into it (free-fall capped)
    void sweepResidue(StepReport& r);   // merge sub-epsilon films, count what cannot be merged
    int  substepsFor(float dt) const;
    double thetaToAir(int x, int y, int z, int dir) const;
    double maxSpeed() const;

    WaterGrid& m_grid;
    SolidQuery m_solids;
    SolverParams m_params;
    std::unique_ptr<IWaterTransport> m_transport;
    std::vector<SourceSpec> m_sources;
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
