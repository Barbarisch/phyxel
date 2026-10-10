#include "core/water/RippleLayer.h"
#include <algorithm>
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
    buildKernel();
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

void RippleLayer::setKinematic(int x, int z, float velocity) {
    if (!inBounds(x, z)) return;
    m_kin[idx(x, z)] = velocity; m_kinSet[idx(x, z)] = 1;
    m_anyKin = true; m_asleep = false; m_quietSteps = 0;
}

void RippleLayer::convolve(const std::vector<float>& in, std::vector<float>& out) const {
    const int R = kRadius, W = 2 * R + 1;
    for (int z = 0; z < m_nz; ++z) for (int x = 0; x < m_nx; ++x) {
        if (!m_mask[idx(x, z)]) { out[idx(x, z)] = 0.0f; continue; }   // masked: held at 0 anyway (walls and dry ground are most of a pond's box)
        float acc = 0.0f;
        for (int dz = -R; dz <= R; ++dz) {
            const int zz = z + dz;
            if (zz < 0 || zz >= m_nz) continue;   // outside the layer: r = 0 (masked)
            const float* krow = &m_kernel[static_cast<size_t>(W) * (dz + R) + R];
            for (int dx = -R; dx <= R; ++dx) {
                const int xx = x + dx;
                if (xx < 0 || xx >= m_nx) continue;
                acc += krow[dx] * in[idx(xx, zz)];
            }
        }
        out[idx(x, z)] = acc;
    }
}

void RippleLayer::step(float dt) {
    if (dt <= 0.0f || (m_asleep && !m_anyImpulse && !m_anyKin)) return;
    int n = static_cast<int>(std::ceil(dt / kMaxStep - 1e-6f));
    n = std::clamp(n, 1, kMaxSubsteps);
    const float ds = std::min(dt / static_cast<float>(n), kMaxStep);
    const float a = m_a;
    for (int s = 0; s < n; ++s) {
        if (m_anyImpulse) {   // Cauchy-Poisson: dr/dt += -(K * I) / rho
            convolve(m_impulse, m_tmp);
            for (size_t i = 0; i < m_r.size(); ++i) if (m_mask[i]) m_prev[i] += m_tmp[i] / kRho * ds;
            std::fill(m_impulse.begin(), m_impulse.end(), 0.0f);
            m_anyImpulse = false;
        }
        if (m_anyKin)   // the surface follows the body's sharp-edge remainder this frame
            for (size_t i = 0; i < m_r.size(); ++i) if (m_kinSet[i] && m_mask[i]) m_prev[i] = m_r[i] - m_kin[i] * ds;
        convolve(m_r, m_tmp);
        // r'' + 2 a r' + g (K * r) = 0, centred: the amplitude decays at a and the frequency is untouched. (The
        // first cut used (2 - a dt)/(1 + a dt) and 1/(1 + a dt), written from memory of iWave's form: its
        // second difference carried an extra -2 a dt r, a hidden stiffness of 2 a / dt that ran every wave
        // 8-10 % fast at 240 Hz - WaterRippleTest.PlaneWaveRunsAtItsOwnSpeed, 2026-10-10.)
        const float damp = a * ds;
        const float c0 = 2.0f / (1.0f + damp), c1 = (1.0f - damp) / (1.0f + damp), c2 = m_g * ds * ds / (1.0f + damp);
        for (int z = 0; z < m_nz; ++z) for (int x = 0; x < m_nx; ++x) {
            const size_t i = idx(x, z);
            if (!m_mask[i]) { m_next[i] = 0.0f; continue; }
            float v = m_r[i] * c0 - m_prev[i] * c1 - c2 * m_tmp[i];
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
        m_prev.swap(m_r);
        m_r.swap(m_next);
    }
    if (m_anyKin) { std::fill(m_kinSet.begin(), m_kinSet.end(), 0); m_anyKin = false; }
    // sleep: exact zero once the field has stayed below 0.2 mm for 30 steps
    float mx = 0.0f;
    for (float v : m_r) mx = std::max(mx, std::abs(v));
    m_quietSteps = mx < kSleepHeight ? m_quietSteps + 1 : 0;
    if (m_quietSteps >= kSleepSteps) {
        std::fill(m_r.begin(), m_r.end(), 0.0f); std::fill(m_prev.begin(), m_prev.end(), 0.0f);
        m_asleep = true; m_quietSteps = 0;
    }
}

int RippleLayer::labelComponents() {
    if (m_maskSeen == m_mask && m_label.size() == m_mask.size()) return m_nComp;
    m_maskSeen = m_mask;
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
