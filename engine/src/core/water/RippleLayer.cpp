#include "core/water/RippleLayer.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace Phyxel::Core::Water {

namespace {
constexpr float kRho = 1000.0f;       // kg/m^3
constexpr float kSigma = 0.0728f;     // N/m, water-air surface tension (20 C)
constexpr float kNu = 1.0e-6f;        // m^2/s, kinematic viscosity of water (20 C)
constexpr float kPi = 3.14159265358979f;
constexpr float kSleepHeight = 2.0e-4f;   // m: below 0.2 mm everywhere ...
constexpr int kSleepSteps = 30;           // ... for 30 consecutive steps -> exact zero, asleep
}

float RippleLayer::targetResponse(float k, float gravity) {
    return k * (1.0f + kSigma * k * k / (kRho * gravity));
}

float RippleLayer::filmDamping(float gravity) {
    // 22.2: an inextensible surface film (natural pond water) damps a wave's amplitude at ~ k sqrt(nu omega / 8)
    // (Lamb, Hydrodynamics s351, the film limit) - far above clean water's 2 nu k^2. One constant (the kernel
    // cannot vary it per k), at the 20 cm band centre: ~0.047 /s.
    const float k = 2.0f * kPi / 0.20f;
    const float omega = std::sqrt(gravity * targetResponse(k, gravity));
    return k * std::sqrt(kNu * omega / 8.0f);
}

RippleLayer::RippleLayer(const glm::ivec2& origin, int nx, int nz, float gravity)
    : m_nx(std::max(1, nx)), m_nz(std::max(1, nz)), m_origin(origin), m_g(gravity), m_a(filmDamping(gravity)) {
    const size_t n = static_cast<size_t>(m_nx) * m_nz;
    m_r.assign(n, 0.0f); m_prev.assign(n, 0.0f); m_next.assign(n, 0.0f); m_tmp.assign(n, 0.0f);
    m_impulse.assign(n, 0.0f); m_kin.assign(n, 0.0f); m_kinSet.assign(n, 0); m_mask.assign(n, 1);
    static uint64_t s_nextUid = 1;
    m_uid = s_nextUid++;
    buildKernel();
    // convolve() folds the kernel (25.29): it must be symmetric in dx and in dz - it is by construction (one weight per class)
    for (int dz = -kRadius; dz <= kRadius; ++dz) for (int dx = -kRadius; dx <= kRadius; ++dx)
        assert(m_kernel[static_cast<size_t>(2 * kRadius + 1) * (dz + kRadius) + dx + kRadius] == m_kernel[static_cast<size_t>(2 * kRadius + 1) * (-dz + kRadius) - dx + kRadius]);
}

