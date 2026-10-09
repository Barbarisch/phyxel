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
#include <functional>
#include <limits>

namespace Phyxel {
namespace Core {
namespace Water {

// ── Phase D write-back (docs/WaterCore.md 16.2) ─────────────────────────────────────────────
WriteBackStats columnRunsFromGrid(const WaterGrid& grid, const std::function<bool(int, int)>& held,
                                  std::vector<ColumnRuns>& out) {
    out.clear();
    WriteBackStats st;
    const GridSpec& sp = grid.spec();
    const float h = sp.h;
    const int per = std::max(1, static_cast<int>(std::lround(1.0f / h)));
    const float fMin = WaterGrid::kSurfaceMinDepth / h;
    const double h3 = static_cast<double>(h) * h * h;
    // voxel columns covered by the grid (aligned boxes: exact; otherwise the partial edge columns
    // aggregate the sub-columns that exist, which keeps mass exact per column either way)
    const int vx0 = static_cast<int>(std::floor(static_cast<float>(sp.origin.x) / per));
    const int vx1 = static_cast<int>(std::floor(static_cast<float>(sp.origin.x + sp.dims.x - 1) / per));
    const int vz0 = static_cast<int>(std::floor(static_cast<float>(sp.origin.z) / per));
    const int vz1 = static_cast<int>(std::floor(static_cast<float>(sp.origin.z + sp.dims.z - 1) / per));
    std::vector<float> layer(static_cast<size_t>(grid.ny()));
    for (int vz = vz0; vz <= vz1; ++vz)
        for (int vx = vx0; vx <= vx1; ++vx) {
            ColumnRuns col; col.x = vx; col.z = vz;
            const int gx0 = std::max(0, vx * per - sp.origin.x), gx1 = std::min(grid.nx() - 1, (vx + 1) * per - 1 - sp.origin.x);
            const int gz0 = std::max(0, vz * per - sp.origin.z), gz1 = std::min(grid.nz() - 1, (vz + 1) * per - 1 - sp.origin.z);
            const int nsub = (gx1 - gx0 + 1) * (gz1 - gz0 + 1);
            if (nsub <= 0) continue;
            double colMass = 0.0;
            for (int gy = 0; gy < grid.ny(); ++gy) {
                double sum = 0.0;
                for (int gz = gz0; gz <= gz1; ++gz) for (int gx = gx0; gx <= gx1; ++gx) sum += grid.f(gx, gy, gz);
                layer[static_cast<size_t>(gy)] = static_cast<float>(sum / nsub);
                colMass += sum * h3;
            }
            if (held && held(vx, vz)) {
                col.held = true; col.heldMass = colMass;
                ++st.heldColumns; st.heldMass += colMass;
                out.push_back(std::move(col));
                continue;
            }
            // sub-column surface spread (geometric, per sub-column)
            float sMin = 1e30f, sMax = -1e30f;
            for (int gz = gz0; gz <= gz1; ++gz) for (int gx = gx0; gx <= gx1; ++gx) {
                const float sy = grid.surfaceWorldY(gx, gz);
                if (std::isnan(sy)) continue;
                sMin = std::min(sMin, sy); sMax = std::max(sMax, sy);
            }
            col.spreadMm = (sMax >= sMin) ? (sMax - sMin) * 1000.0f : 0.0f;
            // runs
            int gy = 0;
            while (gy < grid.ny()) {
                if (layer[static_cast<size_t>(gy)] < fMin) { ++gy; continue; }
                const int start = gy;
                double depth = 0.0;   // m, = Σ f̄·h
                int top = gy;
                while (gy < grid.ny() && layer[static_cast<size_t>(gy)] >= fMin) { depth += static_cast<double>(layer[static_cast<size_t>(gy)]) * h; top = gy; ++gy; }
                ColumnRun run;
                run.bottomY = static_cast<float>(sp.origin.y + start) * h;
                run.topY = static_cast<float>(run.bottomY + depth);
                run.mass = depth * (static_cast<double>(nsub) * h * h);   // × the column area actually covered (1 m² when aligned)
                const float geom = (static_cast<float>(sp.origin.y + top) + std::min(layer[static_cast<size_t>(top)], 1.0f)) * h;
                col.surfaceVsMassMm = std::max(col.surfaceVsMassMm, std::abs(geom - run.topY) * 1000.0f);
                col.runs.push_back(run);
            }
            // fold whatever the 1 mm surface rule left out into the top run: the stored column is
            // the cells' mass exactly (a forced snapshot of a moving pond lost 2.9e-3 m^3 of thin
            // layers to this rule, R3 Small 2026-10-09)
            {
                double inRuns = 0.0;
                for (const auto& r : col.runs) inRuns += r.mass;
                const double leftover = colMass - inRuns;
                if (leftover > 0.0) {
                    if (!col.runs.empty()) { col.runs.back().topY += static_cast<float>(leftover / (static_cast<double>(nsub) * h * h)); col.runs.back().mass += leftover; }
                    else st.thinDropped += leftover;
                }
            }
            st.columns += 1; st.runs += static_cast<long>(col.runs.size());
            for (const auto& r : col.runs) st.mass += r.mass;
            st.surfaceVsMassMmMax = std::max(st.surfaceVsMassMmMax, col.surfaceVsMassMm);
            st.spreadMmMax = std::max(st.spreadMmMax, col.spreadMm);
            out.push_back(std::move(col));
        }
    return st;
}

double seedGridFromRuns(WaterGrid& grid, const std::vector<ColumnRuns>& runs) {
    const GridSpec& sp = grid.spec();
    const float h = sp.h;
    const int per = std::max(1, static_cast<int>(std::lround(1.0f / h)));
    std::fill(grid.fData().begin(), grid.fData().end(), 0.0f);
    std::fill(grid.uData().begin(), grid.uData().end(), 0.0f);
    std::fill(grid.vData().begin(), grid.vData().end(), 0.0f);
    std::fill(grid.wData().begin(), grid.wData().end(), 0.0f);
    double placed = 0.0;
    const double h3 = static_cast<double>(h) * h * h;
    for (const auto& col : runs) {
        if (col.held) continue;
        const int gx0 = std::max(0, col.x * per - sp.origin.x), gx1 = std::min(grid.nx() - 1, (col.x + 1) * per - 1 - sp.origin.x);
        const int gz0 = std::max(0, col.z * per - sp.origin.z), gz1 = std::min(grid.nz() - 1, (col.z + 1) * per - 1 - sp.origin.z);
        if (gx1 < gx0 || gz1 < gz0) continue;
        for (const auto& r : col.runs) {
            for (int gy = 0; gy < grid.ny(); ++gy) {
                const double yb = static_cast<double>(sp.origin.y + gy) * h, yt = yb + h;
                const double ov = std::min(static_cast<double>(r.topY), yt) - std::max(static_cast<double>(r.bottomY), yb);
                if (ov <= 0.0) continue;
                const float f = static_cast<float>(std::min(1.0, ov / h));
                for (int gz = gz0; gz <= gz1; ++gz) for (int gx = gx0; gx <= gx1; ++gx) { grid.f(gx, gy, gz) += f; placed += f * h3; }
            }
        }
    }
    for (float& f : grid.fData()) f = std::min(f, 1.0f);
    grid.classify();
    return placed;
}

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
    // The surface is the top of the highest cell holding at least a millimetre of depth: a
    // trickle in transit (5e-6 of a cell crossing a step edge) is not a surface, and reporting it
    // as one put a 0.85 m "spread" on a pool that was flat to 15 cm (S3 Basin, 2026-10-08). S11
    // writes spans to the surface +- 1 mm, so 1 mm is the resolution the surface is defined at.
    const float fMin = kSurfaceMinDepth / m_spec.h;
    for (int y = ny() - 1; y >= 0; --y) {
        const float fv = m_f[idx(x, y, z)];
        if (fv > fMin) return (static_cast<float>(m_spec.origin.y + y) + std::min(fv, 1.0f)) * m_spec.h;
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
    // A thin cell's water lies on its FLOOR (gravity), so a downward face sweeping |v| dt of the
    // cell height takes the whole film once that exceeds the film depth - min(f, |v| dt / h) -
    // not the continuum fraction f * |v| dt / h, which halves a falling drop per substep forever
    // (CFL caps the fraction at 1/2) while its face keeps integrating gravity to -5.7 m/s and
    // poisons the semi-Lagrangian back-traces around it (dam-break front 18 -> 15 cells once
    // drops were allowed to fall at all, 2026-10-08). Lateral films keep the continuum flux
    // (depth x velocity). `thin` is evaluated on the ORIGINAL fills, like the donor fraction.
    const float thr = m_liquidThreshold;
    auto moveFace = [&](size_t ia, size_t ib, float vel, float fDonorOrig, bool downward = false) {
        if (vel == 0.0f || fDonorOrig <= 0.0f) return;
        const size_t from = vel > 0.0f ? ia : ib, to = vel > 0.0f ? ib : ia;
        const float sweep = std::abs(vel) * dt / h;
        const float desired = (downward && fDonorOrig < thr) ? std::min(fDonorOrig, sweep) : sweep * fDonorOrig;
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
            moveFace(g.idx(x, y - 1, z), g.idx(x, y, z), vel, vel > 0.0f ? f[g.idx(x, y - 1, z)] : f[g.idx(x, y, z)], vel < 0.0f);
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

// ─────────────────────────────────────────────────────────────────── FlipTransport ───────────
// Phase B2 red stub (2026-10-08): seeding, settling and accounting are real; advect does NOTHING
// yet, so the dam-break front, the run-up and the pump tests fail against it by measurement.

namespace {
inline uint32_t flipHash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
inline float flipJitter(uint32_t a, uint32_t b, uint32_t c) { return (flipHash(a, b, c) & 0xFFFFu) / 65536.0f; }
} // namespace

void FlipTransport::sortParticles(const WaterGrid& g) {
    std::sort(m_particles.begin(), m_particles.end(), [&](const FlipParticle& p, const FlipParticle& q) {
        const glm::ivec3 cp = glm::ivec3(glm::floor(p.pos)), cq = glm::ivec3(glm::floor(q.pos));
        const size_t ip = g.inBounds(cp.x, cp.y, cp.z) ? g.idx(cp.x, cp.y, cp.z) : ~size_t(0);
        const size_t iq = g.inBounds(cq.x, cq.y, cq.z) ? g.idx(cq.x, cq.y, cq.z) : ~size_t(0);
        return ip != iq ? ip < iq : p.id < q.id;
    });
}

void FlipTransport::seed(WaterGrid& g) {
    m_particles.clear();
    const float V = g.cellVolume();
    const float mp = V / static_cast<float>(kParticlesPerCell);
    for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x) {
        const float fc = g.f(x, y, z);
        if (fc <= 0.0f || g.occ(x, y, z) != Occ::Air) continue;
        const double cellMass = static_cast<double>(fc) * V;
        int n = static_cast<int>(std::floor(cellMass / mp + 1e-6));
        n = std::min(n, kParticlesPerCell);
        const double remainder = cellMass - static_cast<double>(n) * mp;
        const glm::ivec3 wc = g.spec().origin + glm::ivec3(x, y, z);
        const uint32_t cellIdx = flipHash(static_cast<uint32_t>(wc.x), static_cast<uint32_t>(wc.y), static_cast<uint32_t>(wc.z));   // WORLD cell: the sub-box and the whole box seed identically
        auto place = [&](int i, float mass) {
            // 2x2x2 jittered sites inside the WET part of the cell (water lies on the cell floor)
            const int sx = i & 1, sy = (i >> 1) & 1, sz = (i >> 2) & 1;
            FlipParticle p;
            p.pos = glm::vec3(x + 0.5f * sx + 0.5f * flipJitter(cellIdx, i, 1),
                              y + fc * (0.5f * sy + 0.5f * flipJitter(cellIdx, i, 2)),
                              z + 0.5f * sz + 0.5f * flipJitter(cellIdx, i, 3));
            p.vel = glm::vec3(0.5f * (g.u(x, y, z) + g.u(x + 1, y, z)), 0.5f * (g.v(x, y, z) + g.v(x, y + 1, z)), 0.5f * (g.w(x, y, z) + g.w(x, y, z + 1)));
            p.mass = mass; p.id = m_nextId++;
            m_particles.push_back(p);
        };
        for (int i = 0; i < n; ++i) place(i, mp);
        if (remainder > 1e-9 * V) place(n, static_cast<float>(remainder));
    }
    sortParticles(g);
    m_haveOldGrid = false;
    particlesToGrid(g);
}

void FlipTransport::settle(WaterGrid& g) {
    std::fill(g.fData().begin(), g.fData().end(), 0.0f);
    const float V = g.cellVolume();
    for (const FlipParticle& p : m_particles) {
        glm::ivec3 c = glm::ivec3(glm::floor(p.pos));
        c = glm::clamp(c, glm::ivec3(0), glm::ivec3(g.nx() - 1, g.ny() - 1, g.nz() - 1));
        g.f(c.x, c.y, c.z) += p.mass / V;
    }
    m_particles.clear();
    m_haveOldGrid = false;
}

double FlipTransport::ownedMass() const {
    double m = 0.0;
    for (const FlipParticle& p : m_particles) m += p.mass;
    return m;
}

double FlipTransport::addVolume(WaterGrid& g, const glm::ivec3& cell, double m3, const glm::vec3& vel) {
    if (!g.inBounds(cell.x, cell.y, cell.z) || g.occ(cell.x, cell.y, cell.z) != Occ::Air || m3 <= 0.0) return 0.0;
    const float mp = g.cellVolume() / static_cast<float>(kParticlesPerCell);
    double placed = 0.0;
    const glm::ivec3 wc = g.spec().origin + cell;
    const uint32_t cellIdx = flipHash(static_cast<uint32_t>(wc.x), static_cast<uint32_t>(wc.y), static_cast<uint32_t>(wc.z));
    while (m3 - placed >= mp && m_particles.size() < kMaxParticlesPerVolume) {
        FlipParticle p;
        const uint32_t k = m_nextId;
        p.pos = glm::vec3(cell.x + 0.1f + 0.8f * flipJitter(cellIdx, k, 4), cell.y + 0.1f + 0.8f * flipJitter(cellIdx, k, 5), cell.z + 0.1f + 0.8f * flipJitter(cellIdx, k, 6));
        p.vel = vel; p.mass = mp; p.id = m_nextId++;
        m_particles.push_back(p);
        placed += mp;
    }
    return placed;   // whole particles only; the remainder is owed (SourceSpec::pending)
}

double FlipTransport::removeVolume(WaterGrid& g, const glm::ivec3& cell, double m3) {
    if (!g.inBounds(cell.x, cell.y, cell.z) || m3 <= 0.0) return 0.0;
    double removed = 0.0;
    // lowest creation index in the cell first (the list is sorted by cell, then id)
    for (size_t i = 0; i < m_particles.size() && removed < m3;) {
        const glm::ivec3 c = glm::ivec3(glm::floor(m_particles[i].pos));
        if (c == cell) { removed += m_particles[i].mass; m_particles.erase(m_particles.begin() + static_cast<std::ptrdiff_t>(i)); }
        else ++i;
    }
    return removed;
}

namespace {
// Trilinear scatter of (weight, weight*value) onto a lattice of size (sx, sy, sz) at lattice
// position q; nodes outside the lattice are skipped (their share is lost to the wall, as a
// particle pressed against a boundary has no face beyond it).
inline void scatterLattice(std::vector<float>& acc, std::vector<float>& wacc, int sx, int sy, int sz, const glm::vec3& q, float w, float value) {
    const int x0 = static_cast<int>(std::floor(q.x)), y0 = static_cast<int>(std::floor(q.y)), z0 = static_cast<int>(std::floor(q.z));
    const float tx = q.x - x0, ty = q.y - y0, tz = q.z - z0;
    for (int dz = 0; dz < 2; ++dz) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        const int x = x0 + dx, y = y0 + dy, z = z0 + dz;
        if (x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz) continue;
        const float wt = w * (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty) * (dz ? tz : 1 - tz);
        if (wt <= 0.0f) continue;
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(sx) * (static_cast<size_t>(y) + static_cast<size_t>(sy) * z);
        acc[i] += wt * value; wacc[i] += wt;
    }
}
// Face-sampled velocity (m/s) of three lattices at a cell-space position.
inline glm::vec3 sampleFaces(const std::vector<float>& u, const std::vector<float>& v, const std::vector<float>& w, int nx, int ny, int nz, const glm::vec3& p) {
    return glm::vec3(sampleLattice(u, nx + 1, ny, nz, glm::vec3(p.x, p.y - 0.5f, p.z - 0.5f)),
                     sampleLattice(v, nx, ny + 1, nz, glm::vec3(p.x - 0.5f, p.y, p.z - 0.5f)),
                     sampleLattice(w, nx, ny, nz + 1, glm::vec3(p.x - 0.5f, p.y - 0.5f, p.z)));
}
} // namespace

void FlipTransport::particlesToGrid(WaterGrid& g) {
    const int nx = g.nx(), ny = g.ny(), nz = g.nz();
    std::vector<float>& f = g.fData();
    std::fill(f.begin(), f.end(), 0.0f);
    const float V = g.cellVolume();
    std::vector<float>& u = g.uData(); std::vector<float>& v = g.vData(); std::vector<float>& w = g.wData();
    std::fill(u.begin(), u.end(), 0.0f); std::fill(v.begin(), v.end(), 0.0f); std::fill(w.begin(), w.end(), 0.0f);
    m_uW.assign(u.size(), 0.0f); m_vW.assign(v.size(), 0.0f); m_wW.assign(w.size(), 0.0f);
    for (const FlipParticle& p : m_particles) {   // sorted order: deterministic accumulation
        const glm::ivec3 c = glm::ivec3(glm::floor(p.pos));
        if (g.inBounds(c.x, c.y, c.z)) f[g.idx(c.x, c.y, c.z)] += p.mass / V;
        scatterLattice(u, m_uW, nx + 1, ny, nz, glm::vec3(p.pos.x, p.pos.y - 0.5f, p.pos.z - 0.5f), p.mass, p.vel.x);
        scatterLattice(v, m_vW, nx, ny + 1, nz, glm::vec3(p.pos.x - 0.5f, p.pos.y, p.pos.z - 0.5f), p.mass, p.vel.y);
        scatterLattice(w, m_wW, nx, ny, nz + 1, glm::vec3(p.pos.x - 0.5f, p.pos.y - 0.5f, p.pos.z), p.mass, p.vel.z);
    }
    for (size_t i = 0; i < u.size(); ++i) u[i] = m_uW[i] > 0.0f ? u[i] / m_uW[i] : 0.0f;
    for (size_t i = 0; i < v.size(); ++i) v[i] = m_vW[i] > 0.0f ? v[i] / m_vW[i] : 0.0f;
    for (size_t i = 0; i < w.size(); ++i) w[i] = m_wW[i] > 0.0f ? w[i] / m_wW[i] : 0.0f;
    m_uOld = u; m_vOld = v; m_wOld = w;   // the FLIP delta base for the next substep
    m_haveOldGrid = true;
}

void FlipTransport::advect(WaterGrid& g, float dt) {
    const int nx = g.nx(), ny = g.ny(), nz = g.nz();
    const float h = g.h();
    const float eps = 1e-4f;
    auto blockedCell = [&](int x, int y, int z) { return !g.inBounds(x, y, z) || g.occ(x, y, z) != Occ::Air; };
    // 1. grid -> particle: the projected grid (left by the last projection, extrapolated into its
    //    halo) vs the p2g base of the last substep. FLIP keeps the particle's own velocity plus the
    //    grid's CHANGE; the PIC share damps the noise FLIP is known for.
    if (m_haveOldGrid) {
        for (FlipParticle& p : m_particles) {
            const glm::vec3 vNew = sampleFaces(g.uData(), g.vData(), g.wData(), nx, ny, nz, p.pos);
            const glm::vec3 vOld = sampleFaces(m_uOld, m_vOld, m_wOld, nx, ny, nz, p.pos);
            p.vel = m_flipBlend * (p.vel + (vNew - vOld)) + (1.0f - m_flipBlend) * vNew;
        }
    }
    // 2. move: RK2 in the grid field (cells/s), then axis-split placement against solids and the
    //    hold boundary - a blocked axis loses its velocity component, the others keep theirs.
    for (FlipParticle& p : m_particles) {
        const glm::vec3 k1 = sampleFaces(g.uData(), g.vData(), g.wData(), nx, ny, nz, p.pos) / h;
        const glm::vec3 mid = p.pos + 0.5f * dt * k1;
        glm::vec3 k2 = sampleFaces(g.uData(), g.vData(), g.wData(), nx, ny, nz, mid) / h;
        // a particle the grid does not reach (no wet face around it) moves by its own velocity
        if (glm::dot(k2, k2) == 0.0f) k2 = p.vel / h;
        glm::vec3 target = p.pos + dt * k2;
        glm::vec3 pos = p.pos;
        for (int axis = 0; axis < 3; ++axis) {
            glm::vec3 trial = pos; trial[axis] = target[axis];
            const glm::ivec3 c = glm::ivec3(glm::floor(trial));
            if (!blockedCell(c.x, c.y, c.z)) { pos = trial; continue; }
            // stop at the face of the blocked cell, drop the component into it
            const int here = static_cast<int>(std::floor(pos[axis]));
            pos[axis] = target[axis] > pos[axis] ? static_cast<float>(here + 1) - eps : static_cast<float>(here) + eps;
            p.vel[axis] = 0.0f;
        }
        // the particle's current cell may itself have become solid (a block placed on water): lift it
        const glm::ivec3 cc = glm::ivec3(glm::floor(pos));
        if (blockedCell(cc.x, cc.y, cc.z)) {
            for (int up = 1; up < ny; ++up) { if (!blockedCell(cc.x, cc.y + up, cc.z)) { pos.y = static_cast<float>(cc.y + up) + 0.5f; break; } }
            p.vel = glm::vec3(0.0f);
        }
        p.pos = glm::clamp(pos, glm::vec3(eps), glm::vec3(nx - eps, ny - eps, nz - eps));
    }
    // 3. particle -> grid in sorted order (the mass field and the velocity the solver projects)
    sortParticles(g);
    particlesToGrid(g);
}

// ─────────────────────────────────────────────────────────────────── WaterSolver ─────────────
WaterSolver::WaterSolver(WaterGrid& grid, SolidQuery solids, SolverParams params)
    : m_grid(grid), m_solids(std::move(solids)), m_params(params),
      m_transport(std::make_unique<EulerianTransport>()) {
    m_transport->setLiquidThreshold(m_params.liquidThreshold);
    refreshSolids();
}

void WaterSolver::setTransport(std::unique_ptr<IWaterTransport> t) { if (t) { m_transport = std::move(t); m_transport->setLiquidThreshold(m_params.liquidThreshold); } }

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

WaterSolver::RadialKick WaterSolver::addRadialImpulse(const glm::vec3& centre, float reach, float speedAtCentre, float upBias, float maxSpeed) {
    RadialKick k;
    if (!(reach > 0.0f) || speedAtCentre == 0.0f) return k;
    const float h = m_grid.h();
    const glm::vec3 o(m_grid.spec().origin);
    auto kick = [&](const glm::vec3& p, int axis) -> float {
        const glm::vec3 d = p - centre;
        const float r = glm::length(d);
        if (r >= reach) return 0.0f;
        const glm::vec3 radial = r > 1e-6f ? d / r : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 dir = glm::normalize(glm::mix(radial, glm::vec3(0.0f, 1.0f, 0.0f), upBias));
        float s = speedAtCentre * (1.0f - r / reach);
        if (std::abs(s) > maxSpeed) { s = std::copysign(maxSpeed, s); ++k.clamped; }
        ++k.faces;
        return s * dir[axis];
    };
    // only faces that border WATER: a shock travels through the liquid, and air faces carry no mass -
    // kicking them (the up-bias lifts every air face above the surface) only feeds the extrapolation,
    // which the CPU and GPU rebuild differently (measured: |dv| 0.94 m/s apart after 20 ticks)
    const float wet = WaterGrid::kSurfaceMinDepth / h;
    auto liquid = [&](int x, int y, int z) { return m_grid.inBounds(x, y, z) && m_grid.f(x, y, z) >= wet; };
    for (int z = 0; z < m_grid.nz(); ++z) for (int y = 0; y < m_grid.ny(); ++y) for (int x = 0; x <= m_grid.nx(); ++x)
        if (liquid(x - 1, y, z) || liquid(x, y, z)) m_grid.u(x, y, z) += kick((o + glm::vec3(x, y + 0.5f, z + 0.5f)) * h, 0);
    for (int z = 0; z < m_grid.nz(); ++z) for (int y = 0; y <= m_grid.ny(); ++y) for (int x = 0; x < m_grid.nx(); ++x)
        if (liquid(x, y - 1, z) || liquid(x, y, z)) m_grid.v(x, y, z) += kick((o + glm::vec3(x + 0.5f, y, z + 0.5f)) * h, 1);
    for (int z = 0; z <= m_grid.nz(); ++z) for (int y = 0; y < m_grid.ny(); ++y) for (int x = 0; x < m_grid.nx(); ++x)
        if (liquid(x, y, z - 1) || liquid(x, y, z)) m_grid.w(x, y, z) += kick((o + glm::vec3(x + 0.5f, y + 0.5f, z)) * h, 2);
    if (k.faces > 0) wake();
    return k;
}

WaterSolver::RadialKick WaterSolver::addMomentum(const glm::vec3& pos, float radius, const glm::vec3& momentum, float maxSpeed) {
    RadialKick k;
    const float h = m_grid.h();
    const glm::vec3 o(m_grid.spec().origin);
    const float r = std::max(radius, 1.5f * h);   // at least the cells around the body
    const float wet = WaterGrid::kSurfaceMinDepth / h;
    const glm::ivec3 lo = glm::max(glm::ivec3(glm::floor(pos / h - o - r / h)) - 1, glm::ivec3(0));
    const glm::ivec3 hi = glm::min(glm::ivec3(glm::floor(pos / h - o + r / h)) + 1, glm::ivec3(m_grid.nx() - 1, m_grid.ny() - 1, m_grid.nz() - 1));
    double vol = 0.0;
    std::vector<glm::ivec3> cells;
    for (int z = lo.z; z <= hi.z; ++z) for (int y = lo.y; y <= hi.y; ++y) for (int x = lo.x; x <= hi.x; ++x) {
        const glm::vec3 c = (o + glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f)) * h;
        if (glm::dot(c - pos, c - pos) > r * r || m_grid.f(x, y, z) < wet || m_grid.occ(x, y, z) == Occ::Solid) continue;
        vol += static_cast<double>(std::min(m_grid.f(x, y, z), 1.0f)) * h * h * h;
        cells.push_back({x, y, z});
    }
    if (vol <= 0.0 || cells.empty()) return k;
    // the face set: every face of every chosen cell (once). The water's momentum (density 1) is the
    // cell-centred sum  sum_cells f h^3 * (mean of the cell's two faces per axis)  =  per axis,
    // sum_faces du * h^3 * (f_left + f_right) / 2  - a face shared with an UNCHOSEN water cell moves that
    // cell too, so each axis is normalised by its own face weight W and du = momentum / W is exact
    // (measured: normalising by the chosen cells' volume gave 1.17-1.5x the momentum).
    std::vector<char> uSet(m_grid.uData().size(), 0), vSet(m_grid.vData().size(), 0), wSet(m_grid.wData().size(), 0);
    std::vector<size_t> uF, vF, wF;
    auto fAt = [&](int x, int y, int z) -> double { return m_grid.inBounds(x, y, z) ? std::min(m_grid.f(x, y, z), 1.0f) : 0.0f; };
    double Wu = 0.0, Wv = 0.0, Ww = 0.0;
    const double h3 = static_cast<double>(h) * h * h;
    auto takeU = [&](int x, int y, int z) { const size_t i = m_grid.uIdx(x, y, z); if (uSet[i]) return; uSet[i] = 1; uF.push_back(i); Wu += 0.5 * (fAt(x - 1, y, z) + fAt(x, y, z)) * h3; };
    auto takeV = [&](int x, int y, int z) { const size_t i = m_grid.vIdx(x, y, z); if (vSet[i]) return; vSet[i] = 1; vF.push_back(i); Wv += 0.5 * (fAt(x, y - 1, z) + fAt(x, y, z)) * h3; };
    auto takeW = [&](int x, int y, int z) { const size_t i = m_grid.wIdx(x, y, z); if (wSet[i]) return; wSet[i] = 1; wF.push_back(i); Ww += 0.5 * (fAt(x, y, z - 1) + fAt(x, y, z)) * h3; };
    for (const auto& c : cells) {
        takeU(c.x, c.y, c.z); takeU(c.x + 1, c.y, c.z);
        takeV(c.x, c.y, c.z); takeV(c.x, c.y + 1, c.z);
        takeW(c.x, c.y, c.z); takeW(c.x, c.y, c.z + 1);
    }
    glm::vec3 du(Wu > 0.0 ? static_cast<float>(momentum.x / Wu) : 0.0f, Wv > 0.0 ? static_cast<float>(momentum.y / Wv) : 0.0f, Ww > 0.0 ? static_cast<float>(momentum.z / Ww) : 0.0f);
    const float sp = glm::length(du);
    if (sp > maxSpeed) { du *= maxSpeed / sp; ++k.clamped; }
    for (size_t i : uF) m_grid.uData()[i] += du.x;
    for (size_t i : vF) m_grid.vData()[i] += du.y;
    for (size_t i : wF) m_grid.wData()[i] += du.z;
    k.faces = static_cast<long>(uF.size() + vF.size() + wF.size());
    wake();
    return k;
}

double WaterSolver::pressure(int x, int y, int z) const {
    if (!m_grid.inBounds(x, y, z) || m_p.empty()) return 0.0;
    return m_p[m_grid.idx(x, y, z)];
}

BoundarySpec& WaterSolver::setBoundary(const glm::ivec2& columnLocal, float targetY) {
    for (BoundarySpec& b : m_boundaries) if (b.column == columnLocal) { b.targetY = targetY; return b; }
    BoundarySpec b; b.column = columnLocal; b.targetY = targetY;
    m_boundaries.push_back(b);
    return m_boundaries.back();
}

double WaterSolver::applyBoundaries() {
    double total = 0.0;
    const float h = m_grid.h();
    const double h3 = m_grid.cellVolume();
    for (BoundarySpec& b : m_boundaries) {
        const int x = b.column.x, z = b.column.y;
        if (x < 0 || z < 0 || x >= m_grid.nx() || z >= m_grid.nz()) continue;
        const float yTarget = b.targetY / h - static_cast<float>(m_grid.spec().origin.y);   // in cell rows above the grid floor
        double d = 0.0;
        int bed = 0;   // the first air cell above the highest solid under the surface
        for (int y = 0; y < m_grid.ny(); ++y) if (m_grid.occ(x, y, z) != Occ::Air && static_cast<float>(y) < yTarget) bed = y + 1;
        const float depth = std::max(yTarget - static_cast<float>(bed), 0.5f) * h;   // m
        for (int y = 0; y < m_grid.ny(); ++y) {
            if (m_grid.occ(x, y, z) != Occ::Air) continue;   // the seabed stays the seabed
            const float want = std::clamp(yTarget - static_cast<float>(y), 0.0f, 1.0f);
            float& fc = m_grid.f(x, y, z);
            d += static_cast<double>(want - fc) * h3;
            fc = want;
            // Airy orbital velocity profile over the wet cells (the wavemaker): horizontal decays as
            // cosh, vertical as sinh, both 1 at the surface; uniform when k = 0
            if (want > 0.0f) {
                const float zc = (static_cast<float>(y) + 0.5f - static_cast<float>(bed)) * h;   // height above the bed, m
                float cu = 1.0f, cw = 1.0f;
                if (b.k > 0.0f) { const float kd = b.k * depth; cu = std::cosh(b.k * zc) / std::cosh(kd); cw = std::sinh(b.k * zc) / std::max(std::sinh(kd), 1e-6f); }
                const float ux = b.uSurface.x * cu, uz = b.uSurface.y * cu, wy = b.wSurface * cw;
                if (!blocked(m_grid, x - 1, y, z)) m_grid.u(x, y, z) = ux;
                if (!blocked(m_grid, x + 1, y, z)) m_grid.u(x + 1, y, z) = ux;
                if (!blocked(m_grid, x, y, z - 1)) m_grid.w(x, y, z) = uz;
                if (!blocked(m_grid, x, y, z + 1)) m_grid.w(x, y, z + 1) = uz;
                m_grid.v(x, y, z) = wy;
                if (y + 1 < m_grid.ny() && !blocked(m_grid, x, y + 1, z)) m_grid.v(x, y + 1, z) = wy;
            }
        }
        b.exchanged += d; total += d;
    }
    return total;
}

void WaterSolver::applySources(float dt, StepReport& r) {
    const float vol = m_grid.cellVolume();
    for (SourceSpec& s : m_sources) {
        if (!m_grid.inBounds(s.cell.x, s.cell.y, s.cell.z) || m_grid.occ(s.cell.x, s.cell.y, s.cell.z) != Occ::Air) continue;
        float& fc = m_grid.f(s.cell.x, s.cell.y, s.cell.z);
        if (m_transport->ownsMass()) {   // Phase B2: the particle transport takes or gives the volume itself
            const double owedT = std::min(s.pending, static_cast<double>(std::abs(s.rate)));
            const double wantT = static_cast<double>(s.rate) * dt + (s.rate >= 0.0f ? owedT : -owedT);
            if (wantT >= 0.0) {
                const glm::vec3 outletVel(0.5f * (m_grid.u(s.cell.x, s.cell.y, s.cell.z) + m_grid.u(s.cell.x + 1, s.cell.y, s.cell.z)),
                                          0.5f * (m_grid.v(s.cell.x, s.cell.y, s.cell.z) + m_grid.v(s.cell.x, s.cell.y + 1, s.cell.z)),
                                          0.5f * (m_grid.w(s.cell.x, s.cell.y, s.cell.z) + m_grid.w(s.cell.x, s.cell.y, s.cell.z + 1)));
                const double placed = m_transport->addVolume(m_grid, s.cell, wantT, outletVel);
                s.placedTotal += placed; s.unplaced = wantT - placed; s.pending = wantT - placed;
                r.sourceAdded += placed; r.sourceUnplaced += wantT - placed;
            } else {
                const double take = m_transport->removeVolume(m_grid, s.cell, -wantT);
                s.placedTotal -= take; s.pending = -wantT - take;
                r.sinkRemoved += take;
            }
            continue;
        }
        // A pump at a fixed rate owes what the outlet could not take: carry it (bounded to one
        // second of rate, so a long blockage releases as a short surge, not a flood) - without
        // this the S2 pump delivered 93 % of 0.1 m^3/s and the ledger could not close on the rate.
        const double owed = std::min(s.pending, static_cast<double>(std::abs(s.rate))) / vol;
        const double want = static_cast<double>(s.rate) * dt / vol + (s.rate >= 0.0f ? owed : -owed);   // fill units this substep
        if (want >= 0.0) {
            // The outlet cell first; what it cannot hold spills into its non-solid neighbours
            // (a submerged hose pushes water out around itself). A pump whose cell was full
            // simply stopped at 2/3 of the trough (S2 Small, 2026-10-08); the projection's
            // source term (project()) is what carries the inflow away between substeps.
            double left = want;
            auto pour = [&](int x, int y, int z) {
                if (left <= 0.0 || !m_grid.inBounds(x, y, z) || m_grid.occ(x, y, z) != Occ::Air) return;
                float& f = m_grid.f(x, y, z);
                const double room = std::max(0.0, 1.0 - static_cast<double>(f));
                const double placed = std::min(left, room);
                f = static_cast<float>(f + placed);
                left -= placed;
            };
            pour(s.cell.x, s.cell.y, s.cell.z);
            pour(s.cell.x, s.cell.y + 1, s.cell.z);
            pour(s.cell.x - 1, s.cell.y, s.cell.z); pour(s.cell.x + 1, s.cell.y, s.cell.z);
            pour(s.cell.x, s.cell.y, s.cell.z - 1); pour(s.cell.x, s.cell.y, s.cell.z + 1);
            pour(s.cell.x, s.cell.y - 1, s.cell.z);
            const double placed = want - left;
            (void)fc;
            s.placedTotal += placed * vol; s.unplaced = left * vol; s.pending = left * vol;
            r.sourceAdded += placed * vol; r.sourceUnplaced += left * vol;
        } else {
            const double take = std::min(-want, static_cast<double>(fc));
            fc = static_cast<float>(fc - take);
            s.placedTotal -= take * vol; s.pending = (-want - take) * vol;
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
                // wet above (a drop or a residue cell sitting on this film): that face is the pair's
                // own dynamics - gravity drains the upper cell into this one. Copying the floor
                // velocity into it froze whole stacks of thin cells above a resting pool (the S3
                // Basin run at h = 1 ended with a 1.03 m surface spread, films at the rim, 2026-10-08).
                if (y + 1 < m_grid.ny() && m_grid.f(x, y + 1, z) > 0.0f) continue;
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
        // Only the depth above the hold depth is mobile: without a pinning depth a 0.02 m^3 pour
        // spread to a 1.5 mm sheet over 8 m^2 and never came to rest (S1 Small, h = 1/3,
        // 2026-10-08). Real puddles stop at a contact-angle-set thickness; 1 cm is the design's
        // "film >= 0.01 m holds" (docs/WaterCore.md S1).
        const float hold = m_params.filmHoldDepth / m_grid.h();
        const float ma = std::max(0.0f, fa - hold), mb = std::max(0.0f, fb - hold);
        if (ma <= 0.0f && mb <= 0.0f) { vel = 0.0f; return; }   // pinned on both sides: no slope flow
        vel -= g * dt * (mb - ma);                   // d(depth)/dx with depth = f*h over dx = h
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
        // The surface sits (f - 0.5) above this cell's centre, plus whatever thin layer lies in the
        // cell above: ONE continuous expression. With a fixed 0.5 every partially filled cell of a
        // thinning tongue reported the same surface height, the solver saw a flat tongue with no
        // horizontal gradient, and the fine dam break stopped dead at the 0.5 contour (profile,
        // 2026-10-08). Then "0.5 + f_above whenever f_above > 0" assumed THIS cell full: a 1e-9
        // residue above a 0.6 cell moved its surface estimate by 0.4 cells, neighbouring columns
        // disagreed, and a flat pool with a 0.5-0.6 top layer never slept (defect #28, Phase D
        // write-back round trip, 2026-10-08). Clamped away from zero: theta -> 0 is the correct
        // limit (p -> 0 at the centre) but 1/theta must stay finite.
        const double fc = static_cast<double>(m_grid.f(x, y, z));
        return std::clamp(fc - 0.5 + fa, m_params.thetaMin, 1.5);
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
        // A pump outlet is an inflow boundary: the projection enforces div(u) = q there, q = rate /
        // cell volume, so the velocity field carries the added water away each substep instead of
        // the outlet cell saturating (S2 Small: the trough stopped at 2/3, 2026-10-08).
        // The owed backlog raises the demanded outflow (by at most the rate itself) so room appears
        // for it: at the fixed rate alone the outlet in a full trough kept 7 % owed forever (S2
        // Small, 93 % delivered, 2026-10-08).
        for (const SourceSpec& src : m_sources)
            if (src.cell.x == x && src.cell.y == y && src.cell.z == z) {
                const double owedRate = std::min(src.pending / dt, static_cast<double>(std::abs(src.rate)));
                const double q = (static_cast<double>(src.rate) + (src.rate >= 0.0f ? owedRate : -owedRate)) / m_grid.cellVolume();
                rhs[i] += q / dt;
            }
        // Particle transport only: an over-full cell (clustered particles, f > 1 after p2g) is asked
        // to push outward at no more than the one-cell free-fall rate per substep (§15.9 volume
        // control; fills never exceed 1 so this is inert for them).
        if (m_transport->ownsMass()) {
            const float fc2 = m_grid.f(x, y, z);
            const double vfall = std::sqrt(2.0 * static_cast<double>(m_params.gravity) * h);
            if (fc2 > 1.0f) {
                const double df = std::min<double>(fc2 - 1.0, vfall * dt / h);
                rhs[i] += df / (static_cast<double>(dt) * dt);
            } else if (fc2 < 1.0f && y + 1 < ny && !blocked(m_grid, x, y + 1, z) && m_grid.f(x, y + 1, z) > 0.0f
                       && std::abs(m_grid.v(x, y + 1, z)) * dt / h < 0.1 * vfall * dt / h
                       && (0.25 * std::pow(static_cast<double>(m_grid.u(x, y, z) + m_grid.u(x + 1, y, z)), 2)
                         + 0.25 * std::pow(static_cast<double>(m_grid.w(x, y, z) + m_grid.w(x, y, z + 1)), 2)) < 0.01 * vfall * vfall) {
                // ... in a QUIET cell only (lateral and vertical speed under a tenth of the one-cell
                // free fall): on a climbing sheet or a streaming front the under-full cells are
                // the surface in motion, and pulling them cost the run-up (2.06 -> 1.42 h0)
                // SYMMETRIC density control: a submerged under-full cell pulls. One-sided pushing
                // alone let the column's mean density drift below 1 and the trough spilled at 74 %
                // full (S2 Small on FLIP, 2026-10-08). The free-surface cell (nothing above) is left
                // to transport, as for fills.
                const double df = std::min<double>(1.0 - fc2, vfall * dt / h);
                rhs[i] -= df / (static_cast<double>(dt) * dt);
            }
        }
        // A partial liquid cell under a SOLID ceiling has no face its surface could rise through:
        // projected as full and divergence-free it can never take the water that would fill it, so
        // a sealed cavity filling through a hole stalled with its top layer at ~0.5 (41.9 of 48 m^3,
        // S5 Basin, 2026-10-08). Ask the projection for a net inflow that closes the void at no more
        // than the free-fall rate across one cell. Cells with a free surface above keep div = 0
        // (their surface rises by transport); water-above voids are closed by
        // compactSubmergedPartials.
        {
            const float fc = m_grid.f(x, y, z);
            if (fc < 1.0f && blocked(m_grid, x, y + 1, z) && !m_transport->ownsMass()) {
                const double vfall = std::sqrt(2.0 * static_cast<double>(m_params.gravity) * h);
                const double df = std::min<double>(1.0 - fc, vfall * dt / h);
                rhs[i] -= df / (static_cast<double>(dt) * dt);
            }
        }
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
    // A vertical face with water above it and room below it (air or a partial cell, not solid)
    // that is not a domain face is a DROP's bottom face: it keeps the gravity it has accumulated,
    // otherwise the halo overwrites it with the resting pool's ~0 every substep and the drop
    // hangs in the air forever (S3 Basin, h = 1: residue stacks at y 15..17 above a 14.2 m
    // surface, 2026-10-08). Lateral faces of thin water stay extrapolated (the fine dam-break
    // tongue needs the front's velocity, see above).
    std::vector<float> fallingV;
    std::vector<uint8_t> keepV(kv.size(), 0);
    for (int z = 0; z < nz; ++z) for (int y = 1; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        const size_t i = m_grid.vIdx(x, y, z);
        if (kv[i]) continue;
        const bool aboveWet = m_grid.f(x, y, z) > 0.0f;
        const bool roomBelow = !blocked(m_grid, x, y - 1, z) && m_grid.f(x, y - 1, z) < 1.0f;
        if (aboveWet && roomBelow) keepV[i] = 1;
    }
    fallingV = m_grid.vData();
    // Only the FIRST layer of thin-film faces is overwritten by the domain: that is the tongue the
    // front is pushing (the 0.35 m/s tip beside a 5 m/s front, above). Thin water farther out owns
    // its velocity - the explicit film physics (gravity, surface slope, advection) must be allowed
    // to accumulate there, or a 2 cm film on a ramp step three cells from the pool is reset to the
    // pool's ~0 every substep and never drains (S3 Basin, h = 1: films at x 6..8 y 15 after 12 s,
    // 2026-10-08). Dry faces take the band value out to kLayers as before.
    const std::vector<float> origU = m_grid.uData(), origW = m_grid.wData();
    constexpr int kLayers = 3;
    extrapolateLattice(m_grid.uData(), ku, nx + 1, ny, nz, 1);
    extrapolateLattice(m_grid.vData(), kv, nx, ny + 1, nz, 1);
    extrapolateLattice(m_grid.wData(), kw, nx, ny, nz + 1, 1);
    const std::vector<uint8_t> firstU = ku, firstV = kv, firstW = kw;
    extrapolateLattice(m_grid.uData(), ku, nx + 1, ny, nz, kLayers - 1);
    extrapolateLattice(m_grid.vData(), kv, nx, ny + 1, nz, kLayers - 1);
    extrapolateLattice(m_grid.wData(), kw, nx, ny, nz + 1, kLayers - 1);
    if (!(m_params.debugDisableStages & 8u)) {
        for (size_t i = 0; i < ku.size(); ++i) if (wu[i] && !firstU[i]) m_grid.uData()[i] = origU[i];
        for (size_t i = 0; i < kw.size(); ++i) if (ww[i] && !firstW[i]) m_grid.wData()[i] = origW[i];
        for (size_t i = 0; i < kv.size(); ++i) if (wv[i] && !firstV[i]) m_grid.vData()[i] = fallingV[i];
    }
    // A drop's face integrates gravity for as long as water keeps trickling onto it; bound it by a
    // three-cell free fall (a face at -43.7 m/s under a 1e-6 trickle drove the CFL substep count
    // and the back-traces around it, S3 Basin 2026-10-08). Droplets proper are the §12 pool.
    const float vFallMax = static_cast<float>(std::sqrt(2.0 * m_params.gravity * 3.0 * m_grid.h()));
    const bool clampFall = !m_transport->ownsMass();   // a particle falling five metres is allowed its 10 m/s
    for (size_t i = 0; i < keepV.size(); ++i) if (keepV[i]) { m_grid.vData()[i] = clampFall ? std::max(fallingV[i], -vFallMax) : fallingV[i]; kv[i] = 1; }
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

// A liquid cell is projected as if FULL, so the projection can never pull water down into a
// partial liquid cell: the face above it is made divergence-free for a full cell. Left alone, a
// settling pool freezes as two half layers (S3 Basin, h = 1: y13 at 0.53-0.90 under y14 at
// 0.33-0.50, kinetic energy never quiet, 2026-10-08). A void under water is unphysical, so close it
// explicitly: water directly above a partial liquid cell falls into it, at no more than the
// free-fall rate across one cell (sqrt(2 g h)). Thin cells below thin cells are NOT touched - their
// faces carry gravity themselves (see extrapolateVelocity) - and the true free surface (air above)
// is left to the projection. A first attempt did this through the pressure solve as a divergence
// target; the 1/dt^2 right-hand side perturbed every transient partial cell in a collapsing dam
// (fine front 15.3 m vs 16.0 m, Torricelli drain 30 % fast) and was replaced by this transport.
void WaterSolver::compactSubmergedPartials(float dt) {
    const float thr = m_params.liquidThreshold;
    const float h = m_grid.h();
    const float cap = static_cast<float>(std::sqrt(2.0 * m_params.gravity * h) * dt / h);   // cell fraction per substep
    const int nx = m_grid.nx(), ny = m_grid.ny(), nz = m_grid.nz();
    for (int z = 0; z < nz; ++z) for (int y = 0; y + 1 < ny; ++y) for (int x = 0; x < nx; ++x) {
        const float fb = m_grid.f(x, y, z);
        if (fb < thr || fb >= 1.0f || blocked(m_grid, x, y, z) || blocked(m_grid, x, y + 1, z)) continue;
        const float fa = m_grid.f(x, y + 1, z);
        if (fa <= 0.0f) continue;
        // only a QUIET face is a void under water: a draining column over a hole or a sheet
        // climbing a wall is moving water through this face already (Torricelli drain ran 28 %
        // fast and the wall crest fell 0.4 % short with the cap added on top of the flow), and
        // even a "top-up to the cap" on moving faces thinned the fine dam-break tongue (front
        // 15.3 m vs 16.0 m required, 2026-10-08). A resting pool's surface faces jitter at
        // ~0.1 m/s = 0.003 of a cell per substep, far under a tenth of the free-fall cap.
        // ... and never through a face that is flowing UP: a surface rising at the 0.08 m/s a
        // submerged pump demands is quieter than a tenth of the cap, and compaction undid each
        // substep's rise exactly (DiagSubmergedPump: 0.0017 up, 0.0017 back, 2026-10-08)
        const float vFace = m_grid.v(x, y + 1, z);
        const float already = std::abs(vFace) * dt / h;
        // (Tried 2026-10-08, Phase D: letting faces rising slower than 1 cm/s compact, to close the
        // residue sheet that keeps a bumped pool awake. It made the pool WORSE - ke 6e-6 -> 2e-4 -
        // the projection pushes back up what compaction pulls down. Defect #29 stays open.)
        if (vFace > 0.0f || already > 0.1f * cap) continue;
        // ... and only in water that is not streaming past: in a sheet flowing over a sill the
        // "partial cell with thin water above" is the free surface crossing the cell diagonally,
        // not a void, and squashing it per cell cost the fine-grid weir 15 % of its discharge
        // (DiagWeirFine: 18.9 -> 22.2 m^3 of 23.8 with compaction off, 2026-10-08). A resting
        // pool's lateral jitter is ~0.1 m/s; the gate is a tenth of the one-cell free-fall speed.
        const float uc = 0.5f * (m_grid.u(x, y, z) + m_grid.u(x + 1, y, z));
        const float wc = 0.5f * (m_grid.w(x, y, z) + m_grid.w(x, y, z + 1));
        const float vFree = static_cast<float>(std::sqrt(2.0 * m_params.gravity * h));
        if (uc * uc + wc * wc > 0.01f * vFree * vFree) continue;
        const float moved = std::min({fa, 1.0f - fb, cap});
        m_grid.f(x, y, z) = fb + moved;
        m_grid.f(x, y + 1, z) = fa - moved;
    }
}

// Sub-epsilon residue (f < kResidueFill, i.e. under a cubic centimetre in a 1 m cell) drains out of
// a drop only asymptotically - the donor-cell move is proportional to what is left - and every
// probe, renderer and "is the pool flat" gate that asks "f > 0" would see 1e-20 of water hanging
// at the rim forever (S3 Basin, h = 1, 2026-10-08). Merge it into the cell below, else a wet
// lateral neighbour with room, else drop it and COUNT it (StepReport::residueDropped) so the mass
// ledger stays honest.
void WaterSolver::sweepResidue(StepReport& r) {
    constexpr float kResidueFill = 1e-6f;
    const int nx = m_grid.nx(), ny = m_grid.ny(), nz = m_grid.nz();
    for (int z = 0; z < nz; ++z) for (int y = ny - 1; y >= 0; --y) for (int x = 0; x < nx; ++x) {
        const float fc = m_grid.f(x, y, z);
        if (fc <= 0.0f || fc >= kResidueFill) continue;
        auto tryInto = [&](int xn, int yn, int zn, bool needWet) {
            if (!m_grid.inBounds(xn, yn, zn) || blocked(m_grid, xn, yn, zn)) return false;
            const float fn = m_grid.f(xn, yn, zn);
            if (needWet && fn <= 0.0f) return false;
            if (fn + fc > 1.0f) return false;
            m_grid.f(xn, yn, zn) = fn + fc;
            return true;
        };
        bool placed = tryInto(x, y - 1, z, false)
                   || tryInto(x - 1, y, z, true) || tryInto(x + 1, y, z, true)
                   || tryInto(x, y, z - 1, true) || tryInto(x, y, z + 1, true);
        if (!placed) r.residueDropped += fc * static_cast<double>(m_grid.h()) * m_grid.h() * m_grid.h();
        m_grid.f(x, y, z) = 0.0f;
        m_grid.v(x, y, z) = 0.0f;
        if (y + 1 <= ny) m_grid.v(x, y + 1, z) = 0.0f;
    }
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
    r.boundaryExchange = applyBoundaries();   // Phase G: the ocean boundary, before anything moves
    m_fPrev.assign(m_grid.fData().begin(), m_grid.fData().end());
    const int n = substepsFor(dt);
    const float ds = dt / static_cast<float>(n);
    const double keWake = m_transport->ownsMass() ? m_params.keWakeParticles : m_params.keWake;
    const double keBefore = m_grid.kineticEnergy() / std::max(m_grid.totalMass(), 1e-9);
    const double keSettle = m_transport->ownsMass() ? std::max(m_params.keSettle, m_params.keWakeParticles) : m_params.keSettle;
    const bool settleBefore = keBefore < keSettle;   // the settle band (see SolverParams::keSettle): damping stands in for dissipation here, never above it
    // Substep order: TRANSPORT with the divergence-free field the last projection left, THEN body
    // forces, THEN project. Adding gravity before the advection moved water with u* = u + g dt: a
    // free-surface face rising at the 0.08 m/s a submerged pump demands read -0.08 m/s at
    // transport time (g dt = 0.16 m/s at 60 Hz) and the pump delivered nothing into a full
    // trough (S2 Small and the DiagSubmergedPump reference, 2026-10-08). Thin films keep their
    // own faces between substeps, so a falling drop still integrates gravity across substeps.
    const uint32_t off = m_params.debugDisableStages;
    for (int s = 0; s < n; ++s) {
        applySources(ds, r);
        enforceSolidFaces();
        m_transport->advect(m_grid, ds);
        enforceSolidFaces();
        if (!(off & 1u) && !m_transport->ownsMass()) compactSubmergedPartials(ds);
        applyGravity(ds);
        if (!(off & 2u)) applyThinFilmGradient(ds);
        if (!(off & 4u)) settleThinFilmTopFaces();
        enforceSolidFaces();
        project(ds, r);
        extrapolateVelocity();
        if (!(off & 4u)) settleThinFilmTopFaces();
        if (settleBefore && !(off & 32u)) applyRestDamping(ds);
    }
    if (!(off & 16u) && !m_transport->ownsMass()) sweepResidue(r);
    r.substeps = n;
    r.totalMass = m_transport->ownsMass() ? m_transport->ownedMass() : m_grid.totalMass();
    r.kineticEnergy = m_grid.kineticEnergy();
    double maxD = 0.0;
    const std::vector<float>& f = m_grid.fData();
    for (size_t i = 0; i < f.size(); ++i) maxD = std::max(maxD, static_cast<double>(std::abs(f[i] - m_fPrev[i])));
    r.maxDeltaF = maxD;
    const double specificKE = r.kineticEnergy / std::max(r.totalMass, 1e-9);   // m^2/s^2 per unit mass
    const double maxDQuiet = m_transport->ownsMass() ? m_params.maxDeltaFQuietParticles : m_params.maxDeltaFQuiet;
    const bool quiet = specificKE < keWake && maxD < maxDQuiet && r.sourceAdded == 0.0 && r.sinkRemoved == 0.0 && std::abs(r.boundaryExchange) < 1e-9;
    m_quietTicks = quiet ? m_quietTicks + 1 : 0;
    if (m_quietTicks >= m_params.restTicks) {
        if (m_transport->ownsMass()) {   // §15.9 rest conversion: particles -> fills, then the fill transport sleeps as usual
            m_transport->settle(m_grid);
            setTransport(std::make_unique<EulerianTransport>());
            r.restConverted = true;
        }
        m_asleep = true;
    }
    r.quietTicks = m_quietTicks; r.asleep = m_asleep;
    m_last = r;
    return r;
}

} // namespace Water
} // namespace Core
} // namespace Phyxel
