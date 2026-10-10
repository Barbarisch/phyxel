#include "core/water/WaterDroplets.h"
#include "core/water/WaterCore.h"
#include <algorithm>
#include <cmath>

namespace Phyxel::Core::Water {

std::vector<DetachedRun> findDetachedRuns(const glm::ivec3& d, const float* f, const float* s, float film) {
    std::vector<DetachedRun> out;
    auto idx = [&](int x, int y, int z) { return static_cast<size_t>(x) + static_cast<size_t>(d.x) * (static_cast<size_t>(y) + static_cast<size_t>(d.y) * z); };
    auto air = [&](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= d.x || y >= d.y || z >= d.z) return false;   // outside the grid is solid
        const size_t i = idx(x, y, z);
        return f[i] + (s ? s[i] : 0.0f) < film;
    };
    for (int z = 0; z < d.z; ++z) for (int x = 0; x < d.x; ++x) {
        for (int y = 1; y < d.y; ++y) {
            if (f[idx(x, y, z)] < film || !air(x, y - 1, z)) continue;
            DetachedRun r; r.bottom = {x, y, z};
            int top = y;
            while (top < d.y && f[idx(x, top, z)] >= film) { r.sumF += f[idx(x, top, z)]; ++top; }
            r.cells = top - y;
            bool iso = top >= d.y || air(x, top, z);
            for (int yy = y; yy < top && iso; ++yy)
                iso = air(x - 1, yy, z) && air(x + 1, yy, z) && air(x, yy, z - 1) && air(x, yy, z + 1);
            r.isolated = iso;
            out.push_back(r);
            y = top;
        }
    }
    return out;
}

namespace {
// Air for the birth rules: open (not solid / unknown), no body, no water.
bool airCell(const WaterGrid& g, int x, int y, int z, float film) {
    if (!g.inBounds(x, y, z)) return false;
    return g.occ(x, y, z) == Occ::Air && g.f(x, y, z) + g.s(x, y, z) < film;
}
// Does the water in (x, y, z) rest on something? Its run's bottom: the grid floor, or a non-air cell below it.
bool supported(const WaterGrid& g, int x, int y, int z, float film) {
    int yb = y;
    while (yb > 0 && g.f(x, yb - 1, z) >= film) --yb;
    return yb == 0 || !airCell(g, x, yb - 1, z, film);
}
glm::vec3 cellVelocity(const WaterGrid& g, int x, int y, int z) {
    return {0.5f * (g.u(x, y, z) + g.u(x + 1, y, z)), 0.5f * (g.v(x, y, z) + g.v(x, y + 1, z)), 0.5f * (g.w(x, y, z) + g.w(x, y, z + 1))};
}
}  // namespace

BirthReport birthDroplets(WaterGrid& g, float dt, float gravity, std::vector<DropletBirth>& out) {
    BirthReport rep;
    const float h = g.h(), film = WaterGrid::kSurfaceMinDepth / h, vc = std::sqrt(2.0f * gravity * h), V = g.cellVolume();
    // Decide every scrap on the PRE-state first (the GPU does it per column in parallel: a scrap is unsupported
    // before and after it is zeroed, so the order cannot matter - doing the CPU in two passes makes that exact).
    struct Scrap { int x, z, y0, y1; };
    std::vector<Scrap> scraps;
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x)
        for (int y = 1; y < g.ny(); ++y) {
            if (g.f(x, y, z) < film || !airCell(g, x, y - 1, z, film)) continue;
            int top = y; float sum = 0.0f; bool ok = true;
            while (top < g.ny() && g.f(x, top, z) >= film) { sum += g.f(x, top, z); if (g.s(x, top, z) > 0.0f) ok = false; ++top; }
            ok = ok && sum < 1.0f && top < g.ny() && g.occ(x, top, z) == Occ::Air && g.s(x, top, z) <= 0.0f;
            for (int yy = y; yy < top && ok; ++yy) {
                const int nb[4][2] = {{x - 1, z}, {x + 1, z}, {x, z - 1}, {x, z + 1}};
                for (const auto& n : nb) {
                    if (!g.inBounds(n[0], yy, n[1]) || g.occ(n[0], yy, n[1]) != Occ::Air || g.s(n[0], yy, n[1]) > 0.0f) { ok = false; break; }
                    if (g.f(n[0], yy, n[1]) >= film && supported(g, n[0], yy, n[1], film)) { ok = false; break; }
                }
            }
            if (ok) scraps.push_back({x, z, y, top});
            y = top;
        }
    std::vector<double> colVol(static_cast<size_t>(g.nx() * g.nz()), 0.0);
    std::vector<glm::dvec3> colPos(colVol.size(), glm::dvec3(0.0)), colVel(colVol.size(), glm::dvec3(0.0));
    auto add = [&](int x, int z, const glm::vec3& p, const glm::vec3& v, double vol) {
        const size_t c = static_cast<size_t>(x + g.nx() * z);
        colVol[c] += vol; colPos[c] += glm::dvec3(p) * vol; colVel[c] += glm::dvec3(v) * vol;
    };
    for (const Scrap& sc : scraps) {
        for (int y = sc.y0; y < sc.y1; ++y) {
            const double vol = static_cast<double>(g.f(sc.x, y, sc.z)) * V;
            add(sc.x, sc.z, g.cellCenterWorld(sc.x, y, sc.z), cellVelocity(g, sc.x, y, sc.z), vol);
            g.f(sc.x, y, sc.z) = 0.0f;
        }
        ++rep.scraps;
    }
    // (2) spray: the top cell of every supported run whose top face carries water up faster than v_c
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x)
        for (int y = 0; y + 1 < g.ny(); ++y) {
            const float ft = g.f(x, y, z);
            if (ft < film || g.f(x, y + 1, z) >= film) continue;                     // not the top of a run
            if (g.s(x, y, z) > 0.0f || !airCell(g, x, y + 1, z, film)) continue;      // a body's own cell, or capped
            if (!supported(g, x, y, z, film)) continue;                               // a scrap (rule 1's)
            const float vUp = g.v(x, y + 1, z);
            if (vUp <= vc) continue;
            const float frac = std::min(ft * (vUp - vc) * dt / h, ft - film);
            if (frac <= 0.0f) continue;
            g.f(x, y, z) = ft - frac;
            glm::vec3 p = g.cellCenterWorld(x, y, z); p.y += 0.5f * h;
            glm::vec3 vel = cellVelocity(g, x, y, z); vel.y = vUp;
            add(x, z, p, vel, static_cast<double>(frac) * V);
            ++rep.sprays;
        }
    for (size_t c = 0; c < colVol.size(); ++c) {
        if (colVol[c] <= 0.0) continue;
        DropletBirth b; b.volume = static_cast<float>(colVol[c]);
        b.pos = glm::vec3(colPos[c] / colVol[c]); b.vel = glm::vec3(colVel[c] / colVol[c]); b.column = static_cast<int>(c);
        out.push_back(b);
        rep.volume += colVol[c];
    }
    return rep;
}

