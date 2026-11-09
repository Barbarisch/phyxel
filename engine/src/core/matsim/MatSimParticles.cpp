// MatSimParticles — docs/MaterialSimPlan.md §6f (experiment E1). See the header.

#include "core/matsim/MatSimParticles.h"
#include "core/matsim/MatSimParticlesGpu.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <climits>
#include <cstdio>
#include <string>
#include <thread>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <deque>
#include <map>
#include <set>
#include <tuple>

namespace Phyxel::Core::MatSim {

namespace {

// 6l: a persistent pool for the wake scan (spawning 15 threads a wake cost ~2 ms, more than the scan). run(k, fn)
// calls fn(0..k-1), task 0 on the caller, and returns when all are done. One caller at a time (the mutex).
class ScanPool {
public:
    static ScanPool& get() { static ScanPool p; return p; }
    size_t workers() const { return threads_.size(); }
    void run(size_t k, const std::function<void(size_t)>& fn) {
        std::lock_guard<std::mutex> one(callerMu_);
        {
            std::lock_guard<std::mutex> lk(mu_);
            fn_ = &fn;
            tasks_ = k;
            next_ = 1;
            left_ = k - 1;
            ++gen_;
        }
        cv_.notify_all();
        fn(0);
        std::unique_lock<std::mutex> lk(mu_);
        done_.wait(lk, [&] { return left_ == 0; });
        fn_ = nullptr;
    }
private:
    ScanPool() {
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        for (unsigned i = 1; i < std::min(hw, 16u); ++i) threads_.emplace_back([this] { loop(); });
    }
    ~ScanPool() {
        { std::lock_guard<std::mutex> lk(mu_); stop_ = true; }
        cv_.notify_all();
        for (std::thread& t : threads_) t.join();
    }
    void loop() {
        uint64_t seen = 0;
        std::unique_lock<std::mutex> lk(mu_);
        for (;;) {
            cv_.wait(lk, [&] { return stop_ || (gen_ != seen && next_ < tasks_); });
            if (stop_) return;
            while (next_ < tasks_) {
                const size_t t = next_++;
                const std::function<void(size_t)>* fn = fn_;
                lk.unlock();
                (*fn)(t);
                lk.lock();
                if (--left_ == 0) done_.notify_one();
            }
            seen = gen_;
        }
    }
    std::vector<std::thread> threads_;
    std::mutex callerMu_, mu_;
    std::condition_variable cv_, done_;
    const std::function<void(size_t)>* fn_ = nullptr;
    size_t tasks_ = 0, next_ = 0, left_ = 0;
    uint64_t gen_ = 0;
    bool stop_ = false;
};
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
                p_.push_back({x + 0.5f, y + (cfg_.liftAtCentroid ? 0.5f * float(cellFill(c)) / float(full_) : 0.5f), z + 0.5f, 0.f, 0.f, 0.f, cellFill(c), m, 0.f, 0.f, 0.f, 0.f});
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

float MatSimParticles::restDensity() {
    if (rho0_ == 0.f) {
        const float h = cfg_.kernelCells, h2 = h * h;
        const float poly6 = 315.f / (64.f * kPi * std::pow(h, 9.f));
        const int R = int(std::ceil(h));
        for (int a = -R; a <= R; ++a)
            for (int b = -R; b <= R; ++b)
                for (int c = -R; c <= R; ++c) {
                    const float r2 = float(a * a + b * b + c * c);
                    if (r2 >= h2) continue;
                    const float d = h2 - r2;
                    rho0_ += poly6 * d * d * d;
                }
    }
    return rho0_;
}

int MatSimParticles::substepsFor(const MatSimCore& core, double dtFrame) const {
    const float g = cfg_.gravity * float(core.cellsPerM());
    const float vmax = maxSpeed() + g * float(dtFrame);
    const int ns = int(std::ceil(vmax * float(dtFrame) / cfg_.cflCells));
    return std::clamp(ns, 1, cfg_.maxSubsteps);
}

void MatSimParticles::sponge(const MatSimCore& core, double dtFrame) {
    if (!(cfg_.spongeTau > 0.f) || p_.empty()) return;
    const float k = std::exp(-float(dtFrame) / cfg_.spongeTau);
    const int W = std::max(1, cfg_.spongeCells);
    for (Particle& q : p_) {
        const int cx = floorI(q.x), cy = floorI(q.y), cz = floorI(q.z);
        bool near = false;
        for (int dy = -W; dy <= W && !near; ++dy)
            for (int dz = -W; dz <= W && !near; ++dz)
                for (int dx = -W; dx <= W && !near; ++dx) {
                    const IVec3 w{cx + dx, cy + dy, cz + dz};
                    if (!core.inBox(w)) continue;
                    const Cell c = core.at(w);
                    near = cellMaterial(c) == q.mat && cellFill(c) > 0;
                }
        if (near) { q.vx *= k; q.vy *= k; q.vz *= k; }
    }
}

void MatSimParticles::endFrame(double dtFrame) {
    const float k = std::min(1.f, float(dtFrame) / std::max(1e-3f, cfg_.settleTau));
    for (Particle& q : p_) { q.ax += (q.vx - q.ax) * k; q.ay += (q.vy - q.ay) * k; q.az += (q.vz - q.az) * k; q.age += float(dtFrame); }
}

void MatSimParticles::step(const MatSimCore& core, double dtFrame) {
    const size_t n = p_.size();
    if (!n || dtFrame <= 0) { lastSubsteps_ = 0; return; }
    if (!full_) full_ = core.full();
    if (gpu_) {
        std::string err;
        if (gpu_->step(*this, core, dtFrame, &err)) { sponge(core, dtFrame); return; }
        std::fprintf(stderr, "[MatSimParticles] GPU step refused (%s); stepping on the CPU\n", err.c_str());
    }
    const float h = cfg_.kernelCells, h2 = h * h;
    const float poly6 = 315.f / (64.f * kPi * std::pow(h, 9.f));
    const float spiky = -45.f / (kPi * std::pow(h, 6.f));
    auto W = [&](float r2) { if (r2 >= h2) return 0.f; const float d = h2 - r2; return poly6 * d * d * d; };
    // Rest density: a particle inside a perfect lattice of unit-mass particles one cell apart.
    restDensity();
    const float g = cfg_.gravity * float(core.cellsPerM());   // cells / s^2
    const float wdq = W(0.04f * h2);                    // s_corr reference: dq = 0.2 h

    // CFL substeps from the speed after this frame's gravity.
    const int ns = substepsFor(core, dtFrame);
    lastSubsteps_ = ns;
    const float dt = float(dtFrame) / float(ns);

    px_.resize(n); py_.resize(n); pz_.resize(n); lambda_.resize(n); rho_.resize(n);
    const int R = int(std::ceil(h));
    std::vector<float> denom(n, 1.f);   // the density constraint's denominator, reused by the divergence solve

    // Visit the fluid neighbours j of particle i (positions px_/py_/pz_) and the solid cells
    // within h; f(dx, dy, dz, r2, j) with j = -1 for a boundary particle.
    float bmass = 1.f;   // mass of the boundary particle being visited (j = -1)
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
                    // Boundary: a solid cell (mass 1), or CELL water (settled, E2) with mass fill/FULL.
                    const Cell bc = core.at({x, y, z});
                    const uint8_t bm = cellMaterial(bc);
                    if (bm == kSolid) bmass = 1.f;
                    else if (isLiquid(bm) && cellFill(bc) > 0) bmass = float(cellFill(bc)) / float(full_);
                    else continue;
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
                    const float mj = j < 0 ? bmass : float(p_[size_t(j)].units) / float(full_);
                    rho += mj * W(r2);
                    if (j == int(i)) return;
                    float gw[3];
                    gradW(ex, ey, ez, r2, gw);
                    gi[0] += mj * gw[0]; gi[1] += mj * gw[1]; gi[2] += mj * gw[2];
                    if (j >= 0) sum2 += (mj * mj) * (gw[0] * gw[0] + gw[1] * gw[1] + gw[2] * gw[2]) / (rho0_ * rho0_);
                });
                rho_[i] = rho;
                const float C = std::max(rho / rho0_ - 1.f, cfg_.minC);   // minC 0 = unilateral: no pull at the free surface
                sum2 += (gi[0] * gi[0] + gi[1] * gi[1] + gi[2] * gi[2]) / (rho0_ * rho0_);
                denom[i] = sum2 + cfg_.relaxation;
                lambda_[i] = -C / denom[i];
            }
            std::vector<float> dxs(n), dys(n), dzs(n);
            for (size_t i = 0; i < n; ++i) {
                float d[3] = {0, 0, 0};
                forNeighbours(i, [&](float ex, float ey, float ez, float r2, int j) {
                    if (j == int(i)) return;
                    const float mj = j < 0 ? bmass : float(p_[size_t(j)].units) / float(full_);
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
        // Divergence solve (Bender & Koschier 2015, DFSPH, in PBF form): remove the rate of density
        // change D_i = sum m_j (v_i - v_j) . gradW_ij / rho0 from the velocities. A projected
        // position jump at rest is a compress/expand velocity; this cancels it, real flow keeps its
        // divergence-free part. At the free surface (rho < rho0) only compression is removed.
        std::vector<float> kap(n);
        for (int it = 0; it < cfg_.divergenceIterations; ++it) {
            for (size_t i = 0; i < n; ++i) {
                float D = 0.f;
                forNeighbours(i, [&](float ex, float ey, float ez, float r2, int j) {
                    if (j == int(i)) return;
                    float gw[3];
                    gradW(ex, ey, ez, r2, gw);
                    const float mj = j < 0 ? bmass : float(p_[size_t(j)].units) / float(full_);
                    const float rvx = j < 0 ? vx[i] : vx[i] - vx[size_t(j)];
                    const float rvy = j < 0 ? vy[i] : vy[i] - vy[size_t(j)];
                    const float rvz = j < 0 ? vz[i] : vz[i] - vz[size_t(j)];
                    D += mj * (rvx * gw[0] + rvy * gw[1] + rvz * gw[2]);
                });
                D /= rho0_;
                if (rho_[i] < rho0_) D = std::max(D, 0.f);
                kap[i] = -D / denom[i];
            }
            std::vector<float> dvx(n), dvy(n), dvz(n);
            for (size_t i = 0; i < n; ++i) {
                float d[3] = {0, 0, 0};
                forNeighbours(i, [&](float ex, float ey, float ez, float r2, int j) {
                    if (j == int(i)) return;
                    float gw[3];
                    gradW(ex, ey, ez, r2, gw);
                    const float mj = j < 0 ? bmass : float(p_[size_t(j)].units) / float(full_);
                    const float kj = j < 0 ? kap[i] : kap[size_t(j)];
                    const float k = mj * (kap[i] + kj) / rho0_;
                    d[0] += k * gw[0]; d[1] += k * gw[1]; d[2] += k * gw[2];
                });
                dvx[i] = d[0]; dvy[i] = d[1]; dvz[i] = d[2];
            }
            for (size_t i = 0; i < n; ++i) { vx[i] += dvx[i]; vy[i] += dvy[i]; vz[i] += dvz[i]; }
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
    endFrame(dtFrame);   // time-averaged velocity (one update per frame)
    sponge(core, dtFrame);
}

void MatSimParticles::densityError(const MatSimCore& core, float& meanAbsC, float& maxC) {
    // Run a zero-length step's density evaluation: reuse step() with no iterations would move
    // particles, so evaluate directly with the same kernel.
    if (!full_) full_ = core.full();
    const float h = cfg_.kernelCells, h2 = h * h;
    const float poly6 = 315.f / (64.f * kPi * std::pow(h, 9.f));
    auto W = [&](float r2) { if (r2 >= h2) return 0.f; const float d = h2 - r2; return poly6 * d * d * d; };
    if (rho0_ == 0.f) {
        const int R = int(std::ceil(h));
        for (int a = -R; a <= R; ++a)
            for (int b = -R; b <= R; ++b)
                for (int c = -R; c <= R; ++c) rho0_ += W(float(a * a + b * b + c * c));
    }
    const int R = int(std::ceil(h));
    meanAbsC = 0.f; maxC = -1e9f;
    for (const Particle& pi : p_) {
        float rho = 0.f;
        for (const Particle& pj : p_) {
            const float ex = pi.x - pj.x, ey = pi.y - pj.y, ez = pi.z - pj.z;
            rho += float(pj.units) / float(full_) * W(ex * ex + ey * ey + ez * ez);
        }
        const int ix = floorI(pi.x), iy = floorI(pi.y), iz = floorI(pi.z);
        for (int y = iy - R; y <= iy + R; ++y)
            for (int z = iz - R; z <= iz + R; ++z)
                for (int x = ix - R; x <= ix + R; ++x) {
                    if (cellMaterial(core.at({x, y, z})) != kSolid) continue;
                    const float ex = pi.x - (x + 0.5f), ey = pi.y - (y + 0.5f), ez = pi.z - (z + 0.5f);
                    rho += W(ex * ex + ey * ey + ez * ez);
                }
        const float C = rho / rho0_ - 1.f;
        meanAbsC += std::fabs(C) / float(p_.size());
        maxC = std::max(maxC, C);
    }
}

// E2 (6f), body rule: a connected body of water is either all cells or all particles, never
// split along a moving seam. (The first E2 rules worked per particle / per cell: slow water
// settled at a slosh's turning points and woke again, cell water the CA moved under a particle was
// counted as boundary mass and blasted it out, and absorbing it made heavy particles that skew
// PBF's density. Known limit: a disturbance wakes its whole body, so a large lake needs a
// region-limited wake later.)

// Put `u` units of material m into cell w and, if it does not fit, the nearest open room
// (breadth-first over non-solid cells, up first, then the sides, then down). Returns what found no room.
static uint64_t putUnits(MatSimCore& core, IVec3 w0, uint8_t m, uint64_t u) {
    const uint32_t F = core.full();
    auto room = [&](IVec3 w) -> uint32_t {
        if (!core.inBox(w)) return 0;
        const Cell c = core.at(w);
        const uint8_t cm = cellMaterial(c);
        if (cm == kSolid || (cm != kEmpty && cm != m)) return 0;
        return F - cellFill(c);
    };
    auto put = [&](IVec3 w) {
        const uint32_t k = uint32_t(std::min<uint64_t>(room(w), u));
        if (!k) return;
        core.setCell(w, m, cellFill(core.at(w)) + k);
        u -= k;
    };
    put(w0);
    if (!u) return 0;
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
            put(nw);
            if (!u) break;
            q.push_back(nw);
        }
    }
    return u;
}

// Connected particle groups (6h): the face-, edge- and corner-connected components of the cells
// that hold a particle; a group is labelled by its lowest particle index (settle() keeps a
// remainder in that particle). Linear in occupied cells. It replaced union-find over particle
// pairs within h (9.3 of settle's 9.5 ms a frame at 1/9 m, Release); a liquid body packs ~1 cell
// apart, so bodies join the same way; only sparse spray splits finer (and spray is busy anyway).
std::vector<uint32_t> MatSimParticles::groups(const MatSimCore& core) {
    const size_t n = p_.size();
    const IVec3 o = core.origin(), sz = core.size();
    const size_t N = size_t(sz.x) * sz.y * sz.z;
    auto cellOf = [&](const Particle& q) {
        const int x = std::clamp(floorI(q.x) - o.x, 0, sz.x - 1), y = std::clamp(floorI(q.y) - o.y, 0, sz.y - 1),
                  z = std::clamp(floorI(q.z) - o.z, 0, sz.z - 1);
        return uint32_t(size_t(x) + size_t(sz.x) * (size_t(z) + size_t(sz.z) * size_t(y)));
    };
    constexpr uint32_t kNone = UINT32_MAX;
    gCell_.assign(N, kNone);   // per box cell: lowest particle index in it, then its component's label
    std::vector<uint32_t> cellOfP(n);
    std::vector<uint32_t> occupied;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t c = cellOf(p_[i]);
        cellOfP[i] = c;
        if (gCell_[c] == kNone) { gCell_[c] = uint32_t(i); occupied.push_back(c); }
    }
    // Flood each component; its label is the lowest particle index among its cells.
    std::vector<uint8_t> seen(N, 0);
    std::vector<uint32_t> comp, stack;
    for (uint32_t c0 : occupied) {
        if (seen[c0]) continue;
        comp.clear();
        stack.assign(1, c0);
        seen[c0] = 1;
        uint32_t label = gCell_[c0];
        while (!stack.empty()) {
            const uint32_t c = stack.back();
            stack.pop_back();
            comp.push_back(c);
            label = std::min(label, gCell_[c]);
            const int x = int(c % uint32_t(sz.x)), z = int((c / uint32_t(sz.x)) % uint32_t(sz.z)),
                      y = int(c / (uint32_t(sz.x) * uint32_t(sz.z)));
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int X = x + dx, Y = y + dy, Z = z + dz;
                        if (X < 0 || Y < 0 || Z < 0 || X >= sz.x || Y >= sz.y || Z >= sz.z) continue;
                        const uint32_t nc = uint32_t(size_t(X) + size_t(sz.x) * (size_t(Z) + size_t(sz.z) * size_t(Y)));
                        if (seen[nc] || gCell_[nc] == kNone) continue;
                        seen[nc] = 1;
                        stack.push_back(nc);
                    }
        }
        for (uint32_t c : comp) gCell_[c] = label;
    }
    std::vector<uint32_t> parent(n);
    for (size_t i = 0; i < n; ++i) parent[i] = gCell_[cellOfP[i]];
    return parent;
}

