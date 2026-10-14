// WRv2 M2 — pure meshing of TemplateLodChain levels (TreeLodMeshRegistry::buildLevelMesh).
// Verifies neighbor-culled quad emission without any Vulkan device.

#include <gtest/gtest.h>

#include "core/PlacedObjectManager.h"   // full InteractionPointDef for VoxelTemplate's vector
#include "core/TemplateLodChain.h"
#include "core/VoxelTemplate.h"
#include "graphics/TreeLodMeshRegistry.h"

using namespace Phyxel;
using namespace Phyxel::Core;
using namespace Phyxel::Graphics;

namespace {
FarMaterialResolver fakeResolver() {
    return [](const std::string& m, int) -> uint16_t { return m == "Leaf" ? 7 : 3; };
}

TemplateLodChain::Level levelOf(std::vector<TemplateLodChain::Cell> cells, int size = 9) {
    TemplateLodChain::Level l;
    l.cellSizeMicros = size;
    l.cells = std::move(cells);
    return l;
}
} // namespace

TEST(TreeLodMeshTest, SingleCellEmitsSixFaces) {
    auto mesh = TreeLodMeshRegistry::buildLevelMesh(
        levelOf({{glm::ivec3(0, 0, 0), "Leaf"}}), fakeResolver());
    EXPECT_EQ(mesh.vertices.size(), 24u);   // 6 faces x 4 verts
    EXPECT_EQ(mesh.indices.size(), 36u);    // 6 faces x 6 indices
    for (const auto& v : mesh.vertices) EXPECT_EQ(farVertexTexIndex(v.packed), 7);
}

TEST(TreeLodMeshTest, AdjacentCellsCullSharedFaces) {
    auto mesh = TreeLodMeshRegistry::buildLevelMesh(
        levelOf({{glm::ivec3(0, 0, 0), "Log"}, {glm::ivec3(0, 1, 0), "Leaf"}}),
        fakeResolver());
    // Two cubes sharing one face: 12 - 2 = 10 faces.
    EXPECT_EQ(mesh.vertices.size(), 40u);
    EXPECT_EQ(mesh.indices.size(), 60u);
}

TEST(TreeLodMeshTest, MeshIsTrunkAnchoredAtOrigin) {
    // The base cell (0,0,0) at cell size 9 (one voxel) spans local -0.5..0.5 on X/Z — the
    // trunk axis sits at the mesh origin so instances place trees by their column center.
    auto mesh = TreeLodMeshRegistry::buildLevelMesh(
        levelOf({{glm::ivec3(0, 0, 0), "Log"}}), fakeResolver());
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f;
    for (const auto& v : mesh.vertices) {
        minX = std::min(minX, v.pos.x);
        maxX = std::max(maxX, v.pos.x);
        minY = std::min(minY, v.pos.y);
    }
    EXPECT_FLOAT_EQ(minX, -0.5f);
    EXPECT_FLOAT_EQ(maxX, 0.5f);
    EXPECT_FLOAT_EQ(minY, 0.0f);   // trunk base sits ON the ground plane
}

TEST(TreeLodMeshTest, AnchorMatchesTheNearStamp) {
    // THE correspondence pin (user: "the lower detail trees dont seem to correspond with high
    // detail trees"). decorateChunk stamps at base = worldPos - maxExtent/2; the far instance
    // sits at the column center (worldX + 0.5). For a template spanning voxels 0..8 on X/Z,
    // stamped voxel v occupies world [worldPos + v - 4, +1] — so the mesh, drawn at the
    // instance position, must put voxel v at local [v - 4.5, v - 3.5].
    VoxelTemplate t;
    t.name = "stamp_parity";
    for (int x = 0; x <= 8; ++x)
        t.cubes.push_back({glm::ivec3(x, 0, 4), "Log"});   // 9-wide row at z=4
    const glm::vec3 anchor = TreeLodMeshRegistry::stampAnchorFor(t);
    EXPECT_FLOAT_EQ(anchor.x, -4.5f);   // mx.x=8 -> -(8/2) - 0.5
    EXPECT_FLOAT_EQ(anchor.z, -2.5f);   // mx.z=4 -> -(4/2) - 0.5 (stamp halves per AXIS extent)
    EXPECT_FLOAT_EQ(anchor.y, 0.0f);

    // Meshed at that anchor, voxel x=0 must start at -4.5 and voxel x=8 end at +4.5 — i.e.
    // the mesh's world span equals the stamp's span exactly (offset ghost = regression).
    std::vector<TemplateLodChain::Cell> cells;
    for (int x = 0; x <= 8; ++x) cells.push_back({glm::ivec3(x, 0, 4), "Log"});
    auto mesh = TreeLodMeshRegistry::buildLevelMesh(levelOf(std::move(cells)),
                                                    fakeResolver(), anchor);
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& v : mesh.vertices) {
        minX = std::min(minX, v.pos.x);
        maxX = std::max(maxX, v.pos.x);
    }
    EXPECT_FLOAT_EQ(minX, -4.5f);
    EXPECT_FLOAT_EQ(maxX, 4.5f);
}