void RippleLayer::buildKernel() {
    // The 13 x 13 kernel whose spectrum is L(k) over the layer's band. Truncating the exact inverse transform
    // loses L's long-range tail (L ~ |k| is not smooth at 0): measured 9-30 % slow from 4 to 12 cells
    // (WaterRippleTest, 2026-10-10). So the kernel is FITTED: radially symmetric weights (one per offset class
    // |dx| >= |dz|, 28 for R = 6), least squares on L(k) for wavelengths 3.5 - 14 cells in 8 directions
    // (relative error), with L(0) = 0 held hard (a uniform r is not a wave). The fit ALSO covers every shorter
    // lattice wave down to the 2-cell Nyquist limit and every direction to the diagonal corner, at a lower weight:
    // fitted on the band alone, the response went negative near Nyquist and the field blew up (omega^2 = g K < 0
    // grows exponentially; measured 2026-10-10). Built once per gravity (static cache).
    static std::vector<float> s_cache; static float s_g = -1.0f;
    if (s_g == m_g && !s_cache.empty()) { m_kernel = s_cache; return; }
    const int R = kRadius, W = 2 * R + 1;
    std::vector<std::pair<int, int>> cls;
    for (int i = 0; i <= R; ++i) for (int j = 0; j <= i; ++j) cls.emplace_back(i, j);
    const int nc = static_cast<int>(cls.size());
    auto basis = [&](int c, double kx, double kz) {   // sum of cos over the class's symmetric offsets
        const int i = cls[c].first, j = cls[c].second;
        int offs[8][2]; int n = 0;
        const int cand[8][2] = {{i, j}, {-i, j}, {i, -j}, {-i, -j}, {j, i}, {-j, i}, {j, -i}, {-j, -i}};
        for (const auto& o : cand) {
            bool dup = false;
            for (int q = 0; q < n; ++q) if (offs[q][0] == o[0] && offs[q][1] == o[1]) dup = true;
            if (!dup) { offs[n][0] = o[0]; offs[n][1] = o[1]; ++n; }
        }
        double acc = 0.0;
        for (int q = 0; q < n; ++q) acc += std::cos((kx * offs[q][0] + kz * offs[q][1]) * kPitch);
        return acc;
    };
    std::vector<double> A(static_cast<size_t>(nc) * nc, 0.0), b(static_cast<size_t>(nc), 0.0);
    auto addRow = [&](double kx, double kz, double target, double wgt) {
        std::vector<double> row(static_cast<size_t>(nc));
        for (int c = 0; c < nc; ++c) row[c] = basis(c, kx, kz);
        for (int r = 0; r < nc; ++r) { b[r] += wgt * row[r] * target; for (int c = 0; c < nc; ++c) A[static_cast<size_t>(r) * nc + c] += wgt * row[r] * row[c]; }
    };
    for (int li = 0; li < 40; ++li) {
        const double cells = 3.5 * std::pow(14.0 / 3.5, li / 39.0);
        const double k = 2.0 * kPi / (cells * kPitch);
        const double target = targetResponse(static_cast<float>(k), m_g);
        for (int di = 0; di < 8; ++di) {
            const double th = (kPi / 4.0) * di / 7.0;
            addRow(k * std::cos(th), k * std::sin(th), target, 1.0 / (target * target));   // relative error
        }
    }
    for (int ix = 0; ix <= 24; ++ix) for (int iz = 0; iz <= ix; ++iz) {   // the whole lattice spectrum to Nyquist (k = pi / d)
        const double kx = kPi / kPitch * ix / 24.0, kz = kPi / kPitch * iz / 24.0, k = std::sqrt(kx * kx + kz * kz);
        if (k < 2.0 * kPi / (3.5 * kPitch)) continue;   // the band above has it
        const double target = targetResponse(static_cast<float>(k), m_g);
        addRow(kx, kz, target, 0.1 / (target * target));
    }
    addRow(0.0, 0.0, 0.0, 1e6);   // L(0) = 0, held hard
    for (int c = 0; c < nc; ++c) A[static_cast<size_t>(c) * nc + c] += 1e-9;   // conditioning
    // Gaussian elimination with partial pivoting
    std::vector<double> x(static_cast<size_t>(nc), 0.0);
    for (int col = 0; col < nc; ++col) {
        int piv = col;
        for (int r = col + 1; r < nc; ++r) if (std::abs(A[static_cast<size_t>(r) * nc + col]) > std::abs(A[static_cast<size_t>(piv) * nc + col])) piv = r;
        if (piv != col) { for (int c = 0; c < nc; ++c) std::swap(A[static_cast<size_t>(col) * nc + c], A[static_cast<size_t>(piv) * nc + c]); std::swap(b[col], b[piv]); }
        const double d = A[static_cast<size_t>(col) * nc + col];
        for (int r = col + 1; r < nc; ++r) {
            const double f = A[static_cast<size_t>(r) * nc + col] / d;
            for (int c = col; c < nc; ++c) A[static_cast<size_t>(r) * nc + c] -= f * A[static_cast<size_t>(col) * nc + c];
            b[r] -= f * b[col];
        }
    }
    for (int r = nc - 1; r >= 0; --r) {
        double acc = b[r];
        for (int c = r + 1; c < nc; ++c) acc -= A[static_cast<size_t>(r) * nc + c] * x[c];
        x[r] = acc / A[static_cast<size_t>(r) * nc + r];
    }
    m_kernel.assign(static_cast<size_t>(W) * W, 0.0f);
    for (int dz = -R; dz <= R; ++dz) for (int dx = -R; dx <= R; ++dx) {
        int i = std::abs(dx), j = std::abs(dz);
        if (j > i) std::swap(i, j);
        int c = 0; while (!(cls[c].first == i && cls[c].second == j)) ++c;
        m_kernel[static_cast<size_t>(dx + R) + static_cast<size_t>(W) * (dz + R)] = static_cast<float>(x[c]);
    }
    s_cache = m_kernel; s_g = m_g;
}

