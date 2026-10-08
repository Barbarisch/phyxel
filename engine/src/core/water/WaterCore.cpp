// WaterCore — docs/WaterCore.md §15. PHASE B RED STUB (2026-10-08).
//
// ⚑ This is deliberately the STRAWMAN: gravity plus a naive, unclamped flux with no solids, no
// pressure solve and no rest detection — the behaviour every WaterCoreTest must FAIL against so
// the suite is shown red before the real tick is written. A no-op stub would pass several
// invariants vacuously (still water, sealed cavity, solid faces); the strawman does not.
#include "core/water/WaterCore.h"
#include <algorithm>
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
    const int per = static_cast<int>(std::lround(1.0f / m_spec.h));   // cells per voxel per axis
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

// ─────────────────────────────────────────────────────────────── EulerianTransport (STUB) ────
// Naive unclamped flux: moves f by u*dt across every face with no regard for content, solids or
// holds. Creates and destroys mass. Replaced by the conservative donor-cell scheme (§15.1 step 4).
void EulerianTransport::advect(WaterGrid& g, float dt) {
    m_scratch.assign(g.fData().begin(), g.fData().end());
    const float h = g.h();
    for (int z = 0; z < g.nz(); ++z)
        for (int y = 0; y < g.ny(); ++y)
            for (int x = 0; x < g.nx(); ++x) {
                const float fx = g.u(x + 1, y, z) * dt / h, fy = g.v(x, y + 1, z) * dt / h, fz = g.w(x, y, z + 1) * dt / h;
                const float out = (fx + fy + fz) * g.f(x, y, z);
                m_scratch[g.idx(x, y, z)] -= out;
                if (x + 1 < g.nx()) m_scratch[g.idx(x + 1, y, z)] += fx * g.f(x, y, z);
                if (y + 1 < g.ny()) m_scratch[g.idx(x, y + 1, z)] += fy * g.f(x, y, z);
                if (z + 1 < g.nz()) m_scratch[g.idx(x, y, z + 1)] += fz * g.f(x, y, z);
            }
    g.fData().swap(m_scratch);
}

// ─────────────────────────────────────────────────────────────────── WaterSolver (STUB) ───────
WaterSolver::WaterSolver(WaterGrid& grid, SolidQuery solids, SolverParams params)
    : m_grid(grid), m_solids(std::move(solids)), m_params(params),
      m_transport(std::make_unique<EulerianTransport>()) {}

void WaterSolver::setTransport(std::unique_ptr<IWaterTransport> t) { if (t) m_transport = std::move(t); }
void WaterSolver::refreshSolids() { /* strawman: solids ignored */ }
SourceSpec& WaterSolver::addSource(const glm::ivec3& cellLocal, float rate) { m_sources.push_back(SourceSpec{cellLocal, rate, 0.0, 0.0}); return m_sources.back(); }
void WaterSolver::clearSources() { m_sources.clear(); }
void WaterSolver::addImpulse(const glm::vec3&, float, float, const glm::vec3&) {}
double WaterSolver::pressure(int, int, int) const { return 0.0; }
void WaterSolver::applySources(float, StepReport&) {}
void WaterSolver::applyGravity(float dt) {
    for (int z = 0; z < m_grid.nz(); ++z)
        for (int y = 0; y <= m_grid.ny(); ++y)
            for (int x = 0; x < m_grid.nx(); ++x) m_grid.v(x, y, z) -= m_params.gravity * dt;
}
void WaterSolver::project(float, StepReport&) {}
void WaterSolver::extrapolateVelocity() {}
void WaterSolver::enforceSolidFaces() {}
void WaterSolver::applyRestDamping(float) {}
int  WaterSolver::substepsFor(float) const { return 1; }
double WaterSolver::maxSpeed() const { return 0.0; }

StepReport WaterSolver::step(float dt) {
    StepReport r;
    applyGravity(dt);
    m_transport->advect(m_grid, dt);
    r.substeps = 1;
    r.totalMass = m_grid.totalMass();
    r.kineticEnergy = m_grid.kineticEnergy();
    m_last = r;
    return r;
}

} // namespace Water
} // namespace Core
} // namespace Phyxel