// ============================================================================
// ProxyMeshMergeTest (PerfProgram 2026-09 section 17.2 step 3; design checks 9.12/9.13).
// buildLevelMesh with options.merge greedily merges coplanar same-texture faces and (by default)
// splits rectangle edges at every other rectangle's corner so the mesh stays watertight. The merge is
// only allowed because it cannot change the image: shading derives UVs from world position and a
// vertex carries only position + texture + face. These pin (a) coverage equality with the unmerged
// builder and (b) no T-junctions, on a stepped mixed-material building and a real tree template, at
// EVERY chain level.
// ============================================================================
#include <array>
#include <cmath>
#include <map>
#include <set>
#include "core/ObjectTemplateManager.h"

namespace {
FarMaterialResolver multiResolver() {
    return [](const std::string& m, int) -> uint16_t {
        if (m == "Leaf") return 7;
        if (m == "Stone") return 3;
        if (m == "Wood") return 4;
        if (m == "Log") return 5;
        return 9;
    };
}

// Stepped, L-shaped building whose wall material changes partway (texture boundaries + steps).
TemplateLodChain::Level steppedBuilding() {
    std::vector<TemplateLodChain::Cell> cells;
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 6; ++z) {
            const int h = x < 4 ? 3 : 5;                         // a step in the roofline
            for (int y = 0; y < h; ++y)
                cells.push_back({glm::ivec3(x, y, z), x == 5 ? "Log" : (y < 2 ? "Stone" : "Wood")});
        }
    for (int x = 0; x < 3; ++x)                                  // the L's arm, lower
        for (int z = 6; z < 10; ++z)
            for (int y = 0; y < 2; ++y) cells.push_back({glm::ivec3(x, y, z), "Stone"});
    return levelOf(std::move(cells), 3);
}

const VoxelTemplate* realTree(ObjectTemplateManager& otm) {
    for (const char* p : {"resources/templates/nature/forge_oak_m.voxel",
                          "../resources/templates/nature/forge_oak_m.voxel",
                          "../../resources/templates/nature/forge_oak_m.voxel",
                          "../../../resources/templates/nature/forge_oak_m.voxel"})
        if (otm.loadTemplate(p)) return otm.getTemplate("forge_oak_m");
    return nullptr;
}

constexpr int kN[6] = {2, 2, 0, 0, 1, 1}, kU[6] = {0, 0, 2, 2, 0, 0}, kV[6] = {1, 1, 1, 1, 2, 2};