float RippleLayer::kernelResponse(float k) const {
    const int R = kRadius, W = 2 * R + 1;
    double acc = 0.0;
    for (int dz = -R; dz <= R; ++dz) for (int dx = -R; dx <= R; ++dx)
        acc += m_kernel[static_cast<size_t>(dx + R) + static_cast<size_t>(W) * (dz + R)] * std::cos(k * dx * kPitch);
    return static_cast<float>(acc);
}

glm::ivec2 RippleLayer::cellAt(float wx, float wz) const {
    return {static_cast<int>(std::floor(wx / kPitch)) - m_origin.x, static_cast<int>(std::floor(wz / kPitch)) - m_origin.y};
}

void RippleLayer::addImpulse(float wx, float wz, float impulsePerArea) {
    const glm::ivec2 c = cellAt(wx, wz);
    if (!inBounds(c.x, c.y) || impulsePerArea == 0.0f) return;
    // spread over a 3 x 3 binomial tent (1 2 1 / 4 per axis), the total impulse kept: a one-cell kick holds waves
    // down to the 2-cell Nyquist limit, which the kernel only approximates - they ran ahead of the real rings as
    // lattice junk (WaterRippleTest.AKickSpreadsIntoARingTrain, 2026-10-10). Cells off the layer keep their share
    // on the centre cell (the total is exact).
    const float w1[3] = {0.25f, 0.5f, 0.25f};
    float lost = 0.0f;
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
        const float share = impulsePerArea * w1[dx + 1] * w1[dz + 1];
        if (inBounds(c.x + dx, c.y + dz)) m_impulse[idx(c.x + dx, c.y + dz)] += share; else lost += share;
    }
    m_impulse[idx(c.x, c.y)] += lost;
    m_anyImpulse = true; m_asleep = false; m_quietSteps = 0;
}

void RippleLayer::addImpulseField(const std::vector<float>& raw) {
    if (raw.size() != m_impulse.size()) return;
    std::vector<float> t(raw.size(), 0.0f);
    bool any = false;
    for (int z = 0; z < m_nz; ++z) for (int x = 0; x < m_nx; ++x) {   // x: 1 2 1 / 4
        const float v = raw[idx(x, z)];
        if (v == 0.0f) continue;
        any = true;
        t[idx(x, z)] += 0.5f * v;
        t[idx(x > 0 ? x - 1 : x, z)] += 0.25f * v;
        t[idx(x + 1 < m_nx ? x + 1 : x, z)] += 0.25f * v;
    }
    if (!any) return;
    for (int z = 0; z < m_nz; ++z) for (int x = 0; x < m_nx; ++x) {   // z: 1 2 1 / 4
        const float v = t[idx(x, z)];
        if (v == 0.0f) continue;
        m_impulse[idx(x, z)] += 0.5f * v;
        m_impulse[idx(x, z > 0 ? z - 1 : z)] += 0.25f * v;
        m_impulse[idx(x, z + 1 < m_nz ? z + 1 : z)] += 0.25f * v;
    }
    m_anyImpulse = true; m_asleep = false; m_quietSteps = 0;
}

void RippleLayer::setKinematic(int x, int z, float velocity) {
    if (!inBounds(x, z)) return;
    m_kin[idx(x, z)] = velocity; m_kinSet[idx(x, z)] = 1;
    m_anyKin = true; m_asleep = false; m_quietSteps = 0;
}

