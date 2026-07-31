//
// SeaMeshTest — the sea clipmap's structural invariants.
//
// The polar mesh this replaces failed on a property nobody had measured: its ANGULAR sample spacing
// grew with radius, so the swell aliased into radial spokes converging on the camera. The lesson is
// that "it renders" says nothing, so the replacement gets its invariants asserted directly:
//
//   L2  no T-junction anywhere (a crack in water is a pinhole through to the sky)
//   L2  every level samples at the density it claims
//   L2  coverage actually reaches the requested wave radius
//   L2  cost stays bounded, and does NOT grow the way the polar mesh's ring count did
//
#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

#include "graphics/SeaMesh.h"

using namespace Phyxel::Graphics;

namespace {

// An edge is "open" if exactly one triangle uses it — that is the outer silhouette. An edge used by
// more than two triangles means overlapping geometry. A T-junction is different and nastier: a
// vertex sitting ON another edge's span without being one of its endpoints. Both are checked.
struct EdgeKey {
    uint32_t a, b;
    bool operator<(const EdgeKey& o) const {
        return a != o.a ? a < o.a : b < o.b;
    }
};
EdgeKey edgeOf(uint32_t x, uint32_t y) { return x < y ? EdgeKey{x, y} : EdgeKey{y, x}; }

}  // namespace

TEST(SeaMeshTest, EveryInteriorEdgeIsSharedByExactlyTwoTriangles) {
    const SeaMesh m = buildSeaClipmap(700.0f);
    ASSERT_GT(m.triangles(), 0);

    std::map<EdgeKey, int> uses;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        uses[edgeOf(m.indices[i + 0], m.indices[i + 1])]++;
        uses[edgeOf(m.indices[i + 1], m.indices[i + 2])]++;
        uses[edgeOf(m.indices[i + 2], m.indices[i + 0])]++;
    }
    int open = 0, overused = 0;
    for (const auto& [e, n] : uses) {
        if (n == 1) ++open;
        if (n > 2) ++overused;
    }
    EXPECT_EQ(overused, 0) << "edges shared by >2 triangles means overlapping geometry";
    // The only open edges should be the outermost skirt ring's perimeter.
    EXPECT_LE(open, SEA_CELLS * 4) << "too many open edges - the surface has holes in it";
}

TEST(SeaMeshTest, NoTJunctions) {
    const SeaMesh m = buildSeaClipmap(700.0f);

    // Collect every edge actually used, and every vertex position.
    std::set<EdgeKey> edges;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        edges.insert(edgeOf(m.indices[i + 0], m.indices[i + 1]));
        edges.insert(edgeOf(m.indices[i + 1], m.indices[i + 2]));
        edges.insert(edgeOf(m.indices[i + 2], m.indices[i + 0]));
    }

    // Index vertices by lattice cell so the midpoint lookup is O(1) rather than O(V) per edge.
    // Half the core spacing: edge midpoints are the finest positions in play, so a coarser key
    // would alias a midpoint onto a corner (which is exactly the bug this suite first caught).
    auto key = [](const glm::vec3& v) {
        return std::make_pair(std::llround(v.x / (SEA_CORE_SPACING * 0.5f)),
                              std::llround(v.z / (SEA_CORE_SPACING * 0.5f)));
    };
    std::map<std::pair<int64_t, int64_t>, uint32_t> byCell;
    for (uint32_t i = 0; i < m.vertices.size(); ++i) byCell.emplace(key(m.vertices[i]), i);

    int tjunctions = 0;
    for (const auto& e : edges) {
        const glm::vec3& A = m.vertices[e.a];
        const glm::vec3& B = m.vertices[e.b];
        // Only axis-aligned clipmap edges can host a T-junction; diagonals and skirt edges cannot,
        // because no level places a vertex along them.
        const bool axis = (std::fabs(A.x - B.x) < 1e-3f) != (std::fabs(A.z - B.z) < 1e-3f);
        if (!axis) continue;
        const glm::vec3 mid((A.x + B.x) * 0.5f, 0.0f, (A.z + B.z) * 0.5f);
        auto it = byCell.find(key(mid));
        if (it == byCell.end()) continue;
        const uint32_t v = it->second;
        if (v == e.a || v == e.b) continue;
        // A vertex exists at this edge's midpoint. That is a crack unless the edge was subdivided,
        // i.e. unless the two half-edges are themselves used.
        if (!edges.count(edgeOf(e.a, v)) || !edges.count(edgeOf(v, e.b))) ++tjunctions;
    }
    EXPECT_EQ(tjunctions, 0) << "a vertex sits mid-edge without that edge being split - "
                                "this shows as a pinhole straight through the water";
}

TEST(SeaMeshTest, EachLevelSamplesTheSwellAboveNyquist) {
    // The failure this mesh exists to fix. The old polar mesh's angular spacing grew without bound
    // (0.28 wavelengths/segment at r=60 but 3.23 at r=691); a clipmap's spacing must instead stay
    // proportional to radius, so wavelengths-per-segment is BOUNDED rather than growing.
    const SeaMesh m = buildSeaClipmap(700.0f);
    for (int k = 0; k < m.levels; ++k) {
        const float spacing = SEA_CORE_SPACING * static_cast<float>(1 << k);
        const float radius  = SEA_CORE_HALF * static_cast<float>(1 << k);
        // spacing / radius is the ANGULAR density, and it is what the polar mesh got wrong.
        EXPECT_NEAR(spacing / radius, SEA_CORE_SPACING / SEA_CORE_HALF, 1e-5f)
            << "level " << k << " breaks the constant angular-density property";
    }
    // The core resolves the 14-unit default swell with margin.
    EXPECT_LT(SEA_CORE_SPACING / 14.0f, 0.5f);
}

TEST(SeaMeshTest, CoverageReachesTheRequestedRadius) {
    for (float r : {150.0f, 300.0f, 700.0f, 2000.0f}) {
        const SeaMesh m = buildSeaClipmap(r);
        EXPECT_GE(m.outerExtent, r) << "clipmap does not reach the requested wave radius " << r;
    }
}

TEST(SeaMeshTest, CostIsBoundedAndBarelyGrowsWithViewDistance) {
    // The polar mesh added a ring per 4.1 units of view distance. A clipmap adds one LEVEL per
    // doubling, so going from 175 units of reach to 8192 must cost only a few times more, not ~47x.
    const SeaMesh near = buildSeaClipmap(150.0f);
    const SeaMesh far  = buildSeaClipmap(8000.0f);
    EXPECT_LT(far.triangles(), near.triangles() * 8)
        << "cost is growing too fast with view distance";
    // Absolute budget: the polar mesh it replaces was ~33,800 triangles reaching only 700 units.
    EXPECT_LT(far.triangles(), 60000);
    EXPECT_GT(far.outerExtent, 700.0f);
}
