// WaterCore — docs/WaterCore.md §15. The small-scale water solver, CPU reference (Phase B).
//
// One active volume: a MAC grid of fill fractions f (0..1 per cell) and face velocities. The tick
// (§15.1): refresh solids → sources/sinks → gravity → conservative donor-cell VOF transport +
// semi-Lagrangian velocity → pressure projection on Liquid cells with ghost-fluid free surface →
// velocity extrapolation → rest damping (quiet water only) → rest detection → sleep.
//
// Everything that must be exact is exact by construction: a face transfers ONE amount that is
// subtracted from the donor and added to the receiver (P1); a Solid or Unknown neighbour has its
// face velocity forced to zero before transport and after projection (P5); a sleeping volume is
// not touched (P7). Numbers that are approximations (PCG tolerance, ghost-fluid θ, CFL fraction)
// are parameters with their trade-off stated at the parameter.
#include "core/water/WaterCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Phyxel {
namespace Core {
namespace Water {

// ─────────────────────────────────────────────────────────────────── WaterGrid ──────────────
WaterGrid::WaterGrid(const GridSpec& spec) : m_spec(spec) {
    const size_t n = cellCount();
    m_f.assign(n, 0.0f);
    m_occ.assign(n, Occ::Air);
    m_kind.assign(n, CellKind::Empty);
    m_u.assign(static_cast<size_t>(nx() + 1) * ny() * nz(), 0.0f);
    m_v.assign(static_cast<size_t>(nx()) * (ny() + 1) * nz(), 0.0f);
    m_w.assign(static_cast<size_t>(nx()) * ny() * (nz() + 1), 0.0f);
}

void WaterGrid::fillBox(const glm::ivec3& lo, const glm::ivec3& hi, float fill) {
    for (int z = std::max(0, lo.z); z <= std::min(nz() - 1, hi.z); ++z)
        for (int y = std::max(0, lo.y); y <= std::min(ny() - 1, hi.y); ++y)
            for (int x = std::max(0, lo.x); x <= std::min(nx() - 1, hi.x); ++x)
                m_f[idx(x, y, z)] = fill;
}

void WaterGrid::classify(float liquidThreshold) {
    for (size_t i = 0; i < m_f.size(); ++i)
        m_kind[i] = m_f[i] >= liquidThreshold ? CellKind::Liquid : (m_f[i] > 0.0f ? CellKind::Surface : CellKind::Empty);
}

double WaterGrid::totalMass() const {
    double s = 0.0;
    for (float v : m_f) s += v;
    return s * cellVolume();
}

double WaterGrid::columnMass(int x, int z) const {
    double s = 0.0;
    for (int y = 0; y < ny(); ++y) s += m_f[idx(x, y, z)];
    return s * cellVolume();
}

float WaterGrid::surfaceWorldY(int x, int z) const {
    for (int y = ny() - 1; y >= 0; --y) {
        const float fv = m_f[idx(x, y, z)];
        if (fv > 0.0f) return (static_cast<float>(m_spec.origin.y + y) + std::min(fv, 1.0f)) * m_spec.h;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

glm::vec3 WaterGrid::cellCenterWorld(int x, int y, int z) const {
    return (glm::vec3(m_spec.origin) + glm::vec3(x, y, z) + 0.5f) * m_spec.h;
}

glm::ivec3 WaterGrid::worldToCell(const glm::vec3& world) const {
    const glm::vec3 c = glm::floor(world / m_spec.h);
    return glm::ivec3(c) - m_spec.origin;
}

double WaterGrid::kineticEnergy() const {
    double e = 0.0;
    for (int z = 0; z < nz(); ++z)
        for (int y = 0; y < ny(); ++y)
            for (int x = 0; x < nx(); ++x) {
                const float fv = m_f[idx(x, y, z)];
                if (fv <= 0.0f) continue;
                const float ux = 0.5f * (u(x, y, z) + u(x + 1, y, z));
                const float vy = 0.5f * (v(x, y, z) + v(x, y + 1, z));
                const float wz = 0.5f * (w(x, y, z) + w(x, y, z + 1));
                e += 0.5 * fv * (ux * ux + vy * vy + wz * wz);
            }
    return e * cellVolume();
}

std::vector<std::pair<glm::ivec2, double>> WaterGrid::massPerVoxelColumn() const {
    std::vector<std::pair<glm::ivec2, double>> out;
    const int per = std::max(1, static_cast<int>(std::lround(1.0f / m_spec.h)));   // cells per voxel per axis
    for (int z = 0; z < nz(); ++z)
        for (int x = 0; x < nx(); ++x) {
            const int wx = static_cast<int>(std::floor(static_cast<float>(m_spec.origin.x + x) / per));
            const int wz = static_cast<int>(std::floor(static_cast<float>(m_spec.origin.z + z) / per));
            const double m = columnMass(x, z);
            bool found = false;
            for (auto& e : out) if (e.first.x == wx && e.first.y == wz) { e.second += m; found = true; break; }
            if (!found) out.push_back({glm::ivec2(wx, wz), m});
        }
    return out;
}

// ─────────────────────────────────────────────────────────────── EulerianTransport ──────────
namespace {

inline bool blocked(const WaterGrid& g, int x, int y, int z) {
    return !g.inBounds(x, y, z) || g.occ(x, y, z) != Occ::Air;
}

// Trilinear sample of a MAC component stored at face positions. `pos` is in cell units of the
// component's own lattice (so a u sample at face (i, j+0.5, k+0.5) uses pos = (i, j, k)).
float sampleLattice(const std::vector<float>& a, int sx, int sy, int sz, glm::vec3 pos) {
    pos.x = std::clamp(pos.x, 0.0f, static_cast<float>(sx - 1));
    pos.y = std::clamp(pos.y, 0.0f, static_cast<float>(sy - 1));
    pos.z = std::clamp(pos.z, 0.0f, static_cast<float>(sz - 1));
    const int x0 = std::min(static_cast<int>(pos.x), sx - 2 < 0 ? 0 : sx - 2), y0 = std::min(static_cast<int>(pos.y), sy - 2 < 0 ? 0 : sy - 2), z0 = std::min(static_cast<int>(pos.z), sz - 2 < 0 ? 0 : sz - 2);
    const int x1 = std::min(x0 + 1, sx - 1), y1 = std::min(y0 + 1, sy - 1), z1 = std::min(z0 + 1, sz - 1);
    const float tx = pos.x - x0, ty = pos.y - y0, tz = pos.z - z0;
    auto at = [&](int x, int y, int z) { return a[static_cast<size_t>(x) + static_cast<size_t>(sx) * (static_cast<size_t>(y) + static_cast<size_t>(sy) * z)]; };
    const float c00 = at(x0, y0, z0) * (1 - tx) + at(x1, y0, z0) * tx;
    const float c10 = at(x0, y1, z0) * (1 - tx) + at(x1, y1, z0) * tx;
    const float c01 = at(x0, y0, z1) * (1 - tx) + at(x1, y0, z1) * tx;
    const float c11 = at(x0, y1, z1) * (1 - tx) + at(x1, y1, z1) * tx;
    const float c0 = c00 * (1 - ty) + c10 * ty, c1 = c01 * (1 - ty) + c11 * ty;
    return c0 * (1 - tz) + c1 * tz;
}

// Cell-centred velocity in cell units (per second) at an arbitrary position in cell coordinates.
glm::vec3 velocityAt(const WaterGrid& g, const glm::vec3& cellPos) {
    const int nx = g.nx(), ny = g.ny(), nz = g.nz();
    // u lives at (i, j+0.5, k+0.5) → lattice coords (x, y-0.5, z-0.5)
    const float ux = sampleLattice(const_cast<WaterGrid&>(g).uData(), nx + 1, ny, nz, glm::vec3(cellPos.x, cellPos.y - 0.5f, cellPos.z - 0.5f));
    const float vy = sampleLattice(const_cast<WaterGrid&>(g).vData(), nx, ny + 1, nz, glm::vec3(cellPos.x - 0.5f, cellPos.y, cellPos.z - 0.5f));
    const float wz = sampleLattice(const_cast<WaterGrid&>(g).wData(), nx, ny, nz + 1, glm::vec3(cellPos.x - 0.5f, cellPos.y - 0.5f, cellPos.z));
    return glm::vec3(ux, vy, wz) / g.h();
}

} // namespace

void EulerianTransport::advect(WaterGrid& g, float dt) {
    const int nx = g.nx(), ny = g.ny(), nz = g.nz();
    const float h = g.h();
    std::vector<float>& f = g.fData();

    // ── fill transport: donor-cell flux, applied face by face, exactly conservative ──
    // Each face moves ONE amount = min(desired, what the donor still has, what the receiver can
    // still take), subtracted and added as the same number, in a fixed face order. No clamp ever
    // touches f, so nothing is created or destroyed (P1). The fixed order is a mild bias when
    // limits bind (deterministic by construction); the two-pass scaled limiter it replaces
    // destroyed mass through its final clamp once a receiver's assumed outflow was itself
    // throttled (second green attempt, 2026-10-08: 832 -> 825.5 m^3 in 1000 ticks).
    // Faces are visited bottom-up along y so a draining cell frees room before it receives.
    m_scratch.assign(f.begin(), f.end());
    std::vector<float>& g_ = m_scratch;
    auto moveFace = [&](size_t ia, size_t ib, float vel, float fDonorOrig) {
        if (vel == 0.0f || fDonorOrig <= 0.0f) return;
        const size_t from = vel > 0.0f ? ia : ib, to = vel > 0.0f ? ib : ia;
        const float desired = std::abs(vel) * dt / h * fDonorOrig;
        const float moved = std::min({desired, g_[from], 1.0f - g_[to]});
        if (moved <= 0.0f) return;
        g_[from] -= moved;
        g_[to]   += moved;
    };
    for (int y = 0; y < ny; ++y) {
        for (int z = 0; z < nz; ++z) for (int x = 1; x < nx; ++x) {
            if (blocked(g, x - 1, y, z) || blocked(g, x, y, z)) continue;
            const float vel = g.u(x, y, z);
            moveFace(g.idx(x - 1, y, z), g.idx(x, y, z), vel, vel > 0.0f ? f[g.idx(x - 1, y, z)] : f[g.idx(x, y, z)]);
        }
        for (int z = 1; z < nz; ++z) for (int x = 0; x < nx; ++x) {
            if (blocked(g, x, y, z - 1) || blocked(g, x, y, z)) continue;
            const float vel = g.w(x, y, z);
            moveFace(g.idx(x, y, z - 1), g.idx(x, y, z), vel, vel > 0.0f ? f[g.idx(x, y, z - 1)] : f[g.idx(x, y, z)]);
        }
        if (y >= 1) for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
            if (blocked(g, x, y - 1, z) || blocked(g, x, y, z)) continue;
            const float vel = g.v(x, y, z);
            moveFace(g.idx(x, y - 1, z), g.idx(x, y, z), vel, vel > 0.0f ? f[g.idx(x, y - 1, z)] : f[g.idx(x, y, z)]);
        }
    }
    f.swap(m_scratch);

    // ── velocity transport: semi-Lagrangian back-trace of each face sample ──
    const size_t nu = g.uData().size(), nv = g.vData().size(), nw = g.wData().size();
    m_uNew.assign(nu, 0.0f); m_vNew.assign(nv, 0.0f); m_wNew.assign(nw, 0.0f);
    const float dtc = dt / h;   // seconds → cell units when multiplied by a velocity in m/s... velocityAt returns cells/s
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x <= nx; ++x) {
        const glm::vec3 p(static_cast<float>(x), y + 0.5f, z + 0.5f);
        const glm::vec3 vel = velocityAt(g, p);
        m_uNew[g.uIdx(x, y, z)] = sampleLattice(g.uData(), nx + 1, ny, nz, glm::vec3(p.x - vel.x * dtc * h, p.y - 0.5f - vel.y * dtc * h, p.z - 0.5f - vel.z * dtc * h));
    }
    for (int z = 0; z < nz; ++z) for (int y = 0; y <= ny; ++y) for (int x = 0; x < nx; ++x) {
        const glm::vec3 p(x + 0.5f, static_cast<float>(y), z + 0.5f);
        const glm::vec3 vel = velocityAt(g, p);
        m_vNew[g.vIdx(x, y, z)] = sampleLattice(g.vData(), nx, ny + 1, nz, glm::vec3(p.x - 0.5f - vel.x * dtc * h, p.y - vel.y * dtc * h, p.z - 0.5f - vel.z * dtc * h));
    }
    for (int z = 0; z <= nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        const glm::vec3 p(x + 0.5f, y + 0.5f, static_cast<float>(z));
        const glm::vec3 vel = velocityAt(g, p);
        m_wNew[g.wIdx(x, y, z)] = sampleLattice(g.wData(), nx, ny, nz + 1, glm::vec3(p.x - 0.5f - vel.x * dtc * h, p.y - 0.5f - vel.y * dtc * h, p.z - vel.z * dtc * h));
    }
    g.uData().swap(m_uNew); g.vData().swap(m_vNew); g.wData().swap(m_wNew);
}

// ─────────────────────────────────────────────────────────────────── WaterSolver ─────────────
WaterSolver::WaterSolver(WaterGrid& grid, SolidQuery solids, SolverParams params)
    : m_grid(grid), m_solids(std::move(solids)), m_params(params),
      m_transport(std::make_unique<EulerianTransport>()) {
    refreshSolids();
}

void WaterSolver::setTransport(std::unique_ptr<IWaterTransport> t) { if (t) m_transport = std::move(t); }

void WaterSolver::refreshSolids() {
    const glm::ivec3 o = m_grid.spec().origin;
    for (int z = 0; z < m_grid.nz(); ++z)
        for (int y = 0; y < m_grid.ny(); ++y)
            for (int x = 0; x < m_grid.nx(); ++x) {
                const Occ occ = m_solids ? m_solids(o + glm::ivec3(x, y, z)) : Occ::Air;
                m_grid.occ(x, y, z) = occ;
                if (occ != Occ::Air) m_grid.f(x, y, z) = 0.0f;   // a solid cell never holds water
            }
    enforceSolidFaces();
}

SourceSpec& WaterSolver::addSource(const glm::ivec3& cellLocal, float rate) {
    m_sources.push_back(SourceSpec{cellLocal, rate, 0.0, 0.0});
    wake();
    return m_sources.back();
}

void WaterSolver::clearSources() { m_sources.clear(); }

void WaterSolver::addImpulse(const glm::vec3& worldPos, float radius, float deltaSpeed, const glm::vec3& dir) {
    const glm::vec3 d = glm::length(dir) > 0.0f ? glm::normalize(dir) : glm::vec3(0.0f);
    const float r2 = radius * radius;
    const float h = m_grid.h();
    for (int z = 0; z < m_grid.nz(); ++z) for (int y = 0; y < m_grid.ny(); ++y) for (int x = 0; x <= m_grid.nx(); ++x) {
        const glm::vec3 p = (glm::vec3(m_grid.spec().origin) + glm::vec3(x, y + 0.5f, z + 0.5f)) * h;
        if (glm::dot(p - worldPos, p - worldPos) <= r2) m_grid.u(x, y, z) += deltaSpeed * d.x;
    }
    for (int z = 0; z < m_grid.nz(); ++z) for (int y = 0; y <= m_grid.ny(); ++y) for (int x = 0; x < m_grid.nx(); ++x) {
        const glm::vec3 p = (glm::vec3(m_grid.spec().origin) + glm::vec3(x + 0.5f, y, z + 0.5f)) * h;
        if (glm::dot(p - worldPos, p - worldPos) <= r2) m_grid.v(x, y, z) += deltaSpeed * d.y;
    }
    for (int z = 0; z <= m_grid.nz(); ++z) for (int y = 0; y < m_grid.ny(); ++y) for (int x = 0; x < m_grid.nx(); ++x) {
        const glm::vec3 p = (glm::vec3(m_grid.spec().origin) + glm::vec3(x + 0.5f, y + 0.5f, z)) * h;
        if (glm::dot(p - worldPos, p - worldPos) <= r2) m_grid.w(x, y, z) += deltaSpeed * d.z;
    }
    wake();
}

double WaterSolver::pressure(int x, int y, int z) const {
    if (!m_grid.inBounds(x, y, z) || m_p.empty()) return 0.0;
    return m_p[m_grid.idx(x, y, z)];
}

void WaterSolver::applySources(float dt, StepReport& r) {
    const float vol = m_grid.cellVolume();
    for (SourceSpec& s : m_sources) {
        if (!m_grid.inBounds(s.cell.x, s.cell.y, s.cell.z) || m_grid.occ(s.cell.x, s.cell.y, s.cell.z) != Occ::Air) continue;
        float& fc = m_grid.f(s.cell.x, s.cell.y, s.cell.z);
        const double want = static_cast<double>(s.rate) * dt / vol;     // fill units this substep
        if (want >= 0.0) {
            const double room = std::max(0.0, 1.0 - static_cast<double>(fc));
            const double placed = std::min(want, room);
            fc = static_cast<float>(fc + placed);
            s.placedTotal += placed * vol; s.unplaced = (want - placed) * vol;
            r.sourceAdded += placed * vol; r.sourceUnplaced += (want - placed) * vol;
        } else {
            const double take = std::min(-want, static_cast<double>(fc));
            fc = static_cast<float>(fc - take);
            s.placedTotal -= take * vol;
            r.sinkRemoved += take * vol;
        }
    }
}

void WaterSolver::applyGravity(float dt) {
    // Gravity acts on every face that carries water on at least one side: the top face of a
    // resting pool needs it to balance the pressure push from below (removing it launched the
    // pool upward, 2026-10-08). Faces of THIN cells (outside the projection) are made consistent
    // afterwards by settleThinFilmTopFaces(), so nothing accumulates there.
    for (int z = 0; z < m_grid.nz(); ++z)
        for (int y = 0; y <= m_grid.ny(); ++y)
            for (int x = 0; x < m_grid.nx(); ++x) {
                const bool below = y > 0 && m_grid.f(x, y - 1, z) > 0.0f;
                const bool above = y < m_grid.ny() && m_grid.f(x, y, z) > 0.0f;
                if (below || above) m_grid.v(x, y, z) -= m_params.gravity * dt;
            }
}

// A thin cell (0 < f < liquidThreshold) is outside the pressure projection, so its TOP face would
// otherwise integrate -g*dt forever with nothing above to fall (the front trace showed -1.5 m/s on
// such faces and 80-130 Pa pressure spikes the moment the cell joined the domain, 2026-10-08).
// Make the film move as a block: its top face takes its bottom face's velocity - zero when it
// rests on a solid floor (hydrostatic rest), the projected value when it sits on liquid, and the
// falling velocity when it is a drop in air. Only faces with AIR above are touched; a face into a
// wet cell above belongs to the pair's own dynamics.
void WaterSolver::settleThinFilmTopFaces() {
    const float thr = m_params.liquidThreshold;
    for (int z = 0; z < m_grid.nz(); ++z)
        for (int y = 0; y < m_grid.ny(); ++y)
            for (int x = 0; x < m_grid.nx(); ++x) {
                const float fc = m_grid.f(x, y, z);
                if (fc <= 0.0f || fc >= thr) continue;
                // the face above belongs to the projection only if the upper cell is a domain cell;
                // otherwise (air OR another thin cell) nothing ever resets it, so it is ours
                const bool upperIsDomain = (y + 1 < m_grid.ny()) && m_grid.occ(x, y + 1, z) == Occ::Air && m_grid.f(x, y + 1, z) >= thr;
                if (upperIsDomain) continue;
                if (y + 1 < m_grid.ny() && blocked(m_grid, x, y + 1, z)) continue;   // solid above: face is already forced to 0
                const bool solidBelow = blocked(m_grid, x, y - 1, z);
                const float vBottom = solidBelow ? 0.0f : m_grid.v(x, y, z);
                m_grid.v(x, y + 1, z) = vBottom;   // y ascends in the loop, so a stack of thin cells copies the floor upward
            }
}

// Cells thinner than half a cell are outside the pressure domain, so the projection cannot see
// the one force that drives a thin film: the slope of its own surface (shallow water:
// du/dt = -g dh/dx). Apply it explicitly, locally, on faces where NO pressure-domain cell is
// involved (those faces are the projection's). Measured need: on the fine dam break the profile
// matched Ritter to 5 % down to 0.7 m depth and then fell to zero within one metre - the 0.5 m
// tongue Ritter predicts out to x = 21 m never formed (2026-10-08).
void WaterSolver::applyThinFilmGradient(float dt) {
    const int nx = m_grid.nx(), ny = m_grid.ny(), nz = m_grid.nz();
    const float thr = m_params.liquidThreshold;
    const float g = m_params.gravity;
    auto slope = [&](int xa, int ya, int za, int xb, int yb, int zb, float& vel) {
        if (blocked(m_grid, xa, ya, za) || blocked(m_grid, xb, yb, zb)) return;
        const float fa = m_grid.f(xa, ya, za), fb = m_grid.f(xb, yb, zb);
        if (fa >= thr || fb >= thr) return;          // a domain cell: the projection owns this face
        if (fa <= 0.0f && fb <= 0.0f) return;        // dry
        // only a film resting on something (solid or water below) has a hydrostatic surface slope
        const bool restsA = ya == 0 || blocked(m_grid, xa, ya - 1, za) || m_grid.f(xa, ya - 1, za) >= thr;
        const bool restsB = yb == 0 || blocked(m_grid, xb, yb - 1, zb) || m_grid.f(xb, yb - 1, zb) >= thr;
        if (!restsA && !restsB) return;
        vel -= g * dt * (fb - fa);                   // d(depth)/dx with depth = f*h over dx = h
    };
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 1; x < nx; ++x)
        slope(x - 1, y, z, x, y, z, m_grid.u(x, y, z));
    for (int z = 1; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x)
        slope(x, y, z - 1, x, y, z, m_grid.w(x, y, z));
}

void WaterSolver::enforceSolidFaces() {
    const int nx = m_grid.nx(), ny = m_grid.ny(), nz = m_grid.nz();
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x <= nx; ++x)
        if (blocked(m_grid, x - 1, y, z) || blocked(m_grid, x, y, z)) m_grid.u(x, y, z) = 0.0f;
    for (int z = 0; z < nz; ++z) for (int y = 0; y <= ny; ++y) for (int x = 0; x < nx; ++x)
        if (blocked(m_grid, x, y - 1, z) || blocked(m_grid, x, y, z)) m_grid.v(x, y, z) = 0.0f;
    for (int z = 0; z <= nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x)
        if (blocked(m_grid, x, y, z - 1) || blocked(m_grid, x, y, z)) m_grid.w(x, y, z) = 0.0f;
}