void RippleLayer::convolve(const std::vector<float>& in, std::vector<float>& out) const {
    // Row-wise and FOLDED (25.29 RC cost). The fitted kernel is symmetric in dx and in dz (buildKernel fits one weight per
    // offset class), so for each output row the two input rows z +- dz are summed once into a zero-padded buffer, and that
    // sum is convolved with the 1 + R folded taps w (s[x + dx] + s[x - dx]) - 7 row passes of 7 taps instead of 13 x 13,
    // tight loops the compiler vectorises. Only each row's masked x-span is computed; outside the layer reads as 0 and
    // masked output is 0 (the per-cell form's rules). Measured on the creek window: WaterCore.md 25.29.
    const int R = kRadius, W = 2 * R + 1;
    std::fill(out.begin(), out.end(), 0.0f);
    std::vector<float> pad(static_cast<size_t>(m_nx + 2 * R), 0.0f);
    float* rs = pad.data() + R;   // rs[-R .. nx-1+R], zero outside the layer
    for (int z = 0; z < m_nz; ++z) {
        const uint8_t* mrow = &m_mask[idx(0, z)];
        int x0 = 0, x1 = m_nx - 1;
        while (x0 < m_nx && !mrow[x0]) ++x0;
        if (x0 >= m_nx) continue;
        while (!mrow[x1]) --x1;
        const int a = std::max(0, x0 - R), b = std::min(m_nx - 1, x1 + R);   // input span this row's outputs read
        float* orow = &out[idx(0, z)];
        for (int dz = 0; dz <= R; ++dz) {
            const int zp = z + dz, zm = z - dz;
            const bool hp = zp < m_nz, hm = dz > 0 && zm >= 0;
            if (!hp && !hm) continue;
            const float* ip = hp ? &in[idx(0, zp)] : nullptr;
            const float* im = hm ? &in[idx(0, zm)] : nullptr;
            for (int x = a; x <= b; ++x) rs[x] = (hp ? ip[x] : 0.0f) + (hm ? im[x] : 0.0f);
            const float* krow = &m_kernel[static_cast<size_t>(W) * (dz + R) + R];
            const float w0 = krow[0];
            for (int x = x0; x <= x1; ++x) orow[x] += w0 * rs[x];
            for (int dx = 1; dx <= R; ++dx) {
                const float w = krow[dx];
                for (int x = x0; x <= x1; ++x) orow[x] += w * (rs[x + dx] + rs[x - dx]);
            }
            for (int x = a; x <= b; ++x) rs[x] = 0.0f;   // keep the padding zero for the next row's span
        }
        for (int x = x0; x <= x1; ++x) if (!mrow[x]) orow[x] = 0.0f;   // masked: held at 0
    }
}

float RippleLayer::convolveAt(const std::vector<float>& in, int x, int z) const {
    const int R = kRadius, W = 2 * R + 1;
    float acc = 0.0f;
    for (int dz = -R; dz <= R; ++dz) for (int dx = -R; dx <= R; ++dx)
        if (inBounds(x + dx, z + dz)) acc += m_kernel[static_cast<size_t>(W) * (dz + R) + dx + R] * in[idx(x + dx, z + dz)];
    return acc;
}

int RippleLayer::planStep(float dt) {
    if (dt <= 0.0f || (m_asleep && !m_anyImpulse && !m_anyKin)) { m_accum = 0.0f; return 0; }
    // fixed steps (kStep): frame time accumulates, a stall beyond kMaxSubsteps steps is dropped
    m_accum = std::min(m_accum + dt, kStep * static_cast<float>(kMaxSubsteps));
    const int n = static_cast<int>(m_accum / kStep + 1e-4f);
    if (n <= 0) return 0;   // impulses and this frame's kinematic cells wait for the next whole step
    m_accum = std::max(0.0f, m_accum - static_cast<float>(n) * kStep);
    return n;
}

void RippleLayer::finishStep() {
    if (m_anyKin) { std::fill(m_kinSet.begin(), m_kinSet.end(), 0); m_anyKin = false; }
    sleepRule();
}

void RippleLayer::sleepRule() {
    // sleep: exact zero once the field has stayed below 0.2 mm for 30 steps
    float mx = 0.0f;
    for (float v : m_r) mx = std::max(mx, std::abs(v));
    m_quietSteps = mx < kSleepHeight ? m_quietSteps + 1 : 0;
    if (m_quietSteps >= kSleepSteps) {
        std::fill(m_r.begin(), m_r.end(), 0.0f); std::fill(m_prev.begin(), m_prev.end(), 0.0f);
        m_asleep = true; m_quietSteps = 0;
    }
}