size_t MatSimParticles::settle(MatSimCore& core) {
    // A GROUP settles when every member is at least settleTau old and slower than settleSpeed
    // (time-averaged): all its particles deposit, lowest first, integer-exact.
    settleStats_ = {};
    if (cfg_.settleSpeed <= 0.f || p_.empty()) return 0;
    const float vs = cfg_.settleSpeed * float(core.cellsPerM());
    using Clock = std::chrono::steady_clock;
    auto tA = Clock::now();
    const std::vector<uint32_t> g = groups(core);
    settleStats_.msGroups = std::chrono::duration<double, std::milli>(Clock::now() - tA).count();
    tA = Clock::now();
    const size_t n = p_.size();
    // A group is at rest when at least settleFraction of its members are slow: the resting residual
    // keeps a tail at ~0.3 m/s (p99), so "every member" never held (299 of 354 slow, 40 s busy).
    std::vector<uint32_t> members(n, 0), slowCount(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const Particle& q = p_[i];
        ++members[g[i]];
        if (q.age >= cfg_.settleTau && q.ax * q.ax + q.ay * q.ay + q.az * q.az < vs * vs) ++slowCount[g[i]];
    }
    std::vector<uint8_t> groupBusy(n, 0);
    for (size_t r = 0; r < n; ++r)
        if (members[r] && float(slowCount[r]) < cfg_.settleFraction * float(members[r])) groupBusy[r] = 1;
    size_t big = 0;
    for (size_t r = 0; r < n; ++r) {
        if (!members[r]) continue;
        ++settleStats_.groups;
        if (groupBusy[r]) ++settleStats_.busySpeed;
        if (members[r] > members[big] || !members[big]) big = r;
    }
    settleStats_.bigMembers = members[big];
    settleStats_.bigSlow = slowCount[big];
    // Agree with wake(): a group settles only if its free surface is level to within
    // settleLevelCells (below the wake threshold), or the deposited cells are woken straight back (measured: a U-tube settled
    // at a slosh turning point with the arms ~0.3 m apart, woke, ... 390 particles at 40 s).
    // Surface of a column = its base (lowest open cell) + the group's water volume in it (cells):
    // a volume sum, so it is smooth (single particles jitter +-0.5 cell). Columns capped by a solid
    // above that surface (under a divider) are not free surfaces.
    {
        const uint32_t F = core.full();
        std::map<std::pair<uint32_t, int64_t>, uint64_t> vol;   // (group, column) -> units
        std::map<std::pair<uint32_t, int64_t>, float> top;      // (group, column) -> highest particle y
        for (size_t i = 0; i < n; ++i) {
            if (groupBusy[g[i]]) continue;
            const Particle& q = p_[i];
            const int64_t col = (int64_t(floorI(q.x)) << 32) ^ int64_t(uint32_t(floorI(q.z)));
            vol[{g[i], col}] += q.units;
            auto t = top.find({g[i], col});
            if (t == top.end()) top[{g[i], col}] = q.y;
            else t->second = std::max(t->second, q.y);
        }
        std::vector<float> lo(n, 1e30f), hi(n, -1e30f);
        const int y0 = core.origin().y, y1 = core.origin().y + core.size().y;
        std::map<std::pair<uint32_t, int64_t>, float> surfOf;   // open-topped columns only
        for (const auto& [key, u] : vol) {
            const int x = int(key.second >> 32), z = int(int32_t(uint32_t(key.second & 0xFFFFFFFFu)));
            int base = y0;
            while (base < y1 && cellMaterial(core.at({x, base, z})) == kSolid) ++base;
            float surf = float(base);
            for (int y = base; y < y1 && cellMaterial(core.at({x, y, z})) != kSolid; ++y)   // cell water under the particles counts too
                if (isLiquid(cellMaterial(core.at({x, y, z})))) surf += float(cellFill(core.at({x, y, z}))) / float(F);
            surf += float(u) / float(F);
            bool capped = false;
            for (int y = int(std::floor(surf)) + 1; y < y1 && !capped; ++y) capped = cellMaterial(core.at({x, y, z})) == kSolid;
            if (capped) continue;
            // 6i: the surface for the level test is GEOMETRIC, the column's top particle (averaged over
            // the patch below). Volume per column read 1-2 cells low within ~h of every wall (particles
            // pack thinner there) in the live rig at 1/3 m, so a level body never settled; the top of
            // the water does not depend on how densely it packs.
            surfOf[key] = top[key] + 0.5f;

        }
        // One column's volume is noisy: it counts a whole vertical line of particles, and the
        // packing is not a unit lattice, so some columns hold systematically more (measured at 1/9 m:
        // 22-32 cells across one x slice of a level 27-cell-deep arm; the noise grows with depth in
        // cells). Average each column over the group's open columns in a patch of settlePatchM
        // metres (3 x 3 cells at 1/3 m, as fitted there; 9 x 9 at 1/9 m), by summed-area tables.
        const int k = std::max(1, int(std::lround((cfg_.settlePatchM * float(core.cellsPerM()) - 1.f) * 0.5f)));
        for (auto it = surfOf.begin(); it != surfOf.end();) {
            const uint32_t r = it->first.first;
            auto end = it;
            int x0 = INT32_MAX, x1 = INT32_MIN, z0 = INT32_MAX, z1 = INT32_MIN;
            for (; end != surfOf.end() && end->first.first == r; ++end) {
                const int x = int(end->first.second >> 32), z = int(int32_t(uint32_t(end->first.second & 0xFFFFFFFFu)));
                x0 = std::min(x0, x); x1 = std::max(x1, x); z0 = std::min(z0, z); z1 = std::max(z1, z);
            }
            const int W = x1 - x0 + 1, D = z1 - z0 + 1;
            std::vector<double> sat(size_t(W + 1) * size_t(D + 1), 0.0), cat(size_t(W + 1) * size_t(D + 1), 0.0);
            auto at2 = [&](int i, int j) { return size_t(i) + size_t(W + 1) * size_t(j); };
            for (auto e = it; e != end; ++e) {
                const int x = int(e->first.second >> 32), z = int(int32_t(uint32_t(e->first.second & 0xFFFFFFFFu)));
                sat[at2(x - x0 + 1, z - z0 + 1)] = e->second;
                cat[at2(x - x0 + 1, z - z0 + 1)] = 1.0;
            }
            for (int j = 1; j <= D; ++j)
                for (int i = 1; i <= W; ++i) {
                    sat[at2(i, j)] += sat[at2(i - 1, j)] + sat[at2(i, j - 1)] - sat[at2(i - 1, j - 1)];
                    cat[at2(i, j)] += cat[at2(i - 1, j)] + cat[at2(i, j - 1)] - cat[at2(i - 1, j - 1)];
                }
            auto box = [&](const std::vector<double>& t, int i0, int j0, int i1, int j1) {   // inclusive, 1-based
                return t[at2(i1, j1)] - t[at2(i0 - 1, j1)] - t[at2(i1, j0 - 1)] + t[at2(i0 - 1, j0 - 1)];
            };
            for (auto e = it; e != end; ++e) {
                const int x = int(e->first.second >> 32), z = int(int32_t(uint32_t(e->first.second & 0xFFFFFFFFu)));
                const int i = x - x0 + 1, j = z - z0 + 1;
                const int i0 = std::max(1, i - k), i1 = std::min(W, i + k), j0 = std::max(1, j - k), j1 = std::min(D, j + k);
                const float avg = float(box(sat, i0, j0, i1, j1) / box(cat, i0, j0, i1, j1));
                if (r == big && avg < lo[r]) { settleStats_.loCol[0] = x; settleStats_.loCol[1] = z; settleStats_.loCol[2] = avg; }
                if (r == big && avg > hi[r]) { settleStats_.hiCol[0] = x; settleStats_.hiCol[1] = z; settleStats_.hiCol[2] = avg; }
                lo[r] = std::min(lo[r], avg);
                hi[r] = std::max(hi[r], avg);
            }
            it = end;
        }
        for (size_t r = 0; r < n; ++r)
            if (!groupBusy[r] && members[r] && hi[r] > lo[r] && hi[r] - lo[r] > cfg_.settleLevelCells) {
                groupBusy[r] = 1;
                ++settleStats_.busyLevel;
            }
        if (!groupBusy[big] || hi[big] >= lo[big]) settleStats_.bigSpread = hi[big] >= lo[big] ? hi[big] - lo[big] : 0.f;
    }
    // Absorb: a small group with a particle in, or right above, cell water of its liquid deposits now (absorbGroupMax).
    if (cfg_.absorbGroupMax > 0)
        for (size_t i = 0; i < n; ++i) {
            const uint32_t r = g[i];
            if (!groupBusy[r] || members[r] > uint32_t(cfg_.absorbGroupMax)) continue;
            const Particle& q = p_[i];
            const int x = floorI(q.x), y = floorI(q.y), z = floorI(q.z);
            bool wet = false;
            for (int yy = y; yy >= y - 1 && !wet; --yy) {
                if (!core.inBox({x, yy, z})) continue;
                const Cell c = core.at({x, yy, z});
                wet = cellMaterial(c) == q.mat && cellFill(c) > 0;
            }
            if (!wet) continue;
            groupBusy[r] = 0;
            ++settleStats_.absorbed;
        }
    settleStats_.msSurface = std::chrono::duration<double, std::milli>(Clock::now() - tA).count();
    tA = Clock::now();
    // Level deposit: each settling group's water (its particles plus any cell water already in its
    // space) is laid down as resting water would sit: filled bottom-up, layer by layer, over the
    // cells its particles occupy and the open cells beneath them, so the surface is flat to one
    // unit. Depositing particle by particle left a surface rough to ~1 cell (whole particles per
    // column) and the CA's head field woke the body straight back (1-12 trigger cells a cycle,
    // WakeUTubeFromCells). The settle check above already holds the surface within 0.8 cell, so
    // flattening moves at most a fraction of a cell. Integer-exact.
    const uint32_t Fd = core.full();
    const int yTop = core.origin().y + core.size().y;
    std::vector<uint8_t> gone(n, 0);
    size_t removed = 0;
    for (size_t r = 0; r < n; ++r) {
        if (groupBusy[r] || !members[r]) continue;
        uint8_t mat = 0;
        uint64_t total = 0;
        std::map<std::tuple<int, int, int>, bool> T;   // (y, z, x)
        std::map<std::pair<int, int>, int> colTop;      // (x, z) -> highest y in T
        for (size_t i = 0; i < n; ++i) {
            if (g[i] != r) continue;
            const Particle& q = p_[i];
            mat = q.mat;
            total += q.units;
            const int x = floorI(q.x), y = floorI(q.y), z = floorI(q.z);
            for (int yy = y; yy >= core.origin().y; --yy) {   // the particle's cell and the open cells below
                const Cell c = core.at({x, yy, z});
                const uint8_t cm = cellMaterial(c);
                if (cm == kSolid || (cm != kEmpty && cm != q.mat)) break;
                if (!T.emplace(std::make_tuple(yy, z, x), true).second && yy < y) break;   // the rest is already in
            }
            auto it = colTop.find({x, z});
            if (it == colTop.end() || it->second < y) colTop[{x, z}] = y;
        }
        // Complete the footprint: every column reaches the same top layer, or edge columns with
        // fewer particles end a layer or two low and the step reads as pressure to wake().
        int yMax = core.origin().y;
        for (const auto& [k, v] : T) { yMax = std::max(yMax, std::get<0>(k)); (void)v; }
        for (auto& [col, top] : colTop) {
            for (int y = top + 1; y <= yMax; ++y) {
                const Cell c = core.at({col.first, y, col.second});
                if (cellMaterial(c) == kSolid || (cellMaterial(c) != kEmpty && cellMaterial(c) != mat)) break;
                T.emplace(std::make_tuple(y, col.second, col.first), true);
                top = y;
            }
        }
        for (const auto& [k, v] : T) {   // existing cell water in the space joins the deposit
            const Cell c = core.at({std::get<2>(k), std::get<0>(k), std::get<1>(k)});
            if (cellMaterial(c) == mat) total += cellFill(c);
            (void)v;
        }
        // Room: add a cell above EVERY column's top (a whole layer, so the fill below shares it evenly) until the space
        // holds the water. 6k: stopping at the first column that made room left the remainder as one nearly full cell a
        // cell above the rest; the head field read it as the body's surface and woke the pool (PoolDropPhases, 1/9 m).
        while (uint64_t(T.size()) * Fd < total) {
            bool added = false;
            for (auto& [col, top] : colTop) {
                const int y = top + 1;
                if (y >= yTop) continue;
                const Cell c = core.at({col.first, y, col.second});
                if (cellMaterial(c) == kSolid || (cellMaterial(c) != kEmpty && cellMaterial(c) != mat)) continue;
                // 6k: a cell that already holds water joins with its water (it was overwritten: -256 units at 1/9 m,
                // PoolDropPhases; under the whole-body wake no cell water ever sat above a settling group).
                if (T.emplace(std::make_tuple(y, col.second, col.first), true).second && cellMaterial(c) == mat) total += cellFill(c);
                top = y;
                added = true;
            }
            if (!added) break;
        }
        // Fill layer by layer (T is ordered by y, then z, x).
        std::vector<IVec3> cells;
        for (const auto& [k, v] : T) { cells.push_back({std::get<2>(k), std::get<0>(k), std::get<1>(k)}); (void)v; }
        uint64_t left = total;
        size_t a = 0;
        while (a < cells.size()) {
            size_t b = a;
            while (b < cells.size() && cells[b].y == cells[a].y) ++b;
            const uint64_t cnt = b - a, cap = cnt * Fd;
            if (left >= cap) {
                for (size_t k = a; k < b; ++k) core.setCell(cells[k], mat, Fd);
                left -= cap;
            } else {
                const uint64_t q = left / cnt, rem = left % cnt;
                for (size_t k = a; k < b; ++k) core.setCell(cells[k], mat, uint32_t(q + (k - a < rem ? 1 : 0)));
                left = 0;
            }
            a = b;
        }
        if (left) left = putUnits(core, cells.empty() ? IVec3{floorI(p_[r].x), floorI(p_[r].y), floorI(p_[r].z)} : cells.back(), mat, left);
        if (left) {   // no room anywhere: the remainder stays a particle (the group's root)
            p_[r].units = uint32_t(left);
            for (size_t i = 0; i < n; ++i) if (g[i] == r && i != r) gone[i] = 1;
            continue;
        }
        for (size_t i = 0; i < n; ++i) if (g[i] == r) gone[i] = 1;
    }
    settleStats_.settled = 0;
    for (size_t r = 0; r < n; ++r) if (members[r] && !groupBusy[r]) ++settleStats_.settled;
    settleStats_.msDeposit = std::chrono::duration<double, std::milli>(Clock::now() - tA).count();
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) { if (gone[i]) { ++removed; continue; } p_[k++] = p_[i]; }
    p_.resize(k);
    return removed;
}

