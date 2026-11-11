// docs/VoxelFoundationPlan.md §6 + §8 — fracture lab (F0 skeleton). See FractureLab.h.

#include "core/voxfound/FractureLab.h"

#include <deque>
#include <unordered_map>

namespace Phyxel::Core::VoxFound {

FractureResult fractureReference(const VoxelGrid& /*grid*/, const Hit& /*hit*/,
                                 const std::vector<FractureMaterial>& /*mats*/) {
    // F4 builds this. Until then every fracture-lab prediction is red.
    return FractureResult{};
}

PieceLabels labelPieces(const VoxelGrid& g, const std::vector<uint8_t>& cut) {
    PieceLabels L;
    const size_t n = g.size();
    L.label.assign(n, 0);
    L.size.assign(1, 0);
    L.anchored.assign(1, 0);
    L.centroid.assign(1, glm::vec3(0.0f));
    const bool haveCut = cut.size() == n;
    std::deque<size_t> q;
    for (size_t s = 0; s < n; ++s) {
        if (g.mat[s] == 0 || L.label[s] != 0) continue;
        const uint32_t id = uint32_t(L.size.size());
        L.size.push_back(0);
        L.anchored.push_back(0);
        glm::dvec3 sum(0.0);
        L.label[s] = id;
        q.push_back(s);
        while (!q.empty()) {
            const size_t i = q.front();
            q.pop_front();
            const glm::ivec3 p = g.pos(i);
            ++L.size[id];
            sum += glm::dvec3(p) + 0.5;
            if (g.fixed[i]) L.anchored[id] = 1;
            static const glm::ivec3 kDir[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            for (int a = 0; a < 3; ++a) {
                for (int sgn = -1; sgn <= 1; sgn += 2) {
                    const glm::ivec3 q3 = p + kDir[a] * sgn;
                    if (!g.in(q3)) continue;
                    const size_t j = g.idx(q3);
                    if (g.mat[j] == 0 || L.label[j] != 0) continue;
                    // the bond lives on the lower voxel of the pair
                    const size_t owner = sgn > 0 ? i : j;
                    if (haveCut && (cut[owner] & uint8_t(1u << a))) continue;
                    L.label[j] = id;
                    q.push_back(j);
                }
            }
        }
        L.centroid.push_back(glm::vec3(sum / double(L.size[id])));
    }
    return L;
}

CutStats cutStats(const VoxelGrid& g, const std::vector<uint8_t>& cut) {
    CutStats s;
    if (cut.size() != g.size()) return s;
    static const glm::ivec3 kDir[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (size_t i = 0; i < g.size(); ++i) {
        if (!cut[i] || g.mat[i] == 0) continue;
        const glm::ivec3 p = g.pos(i);
        for (int a = 0; a < 3; ++a) {
            if (!(cut[i] & uint8_t(1u << a))) continue;
            const glm::ivec3 q = p + kDir[a];
            if (!g.in(q) || g.mat[g.idx(q)] == 0) continue;  // not a bond
            ++s.cut;
            const uint16_t pa = g.part[i], pb = g.part[g.idx(q)];
            if (pa != 0 && pb != 0 && pa != pb) ++s.betweenParts; else ++s.withinPart;
            (a == 0 ? s.alongX : a == 1 ? s.alongY : s.alongZ)++;
        }
    }
    return s;
}

bool resultMatchesChecker(const VoxelGrid& g, const FractureResult& r, std::string* why) {
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    if (!r.implemented) return fail("fractureReference not implemented (F4)");
    if (r.cut.size() != g.size() || r.piece.size() != g.size()) return fail("result vectors wrong size");
    const PieceLabels L = labelPieces(g, r.cut);
    std::unordered_map<uint32_t, uint32_t> fwd, back;
    for (size_t i = 0; i < g.size(); ++i) {
        const uint32_t a = r.piece[i], b = L.label[i];
        if ((a == 0) != (b == 0)) return fail("air/solid mismatch at voxel " + std::to_string(i));
        if (a == 0) continue;
        auto f = fwd.emplace(a, b).first;
        auto k = back.emplace(b, a).first;
        if (f->second != b || k->second != a) return fail("piece partition differs at voxel " + std::to_string(i));
    }
    for (auto [a, b] : fwd) {
        if (a >= r.pieceAnchored.size()) return fail("pieceAnchored too short");
        if ((r.pieceAnchored[a] != 0) != (L.anchored[b] != 0)) return fail("anchoring differs for piece " + std::to_string(a));
    }
    return true;
}

// ---- rigs ------------------------------------------------------------------------------------

std::vector<FractureMaterial> labMaterials() {
    std::vector<FractureMaterial> m(6);
    m[kAir] = {"air"};
    m[kStone] = {"Stone", FractureClass::Brittle, 10.0f, glm::vec3(0.0f), 0.3f};
    m[kBricks] = {"Bricks", FractureClass::Brittle, 6.0f, glm::vec3(0.0f), 0.3f};
    m[kWood] = {"Wood", FractureClass::Fibrous, 4.0f, glm::vec3(1, 0, 0), 0.3f};
    m[kGlass] = {"Glass", FractureClass::Glass, 1.0f, glm::vec3(0.0f), 0.3f};
    m[kWoodItem] = {"WoodItem", FractureClass::Fibrous, 4.0f, glm::vec3(0, 1, 0), 0.3f};
    return m;
}

namespace {
Rig base(const std::string& name, glm::ivec3 dim, float voxelM) {
    Rig r;
    r.name = name;
    r.voxelM = voxelM;
    r.grid.resize(dim);
    r.mats = labMaterials();
    return r;
}
}  // namespace

Rig rigStoneWall(glm::vec3 hitPoint, float e) {
    Rig r = base("stone_wall", {24, 16, 3}, 0.125f);
    for (int x = 0; x < 24; ++x)
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 3; ++z) r.grid.set({x, y, z}, kStone, 0, y == 0);
    r.hit = {hitPoint, {0, 0, 1}, e * r.mats[kStone].toughness, 101};
    r.prediction = "loose voxels never decrease as energy rises; more loose pieces and a smaller mean loose piece at "
                   "high energy than at low; an edge hit loosens at least as much as a centre hit at the same energy";
    return r;
}

Rig rigBrickWall(float e) {
    Rig r = base("brick_wall", {32, 16, 2}, 0.125f);
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 2; ++z) {
                const int course = y / 2;
                const int offset = (course % 2) * 2;          // running bond: half-brick offset
                const int bx = (x + offset) / 4;
                r.grid.set({x, y, z}, kBricks, uint16_t(1 + course * 100 + bx), y == 0);
            }
    r.hit = {{16, 8, 0}, {0, 0, 1}, e * r.mats[kBricks].toughness, 202};
    r.prediction = "breaks along mortar: > 80 % of cut bonds lie between parts (bricks)";
    return r;
}

Rig rigBeam(float e) {
    Rig r = base("beam", {32, 2, 2}, 0.125f);
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z) r.grid.set({x, y, z}, kWood, 1, x < 2 || x >= 30);
    r.hit = {{16, 2, 1}, {0, -1, 0}, e * r.mats[kWood].toughness, 303};
    r.prediction = "splits along the grain: cut bonds in y and z (cracks running along x) are > 3x the cut bonds in x";
    return r;
}