// Pressure projection on Liquid cells (f >= liquidThreshold): solve  Σ_faces (p_c − p_n)/h² = −∇·u/dt
// with p = 0 at the free surface (ghost fluid: an Empty/Surface neighbour contributes 1/θ where θ
// is the fraction of a cell from the liquid centre to the surface — 0.5 at a face, 0.5 + fill of
// a Surface cell above), and no term for Solid/Unknown neighbours (Neumann). CG with a Jacobi
// preconditioner; deterministic ordering; tolerance relative to |rhs|.
// Fraction of a cell from a liquid cell's centre to the free surface across face `dir`
// (0:-x 1:+x 2:-y 3:+y 4:-z 5:+z): 0.5 at the face itself; above a liquid cell a partially filled
// Surface cell pushes the surface up by its fill.
double WaterSolver::thetaToAir(int x, int y, int z, int dir) const {
    if (dir == 3) {
        const double fa = m_grid.inBounds(x, y + 1, z) ? std::min(1.0, static_cast<double>(m_grid.f(x, y + 1, z))) : 0.0;
        if (fa > 0.0) return 0.5 + fa;                        // a thin cell above lifts the surface
        // Air above: the surface is INSIDE this cell at its fill height, (f - 0.5) above the centre.
        // With a fixed 0.5 every partially filled cell of a thinning tongue reported the same
        // surface height, the solver saw a flat tongue with no horizontal gradient, and the fine
        // dam break stopped dead at the 0.5 contour (profile, 2026-10-08). Clamped away from zero:
        // theta -> 0 is the correct limit (p -> 0 at the centre) but 1/theta must stay finite.
        const double fc = static_cast<double>(m_grid.f(x, y, z));
        return std::clamp(fc - 0.5, 0.1, 0.5);
    }
    return 0.5;
}