void RippleLayer::step(float dt) {
    const int n = planStep(dt);
    if (n <= 0) return;
    const float ds = kStep;
    const float a = m_a;
    for (int s = 0; s < n; ++s) {
        // r'' + 2 a r' + g (K * r) = 0, centred: the amplitude decays at a and the frequency is untouched. (The
        // first cut used (2 - a dt)/(1 + a dt) and 1/(1 + a dt), written from memory of iWave's form: its
        // second difference carried an extra -2 a dt r, a hidden stiffness of 2 a / dt that ran every wave
        // 8-10 % fast at 240 Hz - WaterRippleTest.PlaneWaveRunsAtItsOwnSpeed, 2026-10-10.)
        const float damp = a * ds;
        const float c0 = 2.0f / (1.0f + damp), c1 = (1.0f - damp) / (1.0f + damp), c2 = m_g * ds * ds / (1.0f + damp);
        // Cauchy-Poisson impulses (dr/dt += -(K * I) / rho, i.e. prev += (K * I) ds / rho) and the wave term share ONE
        // convolution - K is linear (25.29 RC cost: the separate impulse pass doubled the step's price under flow forcing):
        //   next = c0 r - c1 (prev + (K * I) ds / rho) - c2 (K * r) = c0 r - c1 prev - K * (c2 r + c1 ds / rho I)
        // A kinematic cell sets its prev outright (the surface follows the body's sharp-edge remainder this frame), so its
        // impulse share is dropped there - as before - and its K * r is summed directly (a few cells).
        const bool imp = m_anyImpulse;
        const float ci = c1 * ds / kRho;
        for (size_t i = 0; i < m_r.size(); ++i) m_next[i] = c2 * m_r[i] + (imp ? ci * m_impulse[i] : 0.0f);
        convolve(m_next, m_tmp);
        if (imp) { std::fill(m_impulse.begin(), m_impulse.end(), 0.0f); m_anyImpulse = false; }
        for (int z = 0; z < m_nz; ++z) for (int x = 0; x < m_nx; ++x) {
            const size_t i = idx(x, z);
            if (!m_mask[i]) { m_next[i] = 0.0f; continue; }
            float v;
            if (m_anyKin && m_kinSet[i]) v = m_r[i] * c0 - (m_r[i] - m_kin[i] * ds) * c1 - c2 * convolveAt(m_r, x, z);
            else v = m_r[i] * c0 - m_prev[i] * c1 - m_tmp[i];
            if (m_openBorder) {   // water continuing past the volume: absorbed, never a reflecting edge
                const int rim = std::min(std::min(x, z), std::min(m_nx - 1 - x, m_nz - 1 - z));
                if (rim < 2) v *= 0.5f * static_cast<float>(rim);
            }
            m_next[i] = v;
        }
        // zero mean over the water: r is the surface's DEVIATION about the grid's own level, whose mean is the grid's
        // business (22.1). The kernel sums to zero, but masked walls and the absorbing rim cut it, so the mean drifted
        // (+5 mm over a whole pond in a live run, 2026-10-10) - and with no restoring force a mean never leaves.
        // Per CONNECTED body of water: one mean over the whole layer linked the two sides of a wall (a ring on one side
        // moved the water on the other - WaterRippleTest.AVoxelWallReflects).
        {
            const int nComp = labelComponents();
            std::vector<double> sum(static_cast<size_t>(nComp), 0.0), sumPrev(static_cast<size_t>(nComp), 0.0);
            std::vector<long> cnt(static_cast<size_t>(nComp), 0);
            for (size_t i = 0; i < m_next.size(); ++i) if (m_label[i] >= 0) { sum[m_label[i]] += m_next[i]; sumPrev[m_label[i]] += m_r[i]; ++cnt[m_label[i]]; }
            for (size_t i = 0; i < m_next.size(); ++i) if (m_label[i] >= 0) {
                const int c = m_label[i];
                m_next[i] -= static_cast<float>(sum[c] / cnt[c]); m_r[i] -= static_cast<float>(sumPrev[c] / cnt[c]);
            }
        }
        // 22.12 B1: the breaking limit on the new level AND the one becoming the previous (the breaking crest loses its
        // height and the motion that carries it). Pairwise and equal-and-opposite: each body's mean is untouched.
        if (m_breakSlope > 0.0f) m_broken += (limitSlope(m_next) + limitSlope(m_r)) * static_cast<double>(kPitch) * kPitch;
        m_prev.swap(m_r);
        m_r.swap(m_next);
    }
    finishStep();
}