double depositIntoGrid(WaterGrid& g, const glm::vec3& world, double volume) {
    if (volume <= 0.0) return 0.0;
    glm::ivec3 c = g.worldToCell(world);
    c.x = std::clamp(c.x, 0, g.nx() - 1); c.z = std::clamp(c.z, 0, g.nz() - 1); c.y = std::clamp(c.y, 0, g.ny() - 1);
    while (c.y < g.ny() && g.occ(c.x, c.y, c.z) != Occ::Air) ++c.y;   // landed on ground: the first open cell above it
    const double V = g.cellVolume();
    double left = volume / V;   // in cell fractions
    for (int y = c.y; y < g.ny() && left > 0.0; ++y) {
        if (g.occ(c.x, y, c.z) != Occ::Air) break;                      // a ceiling: what is left goes back to the caller
        const double room = std::max(0.0, 1.0 - static_cast<double>(g.s(c.x, y, c.z)) - static_cast<double>(g.f(c.x, y, c.z)));
        const double put = std::min(room, left);
        g.f(c.x, y, c.z) = static_cast<float>(g.f(c.x, y, c.z) + put);
        left -= put;
    }
    return left * V;
}

namespace {
uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
float unit(uint32_t h) { return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu); }   // [0, 1]
}  // namespace

double DropletPool::spawn(const DropletBirth& b, int volumeId, float h, float sizeFraction, uint32_t tick) {
    if (b.volume <= 0.0f) return 0.0;
    // the size knob: below 1/27 of a cell one cell of water is ~20 000 drops (the whole pool); above 1 a drop
    // is bigger than the cell it left
    const float k = std::clamp(sizeFraction, 1.0f / 27.0f, 1.0f);
    const double vd = std::pow(static_cast<double>(k * h), 3.0);
    size_t n = static_cast<size_t>(std::ceil(static_cast<double>(b.volume) / vd - 1e-9));
    n = std::max<size_t>(n, 1);
    const double each = static_cast<double>(b.volume) / static_cast<double>(n);
    const size_t room = m_d.size() < kCap ? kCap - m_d.size() : 0;
    const size_t fit = std::min(n, room);
    const float edge = static_cast<float>(std::cbrt(each));
    const float spread = 0.15f * glm::length(b.vel);
    for (size_t i = 0; i < fit; ++i) {
        const uint32_t hh = hash3(static_cast<uint32_t>(b.column), tick, static_cast<uint32_t>(i / 2));
        Droplet d;
        d.pos = b.pos + glm::vec3((unit(hash3(hh, 1, 0)) - 0.5f) * h, (unit(hash3(hh, 2, 0)) - 0.5f) * 0.5f * h, (unit(hash3(hh, 3, 0)) - 0.5f) * h);
        // zero-sum spread: drops 2j and 2j + 1 get +J and -J (an odd last drop gets none), so the mean is b.vel
        glm::vec3 J(unit(hash3(hh, 4, 0)) - 0.5f, unit(hash3(hh, 5, 0)) - 0.5f, unit(hash3(hh, 6, 0)) - 0.5f);
        const float jl = glm::length(J);
        J = jl > 1e-6f ? J / jl * spread : glm::vec3(0.0f);
        const bool lastOdd = (n % 2 == 1) && i == n - 1;
        d.vel = b.vel + (lastOdd ? glm::vec3(0.0f) : ((i % 2 == 0) ? J : -J));
        d.volume = static_cast<float>(each); d.edge = edge; d.volumeId = volumeId;
        m_d.push_back(d);
    }
    return static_cast<double>(n - fit) * each;
}

std::vector<Droplet> DropletPool::take(int volumeId) {
    std::vector<Droplet> out;
    size_t w = 0;
    for (size_t i = 0; i < m_d.size(); ++i) {
        if (m_d[i].volumeId == volumeId) out.push_back(m_d[i]);
        else m_d[w++] = m_d[i];
    }
    m_d.resize(w);
    return out;
}

void dropletDrawList(const DropletPool& pool, std::vector<glm::vec4>& out) {
    out.clear();
    for (const Droplet& d : pool.droplets()) out.emplace_back(d.pos, d.edge);
}

}  // namespace Phyxel::Core::Water
