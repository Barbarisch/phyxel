#include "core/water/ColumnWater.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>

namespace Phyxel::Core::Water {

const char* columnBedRuleName(ColumnBedRule r) {
    switch (r) {
        case ColumnBedRule::Voxel: return "voxel";
        case ColumnBedRule::Hydraulic: return "hydraulic";
        case ColumnBedRule::Plane: return "plane";
    }
    return "voxel";
}

bool parseColumnBedRule(const std::string& s, ColumnBedRule& out) {
    if (s == "voxel") { out = ColumnBedRule::Voxel; return true; }
    if (s == "hydraulic") { out = ColumnBedRule::Hydraulic; return true; }
    if (s == "plane") { out = ColumnBedRule::Plane; return true; }
    return false;
}

std::vector<float> hydraulicBed(const std::vector<float>& tops, const std::vector<uint8_t>& walls, int nx, int nz,
                                const std::vector<uint8_t>* exits) {
    // Runs = 4-connected columns of equal top (walls excluded). Within a run, a two-source distance field (in columns):
    // d_up to the run's cells that border a HIGHER non-wall column (the step above - its top carried along), d_down to
    // the cells that border a LOWER one. bed = top + (top_up - top) * d_down / (d_up + d_down): the step above's top at
    // the upstream edge, the run's own top at its downstream edge, a straight ramp between. A run without both (a
    // pool with no lower exit, the head of a channel) keeps its top. Pure function of the tops: identical wherever the
    // run sits (C-T7). Walls are banks, never "the step above" - else every run would ramp toward its banks.
    const int n = nx * nz;
    std::vector<float> bed(tops);
    constexpr float kEq = 1e-4f;
    auto idx = [nx](int x, int z) { return x + nx * z; };
    const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    std::vector<int> dUp(n, -1), dDown(n, -1);
    std::vector<float> topUp(n, 0.0f);
    std::deque<int> qUp, qDown;
    for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
        const int i = idx(x, z);
        if (walls[i]) continue;
        float higher = 1e30f; bool lower = false;
        for (int k = 0; k < 4; ++k) {
            const int xx = x + dx[k], zz = z + dz[k];
            if (xx < 0 || zz < 0 || xx >= nx || zz >= nz) continue;
            const int j = idx(xx, zz);
            if (walls[j]) continue;
            if (tops[j] > tops[i] + kEq) higher = std::min(higher, tops[j]);
            else if (tops[j] < tops[i] - kEq) lower = true;
        }
        if (exits && (*exits)[i]) lower = true;   // water leaves the region here: a lower exit
        if (higher < 1e29f) { dUp[i] = 0; topUp[i] = higher; qUp.push_back(i); }
        if (lower) { dDown[i] = 0; qDown.push_back(i); }
    }
    auto spread = [&](std::deque<int>& q, std::vector<int>& d, bool carryTop) {
        while (!q.empty()) {
            const int i = q.front(); q.pop_front();
            const int x = i % nx, z = i / nx;
            for (int k = 0; k < 4; ++k) {
                const int xx = x + dx[k], zz = z + dz[k];
                if (xx < 0 || zz < 0 || xx >= nx || zz >= nz) continue;
                const int j = idx(xx, zz);
                if (walls[j] || d[j] >= 0 || std::abs(tops[j] - tops[i]) > kEq) continue;   // stay inside the run
                d[j] = d[i] + 1;
                if (carryTop) topUp[j] = topUp[i];
                q.push_back(j);
            }
        }
    };
    spread(qUp, dUp, true);
    spread(qDown, dDown, false);
    for (int i = 0; i < n; ++i) {
        if (walls[i] || dUp[i] < 0 || dDown[i] < 0) continue;
        const float t = static_cast<float>(dDown[i]) / static_cast<float>(dUp[i] + dDown[i]);
        bed[i] = tops[i] + (topUp[i] - tops[i]) * t;
    }
    return bed;
}

ColumnWater::ColumnWater(int id, const glm::ivec3& minVoxel, const glm::ivec3& maxVoxel, ShoreParams params)
    : m_id(id), m_min(glm::min(minVoxel, maxVoxel)), m_max(glm::max(minVoxel, maxVoxel)),
      m_solver(glm::vec2(static_cast<float>(glm::min(minVoxel, maxVoxel).x), static_cast<float>(glm::min(minVoxel, maxVoxel).z)),
               (glm::abs(maxVoxel.x - minVoxel.x) + 1) * 3, (glm::abs(maxVoxel.z - minVoxel.z) + 1) * 3, kH, params) {
    const int n = m_solver.nx() * m_solver.nz();
    m_tops.assign(n, static_cast<float>(m_min.y));
    m_walls.assign(n, 0);
    for (int z = 0; z < m_solver.nz(); ++z) for (int x = 0; x < m_solver.nx(); ++x) {
        ShoreColumn& c = m_solver.col(x, z);
        c.bed = static_cast<float>(m_min.y); c.eta = c.bed;
    }
}

glm::vec2 ColumnWater::columnCentre(int x, int z) const {
    return m_solver.origin() + glm::vec2((x + 0.5f) * kH, (z + 0.5f) * kH);
}

bool ColumnWater::columnAt(float wx, float wz, int& x, int& z) const {
    x = static_cast<int>(std::floor((wx - m_solver.origin().x) / kH));
    z = static_cast<int>(std::floor((wz - m_solver.origin().y) / kH));
    return x >= 0 && z >= 0 && x < m_solver.nx() && z < m_solver.nz();
}