float RippleLayer::planAdvect(const std::vector<glm::vec2>& vel, float dt) {
    if (dt <= 0.0f || vel.size() != m_r.size()) return 0.0f;
    if (m_asleep) { m_advT = 0.0f; return 0.0f; }
    m_advT += dt;
    float vmax = 0.0f;
    for (size_t i = 0; i < vel.size(); ++i) if (m_mask[i]) vmax = std::max(vmax, std::max(std::abs(vel[i].x), std::abs(vel[i].y)));
    if (vmax * m_advT < kPitch && m_advT < 0.5f) return 0.0f;
    const float t = m_advT; m_advT = 0.0f;
    return t;
}

void RippleLayer::advect(const std::vector<glm::vec2>& vel, float dtFrame) {
    const float dt = planAdvect(vel, dtFrame);
    if (dt <= 0.0f) return;
    // CUBIC (Catmull-Rom) sampling: the back-trace moves a field by a fraction of a cell every frame (0.5 m/s at 60 Hz: 0.075
    // cell), and bilinear sampling of that fraction smeared the short ripples away - a ring kept 0.17 of its energy over 2 s
    // (WaterRippleTest.ARingRidesTheCurrent, 2026-10-12). Catmull-Rom's error is fourth order in the wave number.
    auto cr = [](float p0, float p1, float p2, float p3, float t) {
        return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
    };
    auto sample = [&](const std::vector<float>& f, float fx, float fz) {
        const int x1 = static_cast<int>(std::floor(fx)), z1 = static_cast<int>(std::floor(fz));
        const float tx = fx - x1, tz = fz - z1;
        auto at = [&](int x, int z) { return (inBounds(x, z) && m_mask[idx(x, z)]) ? f[idx(x, z)] : 0.0f; };
        float row[4];
        for (int j = 0; j < 4; ++j) { const int z = z1 - 1 + j; row[j] = cr(at(x1 - 1, z), at(x1, z), at(x1 + 1, z), at(x1 + 2, z), tx); }
        return cr(row[0], row[1], row[2], row[3], tz);
    };
    m_next.assign(m_r.size(), 0.0f); m_tmp.assign(m_r.size(), 0.0f);
    for (int z = 0; z < m_nz; ++z) for (int x = 0; x < m_nx; ++x) {
        const size_t i = idx(x, z);
        if (!m_mask[i]) continue;
        const float sx = x - vel[i].x * dt / kPitch, sz = z - vel[i].y * dt / kPitch;
        m_next[i] = sample(m_r, sx, sz);
        m_tmp[i] = sample(m_prev, sx, sz);
    }
    m_r.swap(m_next); m_prev.swap(m_tmp);
}

void RippleLayer::shiftOrigin(const glm::ivec2& newOrigin) {
    if (newOrigin == m_origin) return;
    const glm::ivec2 d = newOrigin - m_origin;
    auto move = [&](std::vector<float>& f) {
        std::vector<float> g(f.size(), 0.0f);
        for (int z = 0; z < m_nz; ++z) for (int x = 0; x < m_nx; ++x) {
            const int ox = x + d.x, oz = z + d.y;   // the old cell at this world position
            if (inBounds(ox, oz)) g[idx(x, z)] = f[idx(ox, oz)];
        }
        f.swap(g);
    };
    move(m_r); move(m_prev);
    std::fill(m_impulse.begin(), m_impulse.end(), 0.0f); m_anyImpulse = false;
    std::fill(m_kinSet.begin(), m_kinSet.end(), 0); m_anyKin = false;
    m_origin = newOrigin;
    m_maskSeen.clear();   // the mask is re-set by the caller; the body labels are recomputed
}

