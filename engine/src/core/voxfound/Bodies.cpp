// docs/VoxelFoundationPlan.md §6.5 + §6.7 -- bodies (F5). See Bodies.h; voxfound_f51_prediction.txt.
#include "core/voxfound/Bodies.h"

#include <algorithm>
#include <limits>

namespace Phyxel::Core::VoxFound {

namespace {
constexpr uint32_t kNone = std::numeric_limits<uint32_t>::max();
}

Extraction extractBodies(VoxelGrid& world, const FractureResult& r, const Hit& hit,
                         const std::vector<FractureMaterial>& mats) {
    Extraction ex;
    ex.implemented = true;
    const size_t n = world.size();
    const size_t np = r.pieceKind.size();
    const double vm = world.voxelM;
    const double v3 = vm * vm * vm;

    // pass 1: bounding box per Body piece
    std::vector<glm::ivec3> lo(np, glm::ivec3(std::numeric_limits<int>::max()));
    std::vector<glm::ivec3> hi(np, glm::ivec3(std::numeric_limits<int>::min()));
    for (size_t i = 0; i < n; ++i) {
        const uint32_t id = r.piece[i];
        if (!world.mat[i] || !id || r.pieceKind[id] != PieceKind::Body) continue;
        const glm::ivec3 p = world.pos(i);
        lo[id] = glm::min(lo[id], p);
        hi[id] = glm::max(hi[id], p);
    }
    std::vector<uint32_t> bodyOf(np, kNone);
    for (size_t id = 1; id < np; ++id) {
        if (r.pieceKind[id] != PieceKind::Body) continue;
        bodyOf[id] = uint32_t(ex.bodies.size());
        Body b;
        b.pieceId = uint32_t(id);
        b.origin = lo[id];
        b.grid.resize(hi[id] - lo[id] + 1);
        b.grid.voxelM = world.voxelM;
        b.grid.damage.assign(b.grid.size(), 0);
        ex.bodies.push_back(std::move(b));
    }

    // per body moments in body-local voxel units (centres at j + 0.5): mass, first, second moments
    const size_t nb = ex.bodies.size();
    std::vector<double> M(nb, 0.0);
    std::vector<glm::dvec3> S(nb, glm::dvec3(0.0));
    std::vector<glm::dmat3> Q(nb, glm::dmat3(0.0));
    std::vector<double> best(nb, std::numeric_limits<double>::infinity());
    const glm::dvec3 hp(hit.point);

    // pass 2: move voxels out
    world.damage = r.damage;
    if (world.damage.size() != n) world.damage.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t id = r.piece[i];
        if (!world.mat[i] || !id) continue;
        const PieceKind k = r.pieceKind[id];
        if (k == PieceKind::Anchored) continue;
        const glm::ivec3 p = world.pos(i);
        if (k == PieceKind::Body) {
            const uint32_t bi = bodyOf[id];
            Body& b = ex.bodies[bi];
            const glm::ivec3 l = p - b.origin;
            const size_t j = b.grid.idx(l);
            b.grid.mat[j] = world.mat[i];
            b.grid.part[j] = world.part[i];
            b.grid.damage[j] = r.damage[i];
            ++b.voxels;
            const double m = double(mats[world.mat[i]].density) * v3;
            const glm::dvec3 c = glm::dvec3(l) + 0.5;
            M[bi] += m;
            S[bi] += m * c;
            Q[bi] += m * glm::outerProduct(c, c);
            const glm::dvec3 d = glm::dvec3(p) + 0.5 - hp;
            const double d2 = glm::dot(d, d);
            if (d2 < best[bi]) {   // ascending index: ties keep the lowest
                best[bi] = d2;
                b.contact = p;
            }
        } else {
            ex.crumbs.push_back({p, world.mat[i], world.part[i], r.damage[i]});
        }
        world.mat[i] = 0;
        world.part[i] = 0;
        world.fixed[i] = 0;
        world.damage[i] = 0;
    }

    // mass properties; the hit's momentum shared with the stress falloff
    const double L = FractureModel::kStressLengthM;
    std::vector<double> w(nb, 0.0);
    double wsum = 0.0;
    for (size_t bi = 0; bi < nb; ++bi) {
        Body& b = ex.bodies[bi];
        b.mass = M[bi];
        const glm::dvec3 cl = S[bi] / M[bi];
        b.com = glm::dvec3(b.origin) + cl;
        // about the centre, metres: sum m s^2 / 6 Id + (tr(Q') Id - Q'), Q' = Q - M c c^T
        const glm::dmat3 Qc = (Q[bi] - M[bi] * glm::outerProduct(cl, cl)) * (vm * vm);
        const double tr = Qc[0][0] + Qc[1][1] + Qc[2][2];
        b.inertia = glm::dmat3(M[bi] * vm * vm / 6.0 + tr) - Qc;
        const double dm = std::sqrt(best[bi]) * vm / L;
        w[bi] = b.mass / (1.0 + dm * dm);
        wsum += w[bi];
    }
    const glm::dvec3 P = double(hit.impulse) * glm::dvec3(hit.dir);
    for (size_t bi = 0; bi < nb && wsum > 0.0; ++bi) {
        Body& b = ex.bodies[bi];
        const glm::dvec3 dp = P * (w[bi] / wsum);
        b.vel = dp / b.mass;
        const glm::dvec3 arm = (glm::dvec3(b.contact) + 0.5 - b.com) * vm;
        b.angVel = glm::inverse(b.inertia) * glm::cross(arm, dp);
    }
    return ex;
}

void bodyMassBruteForce(const Body& b, const std::vector<FractureMaterial>& mats, double& mass, glm::dvec3& com,
                        glm::dmat3& inertia) {
    // two passes, per voxel cube, world voxel units then metres (a different method from extractBodies' moments)
    const double vm = b.grid.voxelM;
    mass = 0.0;
    com = glm::dvec3(0.0);
    inertia = glm::dmat3(0.0);
    for (size_t j = 0; j < b.grid.size(); ++j) {
        if (!b.grid.mat[j]) continue;
        const double m = double(mats[b.grid.mat[j]].density) * vm * vm * vm;
        mass += m;
        com += m * (glm::dvec3(b.origin + b.grid.pos(j)) + 0.5);
    }
    if (mass <= 0.0) return;
    com /= mass;
    for (size_t j = 0; j < b.grid.size(); ++j) {
        if (!b.grid.mat[j]) continue;
        const double m = double(mats[b.grid.mat[j]].density) * vm * vm * vm;
        const glm::dvec3 d = (glm::dvec3(b.origin + b.grid.pos(j)) + 0.5 - com) * vm;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                double v = -m * d[r] * d[c];
                if (r == c) v += m * (glm::dot(d, d) + vm * vm / 6.0);
                inertia[c][r] += v;
            }
    }
}

}  // namespace Phyxel::Core::VoxFound
