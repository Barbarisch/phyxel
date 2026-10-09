#include "core/water/ShoreSolver.h"

#include <algorithm>
#include <cmath>

namespace Phyxel::Core::Water {

namespace {

struct State { double d, u, v; };     // depth, face-normal velocity, transverse velocity
struct Flux { double m, p, t; };      // mass, normal momentum, transverse momentum

// monotonized-central limiter (van Leer 1977): second order where smooth, TVD at extrema; less
// diffusive than minmod (which lost 20 % of the swell over 36 m)
inline double minmod(double a, double b) {
    if (a > 0.0 && b > 0.0) return std::min(std::min(2.0 * a, 2.0 * b), 0.5 * (a + b));
    if (a < 0.0 && b < 0.0) return std::max(std::max(2.0 * a, 2.0 * b), 0.5 * (a + b));
    return 0.0;
}

// HLL flux across a face from the left state L to the right state R; Toro's two-rarefaction wave
// speed estimates with the dry-bed limits (a dry side gives the wet side's rarefaction speed).
inline Flux hll(const State& L, const State& R, double g) {
    const bool wl = L.d > 0.0, wr = R.d > 0.0;
    if (!wl && !wr) return {0.0, 0.0, 0.0};
    const double cl = std::sqrt(g * std::max(L.d, 0.0)), cr = std::sqrt(g * std::max(R.d, 0.0));
    double sl, sr;
    if (!wl)      { sl = R.u - 2.0 * cr; sr = R.u + cr; }
    else if (!wr) { sl = L.u - cl;       sr = L.u + 2.0 * cl; }
    else {
        const double us = 0.5 * (L.u + R.u) + cl - cr;
        const double cs = 0.5 * (cl + cr) + 0.25 * (L.u - R.u);
        sl = std::min(L.u - cl, us - cs);
        sr = std::max(R.u + cr, us + cs);
    }
    const Flux fl{L.d * L.u, L.d * L.u * L.u + 0.5 * g * L.d * L.d, 0.0};
    const Flux fr{R.d * R.u, R.d * R.u * R.u + 0.5 * g * R.d * R.d, 0.0};
    Flux f;
    if (sl >= 0.0) f = fl;
    else if (sr <= 0.0) f = fr;
    else {
        const double inv = 1.0 / (sr - sl);
        f.m = (sr * fl.m - sl * fr.m + sl * sr * (R.d - L.d)) * inv;
        f.p = (sr * fl.p - sl * fr.p + sl * sr * (R.d * R.u - L.d * L.u)) * inv;
    }
    f.t = f.m >= 0.0 ? f.m * L.v : f.m * R.v;   // the transverse momentum rides the mass flux
    return f;
}

// A cell's reconstructed values on one of its faces.
struct Side { double eta, d, b, u, v; };

}  // namespace

ShoreSolver::ShoreSolver(const glm::vec2& originXZ, int nx, int nz, float h, ShoreParams params)
    : m_origin(originXZ), m_nx(nx), m_nz(nz), m_h(h), m_params(params) {
    const size_t n = static_cast<size_t>(nx) * nz;
    m_cols.assign(n, ShoreColumn{});
    m_prescribedVel.assign(n, glm::vec2(0.0f));
    m_etaPrev.assign(n, 0.0f);
    m_eta0.assign(n, 0.0f); m_u0.assign(n, 0.0f); m_w0.assign(n, 0.0f);
    m_eta1.assign(n, 0.0f); m_u1.assign(n, 0.0f); m_w1.assign(n, 0.0f);
    m_dEta.assign(n, 0.0); m_dQx.assign(n, 0.0); m_dQz.assign(n, 0.0);
}

void ShoreSolver::prescribe(int x, int z, double eta, const glm::vec2& velocity) {
    ShoreColumn& c = col(x, z);
    c.prescribed = 1;
    const double before = c.eta;
    c.eta = std::max(eta, static_cast<double>(c.bed));
    m_pendingExchange += (c.eta - before) * m_h * m_h;   // the ocean raised or lowered itself: counted
    m_prescribedVel[idx(x, z)] = velocity;
    c.u = velocity.x; c.w = velocity.y;
}

void ShoreSolver::clearPrescriptions() {
    for (auto& c : m_cols) c.prescribed = 0;
}

double ShoreSolver::totalMass() const {
    double m = 0.0;
    for (const auto& c : m_cols) m += std::max(c.eta - c.bed, 0.0);
    return m * m_h * m_h;
}

ShoreStepReport ShoreSolver::step(float dt) {
    ShoreStepReport r;
    // CFL from the fastest signal: |u| + |w| + sqrt(g d)
    float cmax = 0.0f;
    for (const ShoreColumn& c : m_cols) {
        const float d = static_cast<float>(std::max(c.eta - c.bed, 0.0));
        cmax = std::max(cmax, std::abs(c.u) + std::abs(c.w) + std::sqrt(m_params.gravity * d));
    }
    const int n = std::clamp(static_cast<int>(std::ceil(cmax * dt / (m_params.cflFraction * m_h))), 1, m_params.maxSubsteps);
    const float ds = dt / static_cast<float>(n);
    for (size_t i = 0; i < m_cols.size(); ++i) m_etaPrev[i] = m_cols[i].eta;
    r.exchanged += m_pendingExchange; m_pendingExchange = 0.0;
    for (int s = 0; s < n; ++s) substep(ds, r);
    r.substeps = n;
    r.mass = totalMass();
    float mx = 0.0f, de = 0.0f;
    for (size_t i = 0; i < m_cols.size(); ++i) {
        mx = std::max(mx, std::max(std::abs(m_cols[i].u), std::abs(m_cols[i].w)));
        de = std::max(de, static_cast<float>(std::abs(m_cols[i].eta - m_etaPrev[i])));
    }
    r.maxSpeed = mx;
    r.maxEtaChange = de;
    return r;
}

void ShoreSolver::rates(const std::vector<double>& eta, const std::vector<float>& u, const std::vector<float>& w,
                        std::vector<double>& dEta, std::vector<double>& dQx, std::vector<double>& dQz) const {
    const double g = m_params.gravity;
    const double h = m_h;
    const double dry = m_params.dryDepth;
    std::fill(dEta.begin(), dEta.end(), 0.0);
    std::fill(dQx.begin(), dQx.end(), 0.0);
    std::fill(dQz.begin(), dQz.end(), 0.0);

    // One direction at a time (unsplit: both accumulate into the same rates). `dir` 0 = x, 1 = z;
    // the normal velocity is u for x and w for z, the transverse the other.
    for (int dir = 0; dir < 2; ++dir) {
        const int nAlong = dir == 0 ? m_nx : m_nz, nLines = dir == 0 ? m_nz : m_nx;
        std::vector<double>& dQn = dir == 0 ? dQx : dQz;
        std::vector<double>& dQt = dir == 0 ? dQz : dQx;
        auto cellIdx = [&](int line, int k) { return dir == 0 ? idx(k, line) : idx(line, k); };
        auto blocked = [&](int line, int k) { return k < 0 || k >= nAlong || m_cols[cellIdx(line, k)].wall; };
        std::vector<Side> plus(nAlong), minus(nAlong);   // reconstruction per cell on its +/- face
        std::vector<char> wetc(nAlong);
        for (int line = 0; line < nLines; ++line) {
            // reconstruct each cell: minmod slopes on eta, d, u_n, u_t; neighbours mirrored at walls
            for (int k = 0; k < nAlong; ++k) {
                const size_t i = cellIdx(line, k);
                const ShoreColumn& c = m_cols[i];
                const double etaC = eta[i], dC = std::max(eta[i] - c.bed, 0.0);
                const double unC = dir == 0 ? u[i] : w[i], utC = dir == 0 ? w[i] : u[i];
                wetc[k] = !c.wall && dC > dry;
                double sEta = 0.0, sD = 0.0, sUn = 0.0, sUt = 0.0;
                if (wetc[k]) {
                    auto nb = [&](int kk, double& e, double& d, double& un, double& ut) {
                        if (blocked(line, kk)) { e = etaC; d = dC; un = -unC; ut = utC; return; }
                        const size_t j = cellIdx(line, kk);
                        e = eta[j]; d = std::max(eta[j] - m_cols[j].bed, 0.0);
                        un = dir == 0 ? u[j] : w[j]; ut = dir == 0 ? w[j] : u[j];
                    };
                    double eM, dM, unM, utM, eP, dP, unP, utP;
                    nb(k - 1, eM, dM, unM, utM); nb(k + 1, eP, dP, unP, utP);
                    sEta = minmod(etaC - eM, eP - etaC);
                    sD = minmod(dC - dM, dP - dC);
                    sUn = minmod(unC - unM, unP - unC);
                    sUt = minmod(utC - utM, utP - utC);
                    if (dC - 0.5 * std::abs(sD) < 0.0) sD = 0.0;   // the reconstruction stays non-negative
                }
                const double unUse = wetc[k] ? unC : 0.0, utUse = wetc[k] ? utC : 0.0;
                plus[k]  = Side{etaC + 0.5 * sEta, dC + 0.5 * sD, 0.0, unUse + 0.5 * sUn, utUse + 0.5 * sUt};
                minus[k] = Side{etaC - 0.5 * sEta, dC - 0.5 * sD, 0.0, unUse - 0.5 * sUn, utUse - 0.5 * sUt};
                plus[k].b = plus[k].eta - plus[k].d;
                minus[k].b = minus[k].eta - minus[k].d;
                // in-cell bed slope source (the second-order balance term)
                dQn[i] += -g * 0.5 * (minus[k].d + plus[k].d) * (plus[k].b - minus[k].b) / h;
            }
            // faces k+1/2 for k = -1 .. nAlong-1 (the two domain edges reflect)
            for (int k = -1; k < nAlong; ++k) {
                const bool bl = blocked(line, k), br = blocked(line, k + 1);
                if (bl && br) continue;
                const Side L = bl ? Side{minus[k + 1].eta, minus[k + 1].d, minus[k + 1].b, -minus[k + 1].u, minus[k + 1].v} : plus[k];
                const Side R = br ? Side{plus[k].eta, plus[k].d, plus[k].b, -plus[k].u, plus[k].v} : minus[k + 1];
                // hydrostatic reconstruction: both sides stand on the higher bed
                const double bStar = std::max(L.b, R.b);
                const double dLs = std::max(L.eta - bStar, 0.0), dRs = std::max(R.eta - bStar, 0.0);
                const Flux f = hll(State{dLs, L.u, L.v}, State{dRs, R.u, R.v}, g);
                if (!bl) {
                    const size_t i = cellIdx(line, k);
                    dEta[i] -= f.m / h;
                    dQn[i] -= (f.p + 0.5 * g * (L.d * L.d - dLs * dLs)) / h;
                    dQt[i] -= f.t / h;
                }
                if (!br) {
                    const size_t j = cellIdx(line, k + 1);
                    dEta[j] += f.m / h;
                    dQn[j] += (f.p + 0.5 * g * (R.d * R.d - dRs * dRs)) / h;
                    dQt[j] += f.t / h;
                }
            }
        }
    }
}

void ShoreSolver::substep(float dt, ShoreStepReport& r) {
    const size_t n = m_cols.size();
    const double dry = m_params.dryDepth;
    const double g = m_params.gravity;
    for (size_t i = 0; i < n; ++i) { m_eta0[i] = m_cols[i].eta; m_u0[i] = m_cols[i].u; m_w0[i] = m_cols[i].w; }

    // Heun (RK2): stage 1 into (eta1, u1, w1), stage 2 from it, the two averaged in (d, q)
    rates(m_eta0, m_u0, m_w0, m_dEta, m_dQx, m_dQz);
    for (size_t i = 0; i < n; ++i) {
        const ShoreColumn& c = m_cols[i];
        if (c.wall) { m_eta1[i] = m_eta0[i]; m_u1[i] = 0.0f; m_w1[i] = 0.0f; continue; }
        const double d0 = std::max(m_eta0[i] - c.bed, 0.0);
        double d1 = d0 + dt * m_dEta[i];
        if (d1 < 0.0) { r.clamped += -d1 * m_h * m_h; d1 = 0.0; }
        const double qx = d0 * m_u0[i] + dt * m_dQx[i], qz = d0 * m_w0[i] + dt * m_dQz[i];
        m_eta1[i] = c.bed + d1;
        if (d1 > dry) { m_u1[i] = static_cast<float>(qx / d1); m_w1[i] = static_cast<float>(qz / d1); }
        else { m_u1[i] = 0.0f; m_w1[i] = 0.0f; }
    }
    rates(m_eta1, m_u1, m_w1, m_dEta, m_dQx, m_dQz);
    for (size_t i = 0; i < n; ++i) {
        ShoreColumn& c = m_cols[i];
        if (c.wall) { c.u = 0.0f; c.w = 0.0f; continue; }
        const double dA = std::max(m_eta0[i] - c.bed, 0.0);
        const double dB = std::max(m_eta1[i] - c.bed, 0.0);
        double dC = dB + dt * m_dEta[i];
        if (dC < 0.0) { r.clamped += -dC * m_h * m_h * 0.5; dC = 0.0; }
        const double qxC = dB * m_u1[i] + dt * m_dQx[i], qzC = dB * m_w1[i] + dt * m_dQz[i];
        const double d = 0.5 * (dA + dC);
        const double qx = 0.5 * (dA * m_u0[i] + qxC), qz = 0.5 * (dA * m_w0[i] + qzC);
        // Manning friction, semi-implicit: q /= 1 + dt g n^2 |V| / d^(4/3)
        if (d > dry) {
            double uu = qx / d, ww = qz / d;
            if (m_params.manningN > 0.0f) {
                const double speed = std::sqrt(uu * uu + ww * ww);
                const double cf = g * m_params.manningN * m_params.manningN * speed / std::pow(d, 4.0 / 3.0);
                uu /= (1.0 + dt * cf); ww /= (1.0 + dt * cf);
            }
            c.u = static_cast<float>(uu); c.w = static_cast<float>(ww);
        } else { c.u = 0.0f; c.w = 0.0f; }
        c.eta = c.bed + d;
    }
    // the prescribed columns are the ocean: reset to the prescription; the difference is the
    // ocean's supply (the flux ledger closes: fluxes are conservative, resets are counted)
    double exch = 0.0;
    for (size_t i = 0; i < n; ++i) {
        ShoreColumn& c = m_cols[i];
        if (!c.prescribed) continue;
        const double target = std::max(m_etaPrev[i], static_cast<double>(c.bed));   // what prescribe() set before this tick
        exch += (target - c.eta) * m_h * m_h;
        c.eta = target;
        c.u = m_prescribedVel[i].x; c.w = m_prescribedVel[i].y;
    }
    r.exchanged += exch;
    // breaking marker: a steep front (surface slope > 0.3) in shallow water (d < 1.5 m) is a bore;
    // decays per substep (foam for G2)
    for (int z = 0; z < m_nz; ++z) for (int x = 0; x < m_nx; ++x) {
        ShoreColumn& c = col(x, z);
        c.foam *= std::max(0.0f, 1.0f - 1.5f * dt);
        const float d = static_cast<float>(c.eta - c.bed);
        if (c.wall || d <= dry) { c.foam = 0.0f; continue; }
        auto wetN = [&](int xx, int zz) { return !col(xx, zz).wall && col(xx, zz).eta - col(xx, zz).bed > dry; };
        float slope = 0.0f;
        auto rise = [&](int xx, int zz) { return static_cast<float>(std::abs(c.eta - col(xx, zz).eta)) / m_h; };
        if (x > 0 && wetN(x - 1, z)) slope = std::max(slope, rise(x - 1, z));
        if (x + 1 < m_nx && wetN(x + 1, z)) slope = std::max(slope, rise(x + 1, z));
        if (z > 0 && wetN(x, z - 1)) slope = std::max(slope, rise(x, z - 1));
        if (z + 1 < m_nz && wetN(x, z + 1)) slope = std::max(slope, rise(x, z + 1));
        if (slope > 0.3f && d < 1.5f) c.foam = std::min(1.0f, c.foam + 2.0f * dt * (slope - 0.3f) / 0.3f);
    }
}

}  // namespace Phyxel::Core::Water