void MatSimParticles::settleBlockers(const MatSimCore& core, size_t& slow, size_t& groupsBusy) {
    const float vs = cfg_.settleSpeed * float(core.cellsPerM());
    slow = groupsBusy = 0;
    if (p_.empty()) return;
    const std::vector<uint32_t> g = groups(core);
    std::vector<uint32_t> members(p_.size(), 0), sl(p_.size(), 0);
    for (size_t i = 0; i < p_.size(); ++i) {
        const Particle& q = p_[i];
        ++members[g[i]];
        if (q.age >= cfg_.settleTau && q.ax * q.ax + q.ay * q.ay + q.az * q.az < vs * vs) { ++slow; ++sl[g[i]]; }
    }
    for (size_t r = 0; r < p_.size(); ++r)
        if (members[r] && float(sl[r]) < cfg_.settleFraction * float(members[r])) ++groupsBusy;
}

StepStats MatSimParticles::stepCells(MatSimCore& core, const std::function<StepStats(MatSimCore&)>& stepOne) {
    auto one = [&]() { return stepOne ? stepOne(core) : core.step(1); };
    if (!cfg_.caSeesParticles || p_.empty()) return one();
    std::vector<IVec3> held;
    held.reserve(p_.size());
    for (const Particle& q : p_)   // the cell of each particle and its neighbours (as wake's held mask)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) held.push_back({floorI(q.x) + dx, floorI(q.y) + dy, floorI(q.z) + dz});
    core.blockEmpty(held);
    const StepStats st = one();
    core.unblockAll();
    return st;
}