void WaterSolver::project(float dt, StepReport& r) {
    const int nx = m_grid.nx(), ny = m_grid.ny(), nz = m_grid.nz();
    const size_t n = m_grid.cellCount();
    const float h = m_grid.h();
    m_grid.classify(m_params.liquidThreshold);
    std::vector<int> liquid; liquid.reserve(n / 4);
    std::vector<int> row(n, -1);
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x)
        if (m_grid.kind(x, y, z) == CellKind::Liquid && m_grid.occ(x, y, z) == Occ::Air) { row[m_grid.idx(x, y, z)] = static_cast<int>(liquid.size()); liquid.push_back(static_cast<int>(m_grid.idx(x, y, z))); }
    m_p.assign(n, 0.0);
    r.pcgIterations = 0; r.pcgResidual = 0.0;
    if (liquid.empty()) return;
    const size_t m = liquid.size();
    // matrix: diag[i], and for each of 6 neighbours either a liquid row index (coefficient −1) or
    // a Dirichlet weight added to the diagonal (air: 1/θ), or nothing (solid)
    struct Nb { int rowIdx; };
    std::vector<double> diag(m, 0.0), rhs(m, 0.0);
    std::vector<std::array<int, 6>> nb(m);
    const int dx[6] = {-1, 1, 0, 0, 0, 0}, dy[6] = {0, 0, -1, 1, 0, 0}, dz[6] = {0, 0, 0, 0, -1, 1};
    const double invh2 = 1.0 / (static_cast<double>(h) * h);
    for (size_t i = 0; i < m; ++i) {
        const int ci = liquid[i];
        const int x = ci % nx, y = (ci / nx) % ny, z = ci / (nx * ny);
        double d = 0.0;
        for (int k = 0; k < 6; ++k) {
            const int xn = x + dx[k], yn = y + dy[k], zn = z + dz[k];
            nb[i][k] = -1;
            if (blocked(m_grid, xn, yn, zn)) continue;                                   // Neumann
            const int ni = static_cast<int>(m_grid.idx(xn, yn, zn));
            if (row[ni] >= 0) { nb[i][k] = row[ni]; d += 1.0; continue; }               // liquid neighbour
            d += 1.0 / thetaToAir(x, y, z, k);                                          // ghost fluid
        }
        diag[i] = d * invh2;
        // rhs = −div(u)/dt
        const double div = (static_cast<double>(m_grid.u(x + 1, y, z)) - m_grid.u(x, y, z) + m_grid.v(x, y + 1, z) - m_grid.v(x, y, z) + m_grid.w(x, y, z + 1) - m_grid.w(x, y, z)) / h;
        rhs[i] = -div / dt;
    }
    // CG with Jacobi preconditioner
    auto applyA = [&](const std::vector<double>& p, std::vector<double>& out) {
        for (size_t i = 0; i < m; ++i) {
            double s = diag[i] * p[i];
            for (int k = 0; k < 6; ++k) if (nb[i][k] >= 0) s -= invh2 * p[nb[i][k]];
            out[i] = s;
        }
    };
    std::vector<double> x(m, 0.0), rres(rhs), zv(m), sdir(m), Ap(m);
    double rhsNorm = 0.0; for (double v : rhs) rhsNorm += v * v; rhsNorm = std::sqrt(rhsNorm);
    if (rhsNorm < 1e-300) { for (size_t i = 0; i < m; ++i) m_p[liquid[i]] = 0.0; return; }
    for (size_t i = 0; i < m; ++i) zv[i] = rres[i] / diag[i];
    sdir = zv;
    double rz = 0.0; for (size_t i = 0; i < m; ++i) rz += rres[i] * zv[i];
    int it = 0; double resNorm = rhsNorm;
    for (; it < m_params.pcgMaxIters; ++it) {
        applyA(sdir, Ap);
        double sAp = 0.0; for (size_t i = 0; i < m; ++i) sAp += sdir[i] * Ap[i];
        if (std::abs(sAp) < 1e-300) break;
        const double alpha = rz / sAp;
        resNorm = 0.0;
        for (size_t i = 0; i < m; ++i) { x[i] += alpha * sdir[i]; rres[i] -= alpha * Ap[i]; resNorm += rres[i] * rres[i]; }
        resNorm = std::sqrt(resNorm);
        if (resNorm <= m_params.pcgTolerance * rhsNorm) { ++it; break; }
        for (size_t i = 0; i < m; ++i) zv[i] = rres[i] / diag[i];
        double rzNew = 0.0; for (size_t i = 0; i < m; ++i) rzNew += rres[i] * zv[i];
        const double beta = rzNew / rz; rz = rzNew;
        for (size_t i = 0; i < m; ++i) sdir[i] = zv[i] + beta * sdir[i];
    }
    r.pcgIterations += it; r.pcgResidual = resNorm / rhsNorm;
    for (size_t i = 0; i < m; ++i) m_p[liquid[i]] = x[i];
    // velocity update on every face touching a liquid cell: u -= dt * (p_right - p_left) / h,
    // with p = 0 on the air side (ghost) and no update on solid faces
    auto pAt = [&](int xx, int yy, int zz) -> double { return (m_grid.inBounds(xx, yy, zz) && row[m_grid.idx(xx, yy, zz)] >= 0) ? m_p[m_grid.idx(xx, yy, zz)] : 0.0; };
    auto isLiq = [&](int xx, int yy, int zz) { return m_grid.inBounds(xx, yy, zz) && row[m_grid.idx(xx, yy, zz)] >= 0; };
    // The face distance is h between two liquid cells and theta*h between a liquid cell and the
    // free surface - the SAME theta the matrix row used, or the projection is inconsistent (the
    // first green attempt halved the surface-face correction: still water kept g*dt of velocity,
    // hydrostatic pressure carried a +4.5 Pa offset, and nothing flowed, 2026-10-08).
    auto faceUpdate = [&](int xa, int ya, int za, int xb, int yb, int zb, int dirAtoB, float& vel) {
        const bool la = isLiq(xa, ya, za), lb = isLiq(xb, yb, zb);
        if (!la && !lb) return;
        const double pa = pAt(xa, ya, za), pb = pAt(xb, yb, zb);
        double dist = h;
        if (la && !lb) dist = h * thetaToAir(xa, ya, za, dirAtoB);
        else if (lb && !la) dist = h * thetaToAir(xb, yb, zb, dirAtoB ^ 1);
        vel -= static_cast<float>(dt * (pb - pa) / dist);
    };
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x <= nx; ++x) {
        if (blocked(m_grid, x - 1, y, z) || blocked(m_grid, x, y, z)) { m_grid.u(x, y, z) = 0.0f; continue; }
        faceUpdate(x - 1, y, z, x, y, z, 1, m_grid.u(x, y, z));
    }
    for (int z = 0; z < nz; ++z) for (int y = 0; y <= ny; ++y) for (int x = 0; x < nx; ++x) {
        if (blocked(m_grid, x, y - 1, z) || blocked(m_grid, x, y, z)) { m_grid.v(x, y, z) = 0.0f; continue; }
        faceUpdate(x, y - 1, z, x, y, z, 3, m_grid.v(x, y, z));
    }
    for (int z = 0; z <= nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        if (blocked(m_grid, x, y, z - 1) || blocked(m_grid, x, y, z)) { m_grid.w(x, y, z) = 0.0f; continue; }
        faceUpdate(x, y, z - 1, x, y, z, 5, m_grid.w(x, y, z));
    }
}