struct Tri { glm::vec3 a, b, c; int face; uint16_t tex; };
std::vector<Tri> trisOf(const TreeLodMeshRegistry::CpuMesh& m) {
    std::vector<Tri> out;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto& va = m.vertices[m.indices[i]];
        out.push_back({va.pos, m.vertices[m.indices[i + 1]].pos, m.vertices[m.indices[i + 2]].pos,
                       int(farVertexFaceID(va.packed)), farVertexTexIndex(va.packed)});
    }
    return out;
}
long planeKey(const glm::vec3& p, int face) { return std::lround(p[kN[face]] * 1000.0); }
double area2(const Tri& t, int f) {
    const float ax = t.a[kU[f]], ay = t.a[kV[f]], bx = t.b[kU[f]], by = t.b[kV[f]], cx = t.c[kU[f]], cy = t.c[kV[f]];
    return std::abs(double(bx - ax) * (cy - ay) - double(cx - ax) * (by - ay)) * 0.5;
}
bool inTri(const Tri& t, int f, float px, float py) {
    auto cross = [&](const glm::vec3& p0, const glm::vec3& p1) {
        return (p1[kU[f]] - p0[kU[f]]) * (py - p0[kV[f]]) - (p1[kV[f]] - p0[kV[f]]) * (px - p0[kU[f]]);
    };
    const float d0 = cross(t.a, t.b), d1 = cross(t.b, t.c), d2 = cross(t.c, t.a);
    const bool neg = d0 < 0 || d1 < 0 || d2 < 0, pos = d0 > 0 || d1 > 0 || d2 > 0;
    return !(neg && pos);
}

// Coverage: every unmerged cell face is covered by EXACTLY one merged triangle of the same face,
// texture and plane (sampled at an off-centre point that no fan edge passes through), and the merged
// area per (face, texture, plane) equals the unmerged area -- so no gap, no overlap, no extra coverage.
void expectSameCoverage(const TemplateLodChain::Level& level, const char* what) {
    const auto un = TreeLodMeshRegistry::buildLevelMesh(level, multiResolver());
    TreeLodMeshRegistry::MeshOptions opt; opt.merge = true;
    const auto mg = TreeLodMeshRegistry::buildLevelMesh(level, multiResolver(), glm::vec3(-0.5f, 0.0f, -0.5f), opt);
    const auto mt = trisOf(mg);
    std::map<std::tuple<int, uint16_t, long>, std::vector<const Tri*>> groups;
    std::map<std::tuple<int, uint16_t, long>, double> areaMerged, areaUn;
    for (const auto& t : mt) {
        const auto k = std::make_tuple(t.face, t.tex, planeKey(t.a, t.face));
        groups[k].push_back(&t);
        areaMerged[k] += area2(t, t.face);
    }
    const auto ut = trisOf(un);
    for (const auto& t : ut) areaUn[std::make_tuple(t.face, t.tex, planeKey(t.a, t.face))] += area2(t, t.face);
    int bad = 0;
    for (size_t q = 0; q + 3 < un.vertices.size(); q += 4) {
        const auto& v0 = un.vertices[q]; const int f = int(farVertexFaceID(v0.packed));
        glm::vec3 mn = v0.pos, mx = v0.pos;
        for (int i = 1; i < 4; ++i) { mn = glm::min(mn, un.vertices[q + i].pos); mx = glm::max(mx, un.vertices[q + i].pos); }
        const float px = mn[kU[f]] + 0.37f * (mx[kU[f]] - mn[kU[f]]);
        const float py = mn[kV[f]] + 0.29f * (mx[kV[f]] - mn[kV[f]]);
        const auto k = std::make_tuple(f, farVertexTexIndex(v0.packed), planeKey(v0.pos, f));
        int hits = 0;
        for (const Tri* t : groups[k]) hits += inTri(*t, f, px, py) ? 1 : 0;
        if (hits != 1 && ++bad <= 5)
            ADD_FAILURE() << what << ": unmerged face " << q / 4 << " (face " << f << ") covered " << hits
                          << " times by merged triangles of its own texture and plane";
    }
    EXPECT_EQ(bad, 0) << what;
    ASSERT_EQ(areaMerged.size(), areaUn.size()) << what << ": merged mesh has faces the unmerged one lacks";
    for (const auto& [k, a] : areaUn)
        EXPECT_NEAR(areaMerged[k], a, 1e-4 * std::max(1.0, a)) << what << ": area differs in a (face, tex, plane) group";
    EXPECT_LT(mg.indices.size(), un.indices.size()) << what << ": merging produced no saving";
}