size_t MatSimParticles::wake(MatSimCore& core) {
    // A cell-water BODY (face-connected cells of one liquid) wakes whole when any of its cells is
    // about to move (head excess over an open / part-empty neighbour > wakeHeadCells, or at least a
    // quarter cell over a non-full space) or touches a particle (within one cell).
    wakeStats_ = {};
    if (cfg_.wakeHeadCells <= 0.f) return 0;
    using WClock = std::chrono::steady_clock;
    auto wms = [](WClock::time_point a, WClock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    auto wt0 = WClock::now();
    full_ = core.full();
    const uint32_t F = core.full();
    const IVec3 o = core.origin(), n = core.size();
    const auto& head = core.head();
    const uint32_t thr = uint32_t(cfg_.wakeHeadCells * float(F));
    auto idx = [&](int lx, int ly, int lz) { return size_t(lx) + size_t(n.x) * (size_t(lz) + size_t(n.z) * size_t(ly)); };
    const size_t N = size_t(n.x) * n.y * n.z;
    static const int kD[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    std::vector<uint8_t> nearP(N, 0);
    const float vMin = cfg_.wakeContactMinSpeed * float(core.cellsPerM());
    // Small groups (drops) are absorbed by settle() instead of waking what they touch (absorbGroupMax).
    std::vector<uint32_t> gsz;
    std::vector<uint32_t> grp;
    if (cfg_.absorbGroupMax > 0 && !p_.empty()) {
        grp = groups(core);
        gsz.assign(p_.size(), 0);
        for (uint32_t r : grp) ++gsz[r];
    }
    for (size_t qi = 0; qi < p_.size(); ++qi) {
        const Particle& q = p_[qi];
        if (vMin > 0.f) {
            float v2 = q.vx * q.vx + q.vy * q.vy + q.vz * q.vz;
            if (cfg_.wakeContactAveraged) {   // 6k: the EMA, de-biased for a young particle (its average starts at 0)
                const float b = 1.f - std::exp(-q.age / std::max(1e-3f, cfg_.settleTau));
                v2 = b > 1e-4f ? (q.ax * q.ax + q.ay * q.ay + q.az * q.az) / (b * b) : v2;
            }
            if (v2 < vMin * vMin) continue;   // at rest: it settles itself
        }
        if (!grp.empty() && gsz[grp[qi]] <= uint32_t(cfg_.absorbGroupMax)) continue;          // a drop: absorbed, not a wake
        const int cx = floorI(q.x) - o.x, cy = floorI(q.y) - o.y, cz = floorI(q.z) - o.z;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = cx + dx, y = cy + dy, z = cz + dz;
                    if (x >= 0 && y >= 0 && z >= 0 && x < n.x && y < n.y && z < n.z) nearP[idx(x, y, z)] = 1;
                }
    }
    // 6k: cells held by a particle (caSeesParticles): full for the falling and pressure triggers.
    std::vector<uint8_t> heldP;
    if (cfg_.caSeesParticles) {
        heldP.assign(N, 0);
        // A cell is held if a particle is within one cell of it: particles sit about a cell apart, so cells inside a
        // woken region often hold no particle centre, and the pool edge read them as an open hole (RegionalWakeTrace).
        for (const Particle& q : p_) {
            const int cx = floorI(q.x) - o.x, cy = floorI(q.y) - o.y, cz = floorI(q.z) - o.z;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int x = cx + dx, y = cy + dy, z = cz + dz;
                        if (x >= 0 && y >= 0 && z >= 0 && x < n.x && y < n.y && z < n.z) heldP[idx(x, y, z)] = 1;
                    }
        }
    }
    std::vector<uint8_t> trig(N, 0);
    bool any = false;
    auto wt1 = WClock::now();
    wakeStats_.prepMs = wms(wt0, wt1);
    // 6l: testCell reads the core's cell array by local index (the wake's idx is the core's index): the scan
    // is the levelling tail's largest cost (~49 ns a cell through core.at, 13.6 ms for a 1/9 m pool, Release).
    const Cell* C = core.cells().data();
    const size_t strideY = size_t(n.x) * size_t(n.z), strideZ = size_t(n.x);
    const double contactMin = double(cfg_.wakeContactMinFill) * F, pressMin = double(cfg_.wakePressureMinFill) * F,
                 roomMin = double(cfg_.wakePressureMinRoom) * F;
    // 6l: the scan runs on threads when it is large. Each thread takes a contiguous run of scan units (in scan
    // order) and its own accumulator; the accumulators merge in order, so counts and the first-8 pressure list
    // equal the serial scan's (testCell writes only its own trig[i]).
    struct ScanAcc {
        size_t contact = 0, falling = 0, pressure = 0, scanned = 0;
        std::vector<PressureTrigger> first;
        bool any = false;
    };
    auto testCell = [&](ScanAcc& a, int lx, int ly, int lz) {
                ++a.scanned;
                const size_t i = idx(lx, ly, lz);
                const Cell c = C[i];
                const uint8_t m = cellMaterial(c);
                const uint32_t cf = cellFill(c);
                if (!isLiquid(m) || cf == 0) return;
                // Contact wakes a cell at least wakeContactMinFill full (default 0: any contact; see the config).
                bool go = nearP[i] != 0 && double(cf) >= contactMin;
                if (go) ++a.contact;
                // Falling: at least half a cell over an EMPTY cell (outside the box reads SOLID). A cell over a
                // part-filled one is the CA's one-tick gravity job; counting it re-woke settled bodies every ~1 s
                // (4-18 trigger cells per cycle, WakeUTubeFromCells timeline).
                if (!go && ly > 0 && cf >= F / 2 && cellMaterial(C[i - strideY]) == kEmpty && !(!heldP.empty() && heldP[i - strideY])) {
                    go = true;
                    ++a.falling;
                }
                const uint32_t hw = head.empty() ? 0u : (head[i] & kHeadMask);
                if (!go && hw != 0 && double(cf) >= pressMin)
                    for (const auto& d : kD) {
                        const int nx = lx + d[0], ny = ly + d[1], nz = lz + d[2];
                        if (nx < 0 || ny < 0 || nz < 0 || nx >= n.x || ny >= n.y || nz >= n.z) continue;
                        const size_t j = i + ptrdiff_t(d[0]) + ptrdiff_t(d[2]) * ptrdiff_t(strideZ) + ptrdiff_t(d[1]) * ptrdiff_t(strideY);
                        const Cell x = C[j];
                        const uint8_t xm = cellMaterial(x);
                        if (xm == kSolid || (xm != kEmpty && xm != m) || cellFill(x) >= F) continue;
                        if (xm == kEmpty && !heldP.empty() && heldP[j]) continue;   // 6k: a particle holds it
                        if (double(F - cellFill(x)) < roomMin) continue;   // 6k: a small void: the CA fills it
                        // An EMPTY neighbour counts from its top: a one-cell step in a settled surface
                        // is the CA level rule's job, and reading it as a cell of excess re-woke
                        // settled bodies (1-12 trigger cells a cycle, WakeUTubeFromCells timeline).
                        const uint32_t ex = uint32_t(ny) * F + (xm == kEmpty ? F : cellFill(x));
                        if (hw - 1 > ex + thr) {
                            go = true;
                            ++a.pressure;
                            const IVec3 w{o.x + lx, o.y + ly, o.z + lz};
                            if (a.first.size() < 8) a.first.push_back({w, {o.x + nx, o.y + ny, o.z + nz}, hw, ex, cf, cellFill(x)});
                            break;
                        }
                    }
                if (go) { trig[i] = 1; a.any = true; }
    };
    // Scan units, in scan order: local boxes [lo, hi). Full scan: one per layer; skip-quiet: one per scanned brick.
    struct Unit { int x0, y0, z0, x1, y1, z1; };
    std::vector<Unit> units;
    if (!cfg_.wakeSkipQuiet) {
        for (int ly = 0; ly < n.y; ++ly) units.push_back({0, ly, 0, n.x, ly + 1, n.z});
    } else {
        // 6k: only bricks that changed in the last two ticks or hold a particle, dilated by one brick.
        const IVec3 bd = core.brickDim(), bl = core.brickLo();
        const auto& m1 = core.marksTick1();
        const auto& m2 = core.marksTick2();
        auto bidx = [&](int X, int Y, int Z) { return size_t(X) + size_t(bd.x) * (size_t(Z) + size_t(bd.z) * size_t(Y)); };
        std::vector<uint8_t> act(m1.size(), 0), scan(m1.size(), 0);
        for (size_t i = 0; i < act.size(); ++i) act[i] = uint8_t(m1[i] | m2[i]);
        auto fdiv8 = [](int v) { return v >= 0 ? v / 8 : -((-v + 7) / 8); };
        for (const Particle& q : p_) {
            const int X = fdiv8(floorI(q.x)) - bl.x, Y = fdiv8(floorI(q.y)) - bl.y, Z = fdiv8(floorI(q.z)) - bl.z;
            if (X >= 0 && Y >= 0 && Z >= 0 && X < bd.x && Y < bd.y && Z < bd.z) act[bidx(X, Y, Z)] = 1;
        }
        for (int Y = 0; Y < bd.y; ++Y)
            for (int Z = 0; Z < bd.z; ++Z)
                for (int X = 0; X < bd.x; ++X) {
                    if (!act[bidx(X, Y, Z)]) continue;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int x = X + dx, y = Y + dy, z = Z + dz;
                                if (x >= 0 && y >= 0 && z >= 0 && x < bd.x && y < bd.y && z < bd.z) scan[bidx(x, y, z)] = 1;
                            }
                }
        for (int Y = 0; Y < bd.y; ++Y)
            for (int Z = 0; Z < bd.z; ++Z)
                for (int X = 0; X < bd.x; ++X) {
                    if (!scan[bidx(X, Y, Z)]) continue;
                    const int x0 = std::max(o.x, (bl.x + X) * 8), x1 = std::min(o.x + n.x, (bl.x + X) * 8 + 8);
                    const int y0 = std::max(o.y, (bl.y + Y) * 8), y1 = std::min(o.y + n.y, (bl.y + Y) * 8 + 8);
                    const int z0 = std::max(o.z, (bl.z + Z) * 8), z1 = std::min(o.z + n.z, (bl.z + Z) * 8 + 8);
                    if (x0 < x1 && y0 < y1 && z0 < z1) units.push_back({x0 - o.x, y0 - o.y, z0 - o.z, x1 - o.x, y1 - o.y, z1 - o.z});
                }
    }
    auto runUnits = [&](ScanAcc& a, size_t u0, size_t u1) {
        for (size_t u = u0; u < u1; ++u) {
            const Unit& b = units[u];
            for (int y = b.y0; y < b.y1; ++y)
                for (int z = b.z0; z < b.z1; ++z)
                    for (int x = b.x0; x < b.x1; ++x) testCell(a, x, y, z);
        }
    };
    size_t cellsToScan = 0;
    for (const Unit& b : units) cellsToScan += size_t(b.x1 - b.x0) * size_t(b.y1 - b.y0) * size_t(b.z1 - b.z0);
    size_t threads = cfg_.wakeScanThreads > 0 ? size_t(cfg_.wakeScanThreads) : ScanPool::get().workers() + 1;
    threads = std::min<size_t>({threads, 16, units.size()});
    if (cellsToScan < cfg_.wakeScanParallelMin) threads = 1;
    std::vector<ScanAcc> accs(std::max<size_t>(1, threads));
    if (threads <= 1) {
        runUnits(accs[0], 0, units.size());
    } else {
        // Contiguous runs of about equal cell counts.
        std::vector<size_t> cut(threads + 1, units.size());
        cut[0] = 0;
        size_t acc = 0, k = 1;
        for (size_t u = 0; u < units.size() && k < threads; ++u) {
            const Unit& b = units[u];
            acc += size_t(b.x1 - b.x0) * size_t(b.y1 - b.y0) * size_t(b.z1 - b.z0);
            if (acc * threads >= cellsToScan * k) cut[k++] = u + 1;
        }
        // Tasks are the runs; the pool may run several on one worker (fewer workers than runs): still exact.
        ScanPool::get().run(threads, [&](size_t t) { runUnits(accs[t], cut[t], cut[t + 1]); });
    }
    wakeStats_.scanThreads = threads;
    for (const ScanAcc& a : accs) {
        wakeStats_.contact += a.contact;
        wakeStats_.falling += a.falling;
        wakeStats_.pressure += a.pressure;
        wakeStats_.scannedCells += a.scanned;
        for (const PressureTrigger& t : a.first)
            if (wakeStats_.firstPressure.size() < 8) wakeStats_.firstPressure.push_back(t);
        any = any || a.any;
    }
    auto wt2 = WClock::now();
    wakeStats_.scanMs = wms(wt1, wt2);
    if (!any) return 0;
    // Flood each triggered body (face-connected, same liquid) and lift all of it, in index order.
    std::vector<uint8_t> lift(N, 0);
    std::vector<IVec3> stack;
    const int radius = cfg_.wakeRadiusM > 0.f ? std::max(1, int(std::lround(cfg_.wakeRadiusM * float(core.cellsPerM())))) : 0;
    if (radius > 0) {
        // 6k: only the cells within `radius` face-connected steps (through the same liquid) of a trigger cell.
        // Multi-source breadth-first, all triggers at distance 0, in index order: deterministic.
        std::vector<uint16_t> dist(N, 0xFFFF);
        std::vector<size_t> q;
        for (size_t i = 0; i < N; ++i) if (trig[i]) { dist[i] = 0; lift[i] = 1; q.push_back(i); }
        for (size_t h = 0; h < q.size(); ++h) {
            const size_t i = q[h];
            if (dist[i] >= radius) continue;
            const int lx = int(i % size_t(n.x)), lz = int((i / size_t(n.x)) % size_t(n.z)), ly = int(i / (size_t(n.x) * size_t(n.z)));
            const uint8_t m = cellMaterial(core.at({o.x + lx, o.y + ly, o.z + lz}));
            for (const auto& d : kD) {
                const int x = lx + d[0], y = ly + d[1], z = lz + d[2];
                if (x < 0 || y < 0 || z < 0 || x >= n.x || y >= n.y || z >= n.z) continue;
                const size_t j = idx(x, y, z);
                if (dist[j] != 0xFFFF) continue;
                const Cell c = core.at({o.x + x, o.y + y, o.z + z});
                if (cellMaterial(c) != m || cellFill(c) == 0) continue;
                dist[j] = uint16_t(dist[i] + 1);
                lift[j] = 1;
                q.push_back(j);
            }
        }
        // Whole columns: a lifted cell also lifts the same liquid stacked above it. Otherwise the rim of the region
        // (reached low, through the body) leaves cell water resting on the particles, held up by the CA walls, and the
        // settle level test reads those columns' particles as a surface 1.7 cells low (RegionalWakeTrace, 1/3 m).
        for (int lz = 0; lz < n.z; ++lz)
            for (int lx = 0; lx < n.x; ++lx)
                for (int ly = 0; ly < n.y; ++ly) {
                    if (!lift[idx(lx, ly, lz)]) continue;
                    const uint8_t m = cellMaterial(core.at({o.x + lx, o.y + ly, o.z + lz}));
                    for (int y = ly + 1; y < n.y; ++y) {
                        const Cell c = core.at({o.x + lx, o.y + y, o.z + lz});
                        if (cellMaterial(c) != m || cellFill(c) == 0) break;
                        lift[idx(lx, y, lz)] = 1;
                    }
                }
    }
    for (int ly = 0; ly < n.y && radius == 0; ++ly)
        for (int lz = 0; lz < n.z; ++lz)
            for (int lx = 0; lx < n.x; ++lx) {
                if (!trig[idx(lx, ly, lz)] || lift[idx(lx, ly, lz)]) continue;
                const uint8_t m = cellMaterial(core.at({o.x + lx, o.y + ly, o.z + lz}));
                stack.assign(1, {lx, ly, lz});
                lift[idx(lx, ly, lz)] = 1;
                while (!stack.empty()) {
                    const IVec3 l = stack.back();
                    stack.pop_back();
                    for (const auto& d : kD) {
                        const int x = l.x + d[0], y = l.y + d[1], z = l.z + d[2];
                        if (x < 0 || y < 0 || z < 0 || x >= n.x || y >= n.y || z >= n.z || lift[idx(x, y, z)]) continue;
                        const Cell c = core.at({o.x + x, o.y + y, o.z + z});
                        if (cellMaterial(c) != m || cellFill(c) == 0) continue;
                        lift[idx(x, y, z)] = 1;
                        stack.push_back({x, y, z});
                    }
                }
            }
    auto wt3 = WClock::now();
    wakeStats_.liftMs = wms(wt2, wt3);
    size_t lifted = 0;
    const size_t first = p_.size();
    const uint32_t thin = uint32_t(double(cfg_.packThinBelow) * F);
    // Cells at least packThinBelow full: one particle each (at the cell centre, or the water's
    // centroid under liftAtCentroid, rejected as a default; see the config).
    for (int ly = 0; ly < n.y; ++ly)
        for (int lz = 0; lz < n.z; ++lz)
            for (int lx = 0; lx < n.x; ++lx) {
                if (!lift[idx(lx, ly, lz)]) continue;
                const IVec3 w{o.x + lx, o.y + ly, o.z + lz};
                const Cell c = core.at(w);
                if (cellFill(c) < thin) continue;
                p_.push_back({w.x + 0.5f, w.y + (cfg_.liftAtCentroid ? 0.5f * float(cellFill(c)) / float(F) : 0.5f), w.z + 0.5f, 0.f, 0.f, 0.f, cellFill(c), cellMaterial(c),
                              0.f, 0.f, 0.f, 0.f});
                core.setCell(w, kEmpty, 0);
                lift[idx(lx, ly, lz)] = 0;
                ++lifted;
            }
    // Thin cells (the rest of the mask): packed into particles of about one cell's water. A cluster grows
    // breadth-first through face-connected thin cells of one liquid until it holds >= F units; it becomes one
    // particle at the units-weighted mean of its cells' centres (a film's particle sits at the cell centre
    // height, its rest height on the floor). A leftover under F joins the nearest particle of this wake within
    // 2 cells if that keeps it <= 2F, else lifts as itself. Deterministic: seeds in index order.
    constexpr int kPackR = 2;           // cluster radius (cells, Chebyshev) around its seed
    std::vector<uint8_t> state(N, 0);   // 0 free, 1 queued, 2 taken
    std::vector<size_t> queue, members;
    for (int ly = 0; ly < n.y; ++ly)
        for (int lz = 0; lz < n.z; ++lz)
            for (int lx = 0; lx < n.x; ++lx) {
                const size_t s0 = idx(lx, ly, lz);
                if (!lift[s0] || state[s0] == 2) continue;
                const uint8_t m = cellMaterial(core.at({o.x + lx, o.y + ly, o.z + lz}));
                queue.assign(1, s0);
                members.clear();
                state[s0] = 1;
                uint64_t u = 0;
                double sx = 0, sy = 0, sz = 0;
                size_t head = 0;
                while (head < queue.size() && u < F) {
                    const size_t q = queue[head++];
                    const int qx = int(q % size_t(n.x)), qz = int((q / size_t(n.x)) % size_t(n.z)), qy = int(q / (size_t(n.x) * size_t(n.z)));
                    const IVec3 w{o.x + qx, o.y + qy, o.z + qz};
                    const uint32_t f = cellFill(core.at(w));
                    state[q] = 2;
                    members.push_back(q);
                    u += f;
                    sx += double(f) * (w.x + 0.5); sy += double(f) * (w.y + 0.5); sz += double(f) * (w.z + 0.5);
                    core.setCell(w, kEmpty, 0);
                    ++lifted;
                    for (const auto& d : kD) {
                        const int x = qx + d[0], y = qy + d[1], z = qz + d[2];
                        if (x < 0 || y < 0 || z < 0 || x >= n.x || y >= n.y || z >= n.z) continue;
                        const size_t j = idx(x, y, z);
                        if (!lift[j] || state[j] != 0 || cellMaterial(core.at({o.x + x, o.y + y, o.z + z})) != m) continue;
                        // Bounded: within packRadius cells of the seed, so a cluster cannot follow a film down a wall
                        // and put its centroid inside the solid it runs over (measured: the live-rig ledge, y 53.5).
                        if (std::abs(x - lx) > kPackR || std::abs(y - ly) > kPackR || std::abs(z - lz) > kPackR) continue;
                        state[j] = 1;
                        queue.push_back(j);
                    }
                }
                for (size_t k = head; k < queue.size(); ++k) state[queue[k]] = 0;   // not taken: free again
                if (u == 0) continue;
                float cx = float(sx / double(u)), cy = float(sy / double(u)), cz = float(sz / double(u));
                {   // the particle starts inside one of its own cells (the one nearest the centroid), at its centre height
                    size_t bm = members[0];
                    float bd2 = 1e30f;
                    for (size_t q : members) {
                        const float mx = float(o.x + int(q % size_t(n.x))) + 0.5f, my = float(o.y + int(q / (size_t(n.x) * size_t(n.z)))) + 0.5f,
                                    mz = float(o.z + int((q / size_t(n.x)) % size_t(n.z))) + 0.5f;
                        const float d2 = (mx - cx) * (mx - cx) + (my - cy) * (my - cy) + (mz - cz) * (mz - cz);
                        if (d2 < bd2) { bd2 = d2; bm = q; }
                    }
                    const float bx = float(o.x + int(bm % size_t(n.x))), by = float(o.y + int(bm / (size_t(n.x) * size_t(n.z)))),
                                bz = float(o.z + int((bm / size_t(n.x)) % size_t(n.z)));
                    cx = std::clamp(cx, bx + 0.05f, bx + 0.95f);
                    cz = std::clamp(cz, bz + 0.05f, bz + 0.95f);
                    cy = by + 0.5f;
                }
                if (u < F) {
                    size_t best = SIZE_MAX;
                    float bd = 4.0f;   // (2 cells)^2
                    for (size_t i = first; i < p_.size(); ++i) {
                        const Particle& t = p_[i];
                        if (t.mat != m || uint64_t(t.units) + u > 2ull * F) continue;
                        const float dx = t.x - cx, dy = t.y - cy, dz = t.z - cz, d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 <= bd) { bd = d2; best = i; }
                    }
                    if (best != SIZE_MAX) {
                        p_[best].units += uint32_t(u);
                        continue;
                    }
                }
                p_.push_back({cx, cy, cz, 0.f, 0.f, 0.f, uint32_t(u), m, 0.f, 0.f, 0.f, 0.f});
                ++wakeStats_.packed;
            }
    wakeStats_.particles = p_.size() - first;
    wakeStats_.packMs = wms(wt3, WClock::now());
    return lifted;
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