void ColumnWater::setSolids(const std::vector<float>& tops, const std::vector<uint8_t>& walls) {
    const size_t n = static_cast<size_t>(m_solver.nx()) * m_solver.nz();
    if (tops.size() != n || walls.size() != n) return;
    m_tops = tops;
    // a column turned solid loses its water to the solid that took its place: counted out, never silently dropped
    for (size_t i = 0; i < n; ++i) {
        ShoreColumn& c = m_solver.col(static_cast<int>(i % m_solver.nx()), static_cast<int>(i / m_solver.nx()));
        if (walls[i] && !c.wall) { const double d = c.eta - c.bed; if (d > 0.0) m_out += d * kH * kH; }
    }
    m_walls = walls;
    applyBeds();
}

void ColumnWater::setBedRule(ColumnBedRule r) { m_rule = r; applyBeds(); }

void ColumnWater::applyBeds() {
    const int nx = m_solver.nx(), nz = m_solver.nz();
    std::vector<float> beds;
    if (m_rule == ColumnBedRule::Hydraulic) {
        std::vector<uint8_t> exits(m_tops.size(), 0);
        for (int i : m_outfalls) exits[static_cast<size_t>(i)] = 1;
        beds = hydraulicBed(m_tops, m_walls, nx, nz, &exits);
    }
    else beds = m_tops;
    if (m_rule == ColumnBedRule::Plane) {
        const float a = m_planeDir * 3.14159265f / 180.0f;
        const glm::vec2 dir(std::cos(a), std::sin(a));
        for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x)
            beds[x + nx * z] = m_planeY0 - m_planeSlope * glm::dot(columnCentre(x, z), dir);
    }
    const float boxTop = static_cast<float>(m_max.y + 1);
    for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
        const int i = x + nx * z;
        ShoreColumn& c = m_solver.col(x, z);
        if (m_walls[i]) { c.wall = 1; c.bed = boxTop; c.eta = boxTop; c.u = c.w = 0.0f; continue; }
        const double depth = c.wall ? 0.0 : std::max(0.0, c.eta - static_cast<double>(c.bed));   // a column keeps its water's depth
        c.wall = 0;
        c.bed = beds[i];
        c.eta = c.bed + depth;
    }
    m_base = m_solver.totalMass() - (m_in - m_out);
}

void ColumnWater::addSpring(int x, int z, double rate) {
    if (x < 0 || z < 0 || x >= m_solver.nx() || z >= m_solver.nz()) return;
    m_springs.push_back({x, z, rate});
}

void ColumnWater::addOutfall(int x, int z) {
    if (x < 0 || z < 0 || x >= m_solver.nx() || z >= m_solver.nz()) return;
    m_outfalls.push_back(static_cast<int>(m_solver.idx(x, z)));
    if (m_rule == ColumnBedRule::Hydraulic) applyBeds();   // an outfall is a lower exit for the ramp
}

void ColumnWater::step(float dt) {
    if (dt <= 0.0f) return;
    const auto t0 = std::chrono::steady_clock::now();
    for (const Spring& s : m_springs) {
        ShoreColumn& c = m_solver.col(s.x, s.z);
        if (c.wall) continue;
        c.eta += s.rate * dt / (kH * kH);
        m_in += s.rate * dt;
    }
    m_solver.step(dt);
    for (int i : m_outfalls) {
        ShoreColumn& c = m_solver.col(i % m_solver.nx(), i / m_solver.nx());
        const double d = c.eta - c.bed;
        if (d > 0.0) { m_out += d * kH * kH; c.eta = c.bed; c.u = c.w = 0.0f; }
    }
    m_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int ColumnWater::wetColumns() const {
    int n = 0;
    const float dry = m_solver.params().dryDepth;
    for (int z = 0; z < m_solver.nz(); ++z) for (int x = 0; x < m_solver.nx(); ++x)
        if (!m_solver.col(x, z).wall && m_solver.depth(x, z) > dry) ++n;
    return n;
}

void ColumnWater::publish(WaterSurfaceField& out) const {
    const int nx = m_solver.nx(), nz = m_solver.nz();
    out.origin = glm::ivec3(m_min.x * 3, m_min.y * 3, m_min.z * 3);
    out.nx = nx; out.nz = nz; out.h = kH;
    out.cols.assign(static_cast<size_t>(nx) * nz, SurfaceColumn{});
    out.rippleLayer = -1;
    const float dry = m_solver.params().dryDepth;
    for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
        const int i = x + nx * z;
        const ShoreColumn& c = m_solver.col(x, z);
        SurfaceColumn& sc = out.cols[i];
        sc.solidTopY = m_walls[i] ? static_cast<float>(m_max.y + 1) : m_tops[i];
        if (c.wall || c.eta - c.bed <= dry) continue;
        sc.runs = 1.0f;
        // the water body reaches down to the DRAWN ground: under a hydraulic or plane bed above the voxels the water
        // fills the step's corner, so the drawn run spans the voxel top to the surface (no gap of air under water)
        sc.bottom[0] = std::min(c.bed, m_tops[i]);
        sc.top[0] = static_cast<float>(c.eta);
        sc.u = c.u; sc.w = c.w; sc.foam = c.foam;
    }
}

}  // namespace Phyxel::Core::Water