// Fill the velocity field a few cells INTO the air past the free surface, then zero everything
// beyond. Semi-Lagrangian advection back-traces from faces that may lie just ahead of the water;
// if those faces hold no velocity, water arriving there loses its momentum and a thin tongue
// cannot advance (the fine dam break stalled at the 0.5 m contour with a sheer front until this
// existed, 2026-10-08). Standard practice (Bridson ch. 4): extrapolate, here 3 layers by averaging
// the known neighbours in the face lattice, deterministic Jacobi sweeps.
namespace {
void extrapolateLattice(std::vector<float>& a, std::vector<uint8_t>& known, int sx, int sy, int sz, int layers) {
    auto at = [&](int x, int y, int z) { return static_cast<size_t>(x) + static_cast<size_t>(sx) * (static_cast<size_t>(y) + static_cast<size_t>(sy) * z); };
    std::vector<uint8_t> next(known.size());
    std::vector<float> val(a.size());
    const int dx[6] = {-1, 1, 0, 0, 0, 0}, dy[6] = {0, 0, -1, 1, 0, 0}, dz[6] = {0, 0, 0, 0, -1, 1};
    for (int layer = 0; layer < layers; ++layer) {
        next = known; val = a;
        for (int z = 0; z < sz; ++z) for (int y = 0; y < sy; ++y) for (int x = 0; x < sx; ++x) {
            const size_t i = at(x, y, z);
            if (known[i]) continue;
            float sum = 0.0f; int n = 0;
            for (int k = 0; k < 6; ++k) {
                const int xn = x + dx[k], yn = y + dy[k], zn = z + dz[k];
                if (xn < 0 || yn < 0 || zn < 0 || xn >= sx || yn >= sy || zn >= sz) continue;
                const size_t j = at(xn, yn, zn);
                if (known[j]) { sum += a[j]; ++n; }
            }
            if (n > 0) { val[i] = sum / static_cast<float>(n); next[i] = 1; }
        }
        known.swap(next); a.swap(val);
    }
    // `known` now marks everything the band reached; the caller decides what unreached faces keep.
}
} // namespace

