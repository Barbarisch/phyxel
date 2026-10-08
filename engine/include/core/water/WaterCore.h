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
    float surfaceWorldY(int x, int z) const;
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
    float  liquidThreshold = 0.999f;
    double keWake = 1e-4;            ///< below this kinetic energy the volume counts as quiet
    double maxDeltaFQuiet = 1e-5;    ///< ... and when max |delta f| per tick is below this
    int    restTicks = 30;           ///< consecutive quiet ticks before sleeping
    float  restDamping = 0.5f;       ///< 1/s, applied to velocity ONLY while quiet (never to moving water)
    int    pcgMaxIters = 400;
    double pcgTolerance = 1e-6;      ///< relative residual
};

struct SourceSpec {
    glm::ivec3 cell{0, 0, 0};        ///< grid-local cell
    float  rate = 0.0f;              ///< m^3/s; negative = sink
    double placedTotal = 0.0;        ///< m^3 actually added (or removed, negative)
    double unplaced = 0.0;           ///< m^3 the cell could not accept this tick (reported, not lost)
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
    double sourceUnplaced = 0.0;
    int    quietTicks = 0;
    bool   asleep = false;
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
};

class EulerianTransport final : public IWaterTransport {
public:
    const char* name() const override { return "eulerian"; }
    void advect(WaterGrid& g, float dt) override;
private:
    std::vector<float> m_scratch;
    std::vector<float> m_uNew, m_vNew, m_wNew;
};

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
    void clearSources();
    const std::vector<SourceSpec>& sources() const { return m_sources; }

    /// Add `deltaSpeed` along `dir` to every face within `radius` of `worldPos`.
    void addImpulse(const glm::vec3& worldPos, float radius, float deltaSpeed, const glm::vec3& dir);

    /// One engine tick of `dt` seconds, substepped internally to honour the CFL fraction.
    /// A sleeping volume returns immediately (asleep = true, nothing touched).
    StepReport step(float dt);
    const StepReport& lastReport() const { return m_last; }

    bool asleep() const { return m_asleep; }
    void wake() { m_asleep = false; m_quietTicks = 0; }

    /// Pressure from the last projection (Pa with density 1, i.e. m^2/s^2); 0 outside liquid.
    double pressure(int x, int y, int z) const;

private:
    void applySources(float dt, StepReport& r);
    void applyGravity(float dt);
    void project(float dt, StepReport& r);
    void extrapolateVelocity();
    void enforceSolidFaces();
    void applyRestDamping(float dt);
    int  substepsFor(float dt) const;
    double maxSpeed() const;

    WaterGrid& m_grid;
    SolidQuery m_solids;
    SolverParams m_params;
    std::unique_ptr<IWaterTransport> m_transport;
    std::vector<SourceSpec> m_sources;
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