Rig rigGlassPane(float e) {
    Rig r = base("glass_pane", {24, 24, 1}, 0.125f);
    for (int x = 0; x < 24; ++x)
        for (int y = 0; y < 24; ++y)
            r.grid.set({x, y, 0}, kGlass, 0, x == 0 || y == 0 || x == 23 || y == 23);
    r.hit = {{12, 12, 0}, {0, 0, 1}, e * r.mats[kGlass].toughness, 404};
    r.prediction = "radial + concentric shards: >= 6 loose shards whose centroids cover >= 6 of 8 angular sectors "
                   "around the hit";
    return r;
}

Rig rigColumnLintel(float e) {
    Rig r = base("column_lintel", {16, 14, 2}, 0.125f);
    for (int z = 0; z < 2; ++z) {
        for (int y = 0; y < 12; ++y)
            for (int x = 7; x <= 8; ++x) r.grid.set({x, y, z}, kStone, 1, y == 0);   // column
        for (int y = 12; y < 14; ++y)
            for (int x = 0; x < 16; ++x) r.grid.set({x, y, z}, kStone, 2, false);    // lintel
    }
    r.hit = {{8, 6, 0}, {0, 0, 1}, e * r.mats[kStone].toughness, 505};
    r.prediction = "a hit that severs the column leaves every lintel voxel in a loose piece; the column's base stays "
                   "anchored";
    return r;
}

Rig rigChair(float e) {
    // item voxels, 1/64 m: seat 24x3x24 (37.5 cm) at y 24..26, legs 3x24x3, back 24x24x3 at z 21..23
    Rig r = base("chair", {24, 51, 24}, 1.0f / 64.0f);
    auto box = [&](glm::ivec3 lo, glm::ivec3 hi, uint16_t part) {
        for (int x = lo.x; x <= hi.x; ++x)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int z = lo.z; z <= hi.z; ++z) r.grid.set({x, y, z}, kWoodItem, part, false);
    };
    box({0, 0, 0}, {2, 23, 2}, 2);
    box({21, 0, 0}, {23, 23, 2}, 3);
    box({0, 0, 21}, {2, 23, 23}, 4);
    box({21, 0, 21}, {23, 23, 23}, 5);
    box({0, 24, 0}, {23, 26, 23}, 1);
    box({0, 27, 21}, {23, 50, 23}, 6);
    r.hit = {{0, 12, 1}, {1, 0, 0}, e * r.mats[kWoodItem].toughness, 606};
    r.prediction = "breaks at its joints: > 80 % of cut bonds lie between parts, and >= 2 pieces";
    return r;
}

}  // namespace Phyxel::Core::VoxFound