void WaterSolver::extrapolateVelocity() {
    // The pressure DOMAIN (cells with f >= liquidThreshold) dictates the velocity in its halo:
    // faces touching a domain cell are the known values, and the band - air faces AND thin-film
    // faces alike - is extrapolated from them. A thin film's faces are not "known" merely because
    // water touches them: left to themselves they kept whatever stale value they had (the fine
    // dam-break tip ran at 0.35 m/s beside a 5 m/s front, front trace 2026-10-08). Thin water the
    // band does not reach (a drop in mid-air) keeps its own ballistic velocity; unreached dry
    // faces are still air.
    const int nx = m_grid.nx(), ny = m_grid.ny(), nz = m_grid.nz();
    const float thr = m_params.liquidThreshold;
    auto wet = [&](int x, int y, int z) { return m_grid.inBounds(x, y, z) && m_grid.f(x, y, z) > 0.0f; };
    auto dom = [&](int x, int y, int z) { return m_grid.inBounds(x, y, z) && m_grid.f(x, y, z) >= thr && m_grid.occ(x, y, z) == Occ::Air; };
    std::vector<uint8_t> ku(m_grid.uData().size(), 0), kv(m_grid.vData().size(), 0), kw(m_grid.wData().size(), 0);
    std::vector<uint8_t> wu(ku.size(), 0), wv(kv.size(), 0), ww(kw.size(), 0);
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x <= nx; ++x) {
        const size_t i = m_grid.uIdx(x, y, z);
        ku[i] = (dom(x - 1, y, z) || dom(x, y, z)) ? 1 : 0;
        wu[i] = (wet(x - 1, y, z) || wet(x, y, z)) ? 1 : 0;
    }
    for (int z = 0; z < nz; ++z) for (int y = 0; y <= ny; ++y) for (int x = 0; x < nx; ++x) {
        const size_t i = m_grid.vIdx(x, y, z);
        kv[i] = (dom(x, y - 1, z) || dom(x, y, z)) ? 1 : 0;
        wv[i] = (wet(x, y - 1, z) || wet(x, y, z)) ? 1 : 0;
    }
    for (int z = 0; z <= nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        const size_t i = m_grid.wIdx(x, y, z);
        kw[i] = (dom(x, y, z - 1) || dom(x, y, z)) ? 1 : 0;
        ww[i] = (wet(x, y, z - 1) || wet(x, y, z)) ? 1 : 0;
    }
    constexpr int kLayers = 3;
    extrapolateLattice(m_grid.uData(), ku, nx + 1, ny, nz, kLayers);
    extrapolateLattice(m_grid.vData(), kv, nx, ny + 1, nz, kLayers);
    extrapolateLattice(m_grid.wData(), kw, nx, ny, nz + 1, kLayers);
    auto finish = [](std::vector<float>& a, const std::vector<uint8_t>& reached, const std::vector<uint8_t>& wetFace) {
        for (size_t i = 0; i < a.size(); ++i) if (!reached[i] && !wetFace[i]) a[i] = 0.0f;
    };
    finish(m_grid.uData(), ku, wu); finish(m_grid.vData(), kv, wv); finish(m_grid.wData(), kw, ww);
    enforceSolidFaces();   // extrapolation must not write into solid faces
}

