// MatSimParticles — docs/MaterialSimPlan.md §6f (experiment E1). See the header.

#include "core/matsim/MatSimParticles.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <tuple>

namespace Phyxel::Core::MatSim {

namespace {
constexpr float kPi = 3.14159265358979f;

bool isLiquid(uint8_t m) {
    return m != kEmpty && m != kSolid && materialTable()[m].phase == Phase::Liquid;
}
int floorI(float v) { return int(std::floor(v)); }
} // namespace

size_t MatSimParticles::lift(MatSimCore& core, IVec3 lo, IVec3 size) {
    full_ = core.full();
    size_t n = 0;
    for (int y = lo.y; y < lo.y + size.y; ++y)
        for (int z = lo.z; z < lo.z + size.z; ++z)
            for (int x = lo.x; x < lo.x + size.x; ++x) {
                const Cell c = core.at({x, y, z});
                const uint8_t m = cellMaterial(c);
                if (!isLiquid(m) || cellFill(c) == 0) continue;
                p_.push_back({x + 0.5f, y + 0.5f, z + 0.5f, 0.f, 0.f, 0.f, cellFill(c), m});
                core.setCell({x, y, z}, kEmpty, 0);
                ++n;
            }
    return n;
}

uint64_t MatSimParticles::units(uint8_t mat) const {
    uint64_t u = 0;
    for (const Particle& q : p_)
        if (q.mat == mat) u += q.units;
    return u;
}

float MatSimParticles::maxSpeed() const {
    float m = 0.f;
    for (const Particle& q : p_) m = std::max(m, std::sqrt(q.vx * q.vx + q.vy * q.vy + q.vz * q.vz));
    return m;
}

bool MatSimParticles::solidAt(const MatSimCore& core, float x, float y, float z) const {
    return cellMaterial(core.at({floorI(x), floorI(y), floorI(z)})) == kSolid;
}

void MatSimParticles::buildGrid(const MatSimCore& core) {
    const float h = cfg_.kernelCells;
    const IVec3 o = core.origin(), s = core.size();
    gLo_ = {floorI(o.x / h) - 1, floorI(o.y / h) - 1, floorI(o.z / h) - 1};
    gDim_ = {floorI((o.x + s.x) / h) - gLo_.x + 2, floorI((o.y + s.y) / h) - gLo_.y + 2,
             floorI((o.z + s.z) / h) - gLo_.z + 2};
    const size_t nc = size_t(gDim_.x) * gDim_.y * gDim_.z;
    cellStart_.assign(nc + 1, 0);
    cellCount_.assign(nc, 0);
    const size_t n = p_.size();
    std::vector<uint32_t> key(n);
    for (size_t i = 0; i < n; ++i) {
        int gx = std::clamp(floorI(px_[i] / h) - gLo_.x, 0, gDim_.x - 1);
        int gy = std::clamp(floorI(py_[i] / h) - gLo_.y, 0, gDim_.y - 1);
        int gz = std::clamp(floorI(pz_[i] / h) - gLo_.z, 0, gDim_.z - 1);
        key[i] = uint32_t(gx + gDim_.x * (gz + gDim_.z * gy));
        ++cellCount_[key[i]];
    }
    for (size_t c = 0; c < nc; ++c) cellStart_[c + 1] = cellStart_[c] + cellCount_[c];
    sorted_.assign(n, 0);
    std::vector<uint32_t> fillPos(cellStart_.begin(), cellStart_.end() - 1);
    for (size_t i = 0; i < n; ++i) sorted_[fillPos[key[i]]++] = uint32_t(i);   // stable: ascending i per cell
}

void MatSimParticles::step(const MatSimCore& core, double dtFrame) {
    const size_t n = p_.size();
    if (!n || dtFrame <= 0) { lastSubsteps_ = 0; return; }
    if (!full_) full_ = core.full();
    const float h = cfg_.kernelCells, h2 = h * h;
    const float poly6 = 315.f / (64.f * kPi * std::pow(h, 9.f));
    const float spiky = -45.f / (kPi * std::pow(h, 6.f));
    auto W = [&](float r2) { if (r2 >= h2) return 0.f; const float d = h2 - r2; return poly6 * d * d * d; };
    // Rest density: a particle inside a perfect lattice of unit-mass particles one cell apart.
    if (rho0_ == 0.f) {
        const int R = int(std::ceil(h));
        for (int a = -R; a <= R; ++a)
            for (int b = -R; b <= R; ++b)
                for (int c = -R; c <= R; ++c) rho0_ += W(float(a * a + b * b + c * c));
    }
    const float g = cfg_.gravity * float(core.cellsPerM());   // cells / s^2
    const float wdq = W(0.04f * h2);                    // s_corr reference: dq = 0.2 h

    // CFL substeps from the speed after this frame's gravity.
    const float vmax = maxSpeed() + g * float(dtFrame);
    int ns = int(std::ceil(vmax * float(dtFrame) / cfg_.cflCells));
    ns = std::clamp(ns, 1, cfg_.maxSubsteps);
    lastSubsteps_ = ns;
    const float dt = float(dtFrame) / float(ns);

    px_.resize(n); py_.resize(n); pz_.resize(n); lambda_.resize(n); rho_.resize(n);
    const int R = int(std::ceil(h));
    std::vector<float> bx, by, bz;   // boundary (solid cell centre) offsets, per particle, rebuilt per iteration

    // Visit the fluid neighbours j of particle i (positions px_/py_/pz_) and the solid cells
    // within h; f(dx, dy, dz, r2, j) with j = -1 for a boundary particle.
    auto forNeighbours = [&](size_t i, auto&& f) {
        const float xi = px_[i], yi = py_[i], zi = pz_[i];
        const int gx = floorI(xi / h) - gLo_.x, gy = floorI(yi / h) - gLo_.y, gz = floorI(zi / h) - gLo_.z;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int cx = gx + dx, cy = gy + dy, cz = gz + dz;
                    if (cx < 0 || cy < 0 || cz < 0 || cx >= gDim_.x || cy >= gDim_.y || cz >= gDim_.z) continue;
                    const size_t c = size_t(cx) + gDim_.x * (size_t(cz) + gDim_.z * size_t(cy));
                    for (uint32_t k = cellStart_[c]; k < cellStart_[c + 1]; ++k) {
                        const uint32_t j = sorted_[k];
                        const float ex = xi - px_[j], ey = yi - py_[j], ez = zi - pz_[j];
                        const float r2 = ex * ex + ey * ey + ez * ez;
                        if (r2 < h2) f(ex, ey, ez, r2, int(j));
                    }
                }
        const int ix = floorI(xi), iy = floorI(yi), iz = floorI(zi);
        for (int y = iy - R; y <= iy + R; ++y)
            for (int z = iz - R; z <= iz + R; ++z)
                for (int x = ix - R; x <= ix + R; ++x) {
                    const float ex = xi - (x + 0.5f), ey = yi - (y + 0.5f), ez = zi - (z + 0.5f);
                    const float r2 = ex * ex + ey * ey + ez * ez;
                    if (r2 >= h2) continue;
                    if (cellMaterial(core.at({x, y, z})) != kSolid) continue;
                    f(ex, ey, ez, r2, -1);
                }
    };
    auto gradW = [&](float ex, float ey, float ez, float r2, float out[3]) {
        const float r = std::sqrt(r2);
        if (r <= 1e-6f || r >= h) { out[0] = out[1] = out[2] = 0.f; return; }
        const float d = h - r, s = spiky * d * d / r;
        out[0] = s * ex; out[1] = s * ey; out[2] = s * ez;
    };
    // Move from (ox,oy,oz) toward (x,y,z), stopping at solid cell faces, axis by axis.
    auto collide = [&](float ox, float oy, float oz, float& x, float& y, float& z) {
        const float eps = 1e-3f;
        float c[3] = {ox, oy, oz};
        const float t[3] = {x, y, z};
        for (int a = 0; a < 3; ++a) {
            float cand[3] = {c[0], c[1], c[2]};
            cand[a] = t[a];
            if (solidAt(core, cand[0], cand[1], cand[2])) {
                const float cell = std::floor(c[a]);
                cand[a] = t[a] > c[a] ? cell + 1.f - eps : cell + eps;
                if (solidAt(core, cand[0], cand[1], cand[2])) cand[a] = c[a];
            }
            c[a] = cand[a];
        }
        x = c[0]; y = c[1]; z = c[2];
    };