int RippleLayer::labelComponents() {
    if (m_maskSeen == m_mask && m_label.size() == m_mask.size()) return m_nComp;
    m_maskSeen = m_mask;
    ++m_labelRev;
    m_label.assign(m_mask.size(), -1);
    m_nComp = 0;
    std::vector<size_t> stack;
    for (size_t s = 0; s < m_mask.size(); ++s) {
        if (!m_mask[s] || m_label[s] >= 0) continue;
        m_label[s] = m_nComp; stack.push_back(s);
        while (!stack.empty()) {
            const size_t i = stack.back(); stack.pop_back();
            const int x = static_cast<int>(i % m_nx), z = static_cast<int>(i / m_nx);
            const int nb[4][2] = {{x - 1, z}, {x + 1, z}, {x, z - 1}, {x, z + 1}};
            for (const auto& n : nb) {
                if (!inBounds(n[0], n[1])) continue;
                const size_t j = idx(n[0], n[1]);
                if (m_mask[j] && m_label[j] < 0) { m_label[j] = m_nComp; stack.push_back(j); }
            }
        }
        ++m_nComp;
    }
    return m_nComp;
}

double RippleLayer::limitSlope(std::vector<float>& r) {
    // Jacobi passes over a snapshot (order-independent): an edge steeper than the limit moves both ends toward each
    // other by its excess / 2, weighted 1/8 (a cell has 8 edges, so a pass never overshoots). Axis edges hold
    // |dr| <= s d, diagonal edges <= s d sqrt 2: together they bound the facet gradient within 8 % in every direction
    // (sec 22.5 deg), so a diagonal ring breaks like an axis one. Edges across the mask or between bodies are skipped.
    const float lim[2] = {m_breakSlope * kPitch, m_breakSlope * kPitch * 1.41421356f};
    constexpr float kTol = 1e-4f;   // m: an edge this close to the limit counts as at it
    constexpr int kMaxPasses = 16;
    const int dirs[4][3] = {{1, 0, 0}, {0, 1, 0}, {1, 1, 1}, {1, -1, 1}};   // dx, dz, diagonal?
    double moved = 0.0;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        bool any = false;
        for (int z = 0; z < m_nz && !any; ++z) for (int x = 0; x < m_nx && !any; ++x) {
            const size_t i = idx(x, z);
            if (m_label[i] < 0) continue;
            for (const auto& d : dirs) {
                const int xx = x + d[0], zz = z + d[1];
                if (!inBounds(xx, zz)) continue;
                const size_t j = idx(xx, zz);
                if (m_label[j] != m_label[i]) continue;
                if (std::abs(r[i] - r[j]) > lim[d[2]] + kTol) { any = true; break; }
            }
        }
        if (!any) break;   // a field gentler than the limit is untouched bit for bit
        m_delta.assign(r.size(), 0.0f);
        for (int z = 0; z < m_nz; ++z) for (int x = 0; x < m_nx; ++x) {
            const size_t i = idx(x, z);
            if (m_label[i] < 0) continue;
            for (const auto& d : dirs) {
                const int xx = x + d[0], zz = z + d[1];
                if (!inBounds(xx, zz)) continue;
                const size_t j = idx(xx, zz);
                if (m_label[j] != m_label[i]) continue;
                const float diff = r[i] - r[j], ex = std::abs(diff) - lim[d[2]];
                if (ex <= 0.0f) continue;
                const float t = (diff > 0.0f ? 1.0f : -1.0f) * 0.5f * ex * 0.125f;
                m_delta[i] -= t; m_delta[j] += t;
            }
        }
        for (size_t i = 0; i < r.size(); ++i) { r[i] += m_delta[i]; moved += std::abs(m_delta[i]); }
    }
    return moved;
}

float RippleLayer::maxAbs() const {
    float mx = 0.0f;
    for (float v : m_r) mx = std::max(mx, std::abs(v));
    return mx;
}

double RippleLayer::energyProxy() const {
    double e = 0.0;
    for (size_t i = 0; i < m_r.size(); ++i) { const double d = m_r[i] - m_prev[i]; e += static_cast<double>(m_r[i]) * m_r[i] + d * d; }
    return e;
}

}  // namespace Phyxel::Core::Water