// T-junctions: no mesh vertex lies strictly inside an axis-aligned triangle edge (only boundary edges
// are axis-aligned; a fan's interior edges run to the rectangle centre and are shared only in-fan).
int countTJunctions(const TreeLodMeshRegistry::CpuMesh& m) {
    std::map<std::pair<int, std::pair<long, long>>, std::vector<long>> lines;   // axis, other two -> positions
    auto q = [](float v) { return std::lround(v * 9000.0); };
    auto keyFor = [&](int axis, const glm::vec3& p) {
        const long a = axis == 0 ? q(p.y) : q(p.x), b = axis == 2 ? q(p.y) : q(p.z);
        return std::make_pair(axis, std::make_pair(a, b));
    };
    for (const auto& v : m.vertices)
        for (int axis = 0; axis < 3; ++axis) lines[keyFor(axis, v.pos)].push_back(q(v.pos[axis]));
    for (auto& [k, vec] : lines) { std::sort(vec.begin(), vec.end()); vec.erase(std::unique(vec.begin(), vec.end()), vec.end()); }
    int tj = 0;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
        for (int e = 0; e < 3; ++e) {
            const glm::vec3 a = m.vertices[m.indices[i + e]].pos, b = m.vertices[m.indices[i + (e + 1) % 3]].pos;
            int diff = 0, axis = -1;
            for (int c = 0; c < 3; ++c) if (q(a[c]) != q(b[c])) { ++diff; axis = c; }
            if (diff != 1) continue;
            const auto& vec = lines[keyFor(axis, a)];
            const long lo = std::min(q(a[axis]), q(b[axis])), hi = std::max(q(a[axis]), q(b[axis]));
            tj += int(std::lower_bound(vec.begin(), vec.end(), hi) - std::upper_bound(vec.begin(), vec.end(), lo));
        }
    return tj;
}
}  // namespace

TEST(ProxyMeshMergeTest, MergedCoversExactlyTheSameFaces) {
    expectSameCoverage(steppedBuilding(), "stepped building");
    ObjectTemplateManager otm(nullptr);
    const VoxelTemplate* tree = realTree(otm);
    if (!tree) GTEST_SKIP() << "forge_oak_m.voxel not reachable from CWD";
    const auto chain = TemplateLodChain::build(*tree);
    for (size_t li = 0; li < chain.size(); ++li)
        if (!chain[li].cells.empty()) expectSameCoverage(chain[li], ("forge_oak_m level " + std::to_string(li)).c_str());
}

TEST(ProxyMeshMergeTest, NoTJunctions) {
    TreeLodMeshRegistry::MeshOptions opt; opt.merge = true;
    TreeLodMeshRegistry::MeshOptions plain = opt; plain.splitTJunctions = false;
    const auto b = steppedBuilding();
    // Teeth: the plain greedy merge of this building DOES leave T-junctions (the steps and the material
    // seam make neighbouring rectangles meet mid-edge) -- if this is 0 the check below proves nothing.
    EXPECT_GT(countTJunctions(TreeLodMeshRegistry::buildLevelMesh(b, multiResolver(), glm::vec3(-0.5f, 0, -0.5f), plain)), 0);
    EXPECT_EQ(countTJunctions(TreeLodMeshRegistry::buildLevelMesh(b, multiResolver(), glm::vec3(-0.5f, 0, -0.5f), opt)), 0)
        << "stepped building";
    ObjectTemplateManager otm(nullptr);
    const VoxelTemplate* tree = realTree(otm);
    if (!tree) GTEST_SKIP() << "forge_oak_m.voxel not reachable from CWD";
    const auto chain = TemplateLodChain::build(*tree);
    for (size_t li = 0; li < chain.size(); ++li) {
        if (chain[li].cells.empty()) continue;
        const auto merged = TreeLodMeshRegistry::buildLevelMesh(chain[li], multiResolver(), glm::vec3(-0.5f, 0, -0.5f), opt);
        const auto unmerged = TreeLodMeshRegistry::buildLevelMesh(chain[li], multiResolver());
        EXPECT_EQ(countTJunctions(merged), 0) << "forge_oak_m level " << li;
        std::printf("[ProxyMeshMerge] forge_oak_m L%zu: %zu -> %zu triangles (%.1fx)\n", li,
                    unmerged.indices.size() / 3, merged.indices.size() / 3,
                    double(unmerged.indices.size()) / double(std::max<size_t>(1, merged.indices.size())));
    }
}