    for (int s = 0; s < ns; ++s) {
        // Predict.
        for (size_t i = 0; i < n; ++i) {
            Particle& q = p_[i];
            q.vy -= g * dt;
            float x = q.x + q.vx * dt, y = q.y + q.vy * dt, z = q.z + q.vz * dt;
            collide(q.x, q.y, q.z, x, y, z);
            px_[i] = x; py_[i] = y; pz_[i] = z;
        }
        buildGrid(core);
        // Density constraint.
        for (int it = 0; it < cfg_.iterations; ++it) {
            for (size_t i = 0; i < n; ++i) {
                float rho = 0.f, gi[3] = {0, 0, 0}, sum2 = 0.f;
                forNeighbours(i, [&](float ex, float ey, float ez, float r2, int j) {
                    const float mj = j < 0 ? 1.f : float(p_[size_t(j)].units) / float(full_);
                    rho += mj * W(r2);
                    if (j == int(i)) return;
                    float gw[3];
                    gradW(ex, ey, ez, r2, gw);
                    gi[0] += mj * gw[0]; gi[1] += mj * gw[1]; gi[2] += mj * gw[2];
                    if (j >= 0) sum2 += (mj * mj) * (gw[0] * gw[0] + gw[1] * gw[1] + gw[2] * gw[2]) / (rho0_ * rho0_);
                });
                rho_[i] = rho;
                const float C = std::max(rho / rho0_ - 1.f, 0.f);   // unilateral: no pull at the free surface
                sum2 += (gi[0] * gi[0] + gi[1] * gi[1] + gi[2] * gi[2]) / (rho0_ * rho0_);
                lambda_[i] = -C / (sum2 + cfg_.relaxation);
            }
            std::vector<float> dxs(n), dys(n), dzs(n);
            for (size_t i = 0; i < n; ++i) {
                float d[3] = {0, 0, 0};
                forNeighbours(i, [&](float ex, float ey, float ez, float r2, int j) {
                    if (j == int(i)) return;
                    const float mj = j < 0 ? 1.f : float(p_[size_t(j)].units) / float(full_);
                    const float lj = j < 0 ? lambda_[i] : lambda_[size_t(j)];
                    float sc = 0.f;
                    if (cfg_.tensileK > 0.f && j >= 0) { const float w = W(r2) / wdq; sc = -cfg_.tensileK * w * w * w * w; }
                    float gw[3];
                    gradW(ex, ey, ez, r2, gw);
                    const float k = mj * (lambda_[i] + lj + sc) / rho0_;
                    d[0] += k * gw[0]; d[1] += k * gw[1]; d[2] += k * gw[2];
                });
                dxs[i] = d[0]; dys[i] = d[1]; dzs[i] = d[2];
            }
            for (size_t i = 0; i < n; ++i) {
                const float w = cfg_.omega;
                float x = px_[i] + w * dxs[i], y = py_[i] + w * dys[i], z = pz_[i] + w * dzs[i];
                collide(px_[i], py_[i], pz_[i], x, y, z);
                px_[i] = x; py_[i] = y; pz_[i] = z;
            }
        }
        // Velocities from positions, then XSPH viscosity.
        std::vector<float> vx(n), vy(n), vz(n);
        for (size_t i = 0; i < n; ++i) {
            vx[i] = (px_[i] - p_[i].x) / dt; vy[i] = (py_[i] - p_[i].y) / dt; vz[i] = (pz_[i] - p_[i].z) / dt;
        }
        for (size_t i = 0; i < n; ++i) {
            float a[3] = {0, 0, 0};
            if (cfg_.viscosity > 0.f)
                forNeighbours(i, [&](float, float, float, float r2, int j) {
                    if (j < 0 || j == int(i)) return;
                    const float w = W(r2) / rho0_;
                    a[0] += (vx[size_t(j)] - vx[i]) * w; a[1] += (vy[size_t(j)] - vy[i]) * w; a[2] += (vz[size_t(j)] - vz[i]) * w;
                });
            Particle& q = p_[i];
            q.vx = vx[i] + cfg_.viscosity * a[0]; q.vy = vy[i] + cfg_.viscosity * a[1]; q.vz = vz[i] + cfg_.viscosity * a[2];
            q.x = px_[i]; q.y = py_[i]; q.z = pz_[i];
        }
    }
}