void WaterSolver::applyRestDamping(float dt) {
    const float k = std::max(0.0f, 1.0f - m_params.restDamping * dt);
    for (float& x : m_grid.uData()) x *= k;
    for (float& x : m_grid.vData()) x *= k;
    for (float& x : m_grid.wData()) x *= k;
}

double WaterSolver::maxSpeed() const {
    float mx = 0.0f;
    for (float x : const_cast<WaterGrid&>(m_grid).uData()) mx = std::max(mx, std::abs(x));
    for (float x : const_cast<WaterGrid&>(m_grid).vData()) mx = std::max(mx, std::abs(x));
    for (float x : const_cast<WaterGrid&>(m_grid).wData()) mx = std::max(mx, std::abs(x));
    return mx;
}

int WaterSolver::substepsFor(float dt) const {
    // CFL: max|v| * dtSub <= cflFraction * h, with the gravity this tick will add included.
    const double vmax = maxSpeed() + m_params.gravity * dt;
    const double limit = m_params.cflFraction * m_grid.h();
    const int n = static_cast<int>(std::ceil(vmax * dt / limit));
    return std::clamp(n, 1, m_params.maxSubsteps);
}

StepReport WaterSolver::step(float dt) {
    StepReport r;
    if (m_asleep) { r.asleep = true; r.totalMass = m_grid.totalMass(); r.quietTicks = m_quietTicks; m_last = r; return r; }
    refreshSolids();
    m_fPrev.assign(m_grid.fData().begin(), m_grid.fData().end());
    const int n = substepsFor(dt);
    const float ds = dt / static_cast<float>(n);
    const bool quietBefore = m_grid.kineticEnergy() / std::max(m_grid.totalMass(), 1e-9) < m_params.keWake;
    for (int s = 0; s < n; ++s) {
        applySources(ds, r);
        applyGravity(ds);
        applyThinFilmGradient(ds);
        settleThinFilmTopFaces();
        enforceSolidFaces();
        m_transport->advect(m_grid, ds);
        enforceSolidFaces();
        project(ds, r);
        extrapolateVelocity();
        settleThinFilmTopFaces();
        if (quietBefore) applyRestDamping(ds);
    }
    r.substeps = n;
    r.totalMass = m_grid.totalMass();
    r.kineticEnergy = m_grid.kineticEnergy();
    double maxD = 0.0;
    const std::vector<float>& f = m_grid.fData();
    for (size_t i = 0; i < f.size(); ++i) maxD = std::max(maxD, static_cast<double>(std::abs(f[i] - m_fPrev[i])));
    r.maxDeltaF = maxD;
    const double specificKE = r.kineticEnergy / std::max(r.totalMass, 1e-9);   // m^2/s^2 per unit mass
    const bool quiet = specificKE < m_params.keWake && maxD < m_params.maxDeltaFQuiet && r.sourceAdded == 0.0 && r.sinkRemoved == 0.0;
    m_quietTicks = quiet ? m_quietTicks + 1 : 0;
    if (m_quietTicks >= m_params.restTicks) { m_asleep = true; }
    r.quietTicks = m_quietTicks; r.asleep = m_asleep;
    m_last = r;
    return r;
}

} // namespace Water
} // namespace Core
} // namespace Phyxel