uint64_t MatSimParticles::deposit(MatSimCore& core) {
    const uint32_t F = core.full();
    // Units per cell, in deterministic cell order.
    std::map<std::tuple<int, int, int>, std::pair<uint64_t, uint8_t>> acc;   // key (y, z, x)
    for (const Particle& q : p_) {
        auto& e = acc[{floorI(q.y), floorI(q.z), floorI(q.x)}];
        e.first += q.units;
        e.second = q.mat;
    }
    p_.clear();
    uint64_t lost = 0;
    auto room = [&](IVec3 w, uint8_t m) -> uint32_t {
        if (!core.inBox(w)) return 0;
        const Cell c = core.at(w);
        const uint8_t cm = cellMaterial(c);
        if (cm == kSolid || (cm != kEmpty && cm != m)) return 0;
        return F - cellFill(c);
    };
    auto put = [&](IVec3 w, uint8_t m, uint64_t& u) {
        const uint32_t r = room(w, m);
        const uint32_t k = uint32_t(std::min<uint64_t>(r, u));
        if (!k) return;
        core.setCell(w, m, cellFill(core.at(w)) + k);
        u -= k;
    };
    for (auto& [key, e] : acc) {
        const IVec3 w0{std::get<2>(key), std::get<0>(key), std::get<1>(key)};
        uint64_t u = e.first;
        put(w0, e.second, u);
        if (!u) continue;
        // Overflow: breadth-first over non-solid cells, up first, then sideways, then down.
        static const int kOrder[6][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
        std::deque<IVec3> q{w0};
        std::map<std::tuple<int, int, int>, bool> seen{{{w0.y, w0.z, w0.x}, true}};
        while (u && !q.empty()) {
            const IVec3 w = q.front();
            q.pop_front();
            for (const auto& d : kOrder) {
                const IVec3 nw{w.x + d[0], w.y + d[1], w.z + d[2]};
                if (!core.inBox(nw) || cellMaterial(core.at(nw)) == kSolid) continue;
                if (!seen.emplace(std::make_tuple(nw.y, nw.z, nw.x), true).second) continue;
                put(nw, e.second, u);
                if (!u) break;
                q.push_back(nw);
            }
        }
        lost += u;
    }
    return lost;
}

} // namespace Phyxel::Core::MatSim
