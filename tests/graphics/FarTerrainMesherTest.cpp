#include <gtest/gtest.h>
#include "graphics/FarTerrainMesher.h"
#include "core/WorldGenerator.h"

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::Graphics;

// ============================================================================
// FarTerrainMesher — pure-CPU far-terrain tile meshing invariants.
//
// Spec under test (see FarTerrainMesher.h):
//   - Columns sampled at their min-corner world coordinate, grid [-1..N] per axis.
//   - quantizeTop: multiple of step, in (surfaceY + 1 - step, surfaceY + 1].
//   - Tops: greedy-merged faceID-4 quads; every column covered exactly once at its
//     quantized height; total top area == tileSize².
//   - Walls: exact side walls on interior planes spanning |qA - qB| between adjacent
//     columns; +X/+Z border planes get exact walls too (using the sampled neighbor
//     column); -X/-Z border walls are owned by the neighbor tile.
//   - Skirts: all four border planes get outward-facing skirts kSkirtSteps*step tall,
//     hanging from min(qHere, qNeighbor).
//   - Mesh is emitted as quads: 4 vertices + 6 indices per quad.
//   - Deterministic: same generator config + key + step => bit-identical mesh.
// ============================================================================

namespace {

constexpr uint32_t kSeed = 12345;
constexpr int N = FarTerrainMesher::kColumns;

std::unique_ptr<WorldGenerator> makeGen() {
    return std::make_unique<WorldGenerator>(WorldGenerator::GenerationType::Perlin, kSeed);
}

uint16_t fakeResolver(const std::string& mat, int faceID) {
    uint32_t h = 2166136261u;
    for (char c : mat) h = (h ^ uint32_t(uint8_t(c))) * 16777619u;
    return uint16_t(((h * 31u) + uint32_t(faceID)) & 0x7FFFu);
}

struct Quad {
    glm::vec3 v[4];
    uint32_t faceID = 0;
    uint16_t tex = 0;
    float area() const {
        // Planar quad: area = |d1 x d2| / 2 (diagonals).
        return glm::length(glm::cross(v[2] - v[0], v[3] - v[1])) * 0.5f;
    }
    float minC(int axis) const { return std::min(std::min(v[0][axis], v[1][axis]), std::min(v[2][axis], v[3][axis])); }
    float maxC(int axis) const { return std::max(std::max(v[0][axis], v[1][axis]), std::max(v[2][axis], v[3][axis])); }
    bool allOnPlane(int axis, float value) const {
        for (const auto& p : v) if (p[axis] != value) return false;
        return true;
    }
};

// Group the mesh into quads (4 verts + 6 indices each) and sanity-check the layout.
std::vector<Quad> extractQuads(const FarTileMesh& mesh) {
    EXPECT_EQ(mesh.vertices.size() % 4, 0u) << "vertices must come in quads";
    EXPECT_EQ(mesh.indices.size(), mesh.vertices.size() / 4 * 6) << "6 indices per quad";
    std::vector<Quad> quads;
    for (size_t q = 0; q * 4 < mesh.vertices.size(); ++q) {
        Quad quad;
        for (int c = 0; c < 4; ++c) {
            const FarVertex& fv = mesh.vertices[q * 4 + c];
            quad.v[c] = fv.pos;
            quad.faceID = farVertexFaceID(fv.packed);
            quad.tex = farVertexTexIndex(fv.packed);
        }
        // The quad's 6 indices must reference only its own 4 vertices.
        for (size_t k = 0; k < 6; ++k) {
            uint32_t idx = mesh.indices[q * 6 + k];
            EXPECT_GE(idx, uint32_t(q * 4));
            EXPECT_LT(idx, uint32_t(q * 4 + 4));
        }
        quads.push_back(quad);
    }
    return quads;
}

// Ground-truth quantized heights over the sampled grid [-1..N] x [-1..N].
struct GroundTruth {
    int step = 0;
    glm::ivec2 origin{0, 0};
    std::vector<int> q;  // (N+2)*(N+2), index (i+1) + (j+1)*(N+2)
    int at(int i, int j) const { return q[size_t(i + 1) + size_t(j + 1) * (N + 2)]; }
};

GroundTruth computeGroundTruth(const FarTileKey& key, int step) {
    GroundTruth gt;
    gt.step = step;
    const int tileSize = N * step;
    gt.origin = glm::ivec2(key.x * tileSize, key.z * tileSize);
    gt.q.resize(size_t(N + 2) * size_t(N + 2));
    auto gen = makeGen();
    for (int j = -1; j <= N; ++j) {
        for (int i = -1; i <= N; ++i) {
            auto col = gen->sampleSurface(gt.origin.x + i * step, gt.origin.y + j * step);
            gt.q[size_t(i + 1) + size_t(j + 1) * (N + 2)] =
                FarTerrainMesher::quantizeTop(col.surfaceY, step);
        }
    }
    return gt;
}

const FarTileKey kKey{1, 3, -2};
constexpr int kStep = 2;

} // namespace

// ---------------------------------------------------------------------------

TEST(FarTerrainMesherTest, QuantizeTop_MultipleOfStep_WithinOneStepBelowSurfacePlane) {
    for (int step : {2, 4, 8, 16}) {
        for (int surfaceY = -70; surfaceY <= 70; ++surfaceY) {
            int q = FarTerrainMesher::quantizeTop(surfaceY, step);
            EXPECT_EQ(((q % step) + step) % step, 0) << "y=" << surfaceY << " s=" << step;
            EXPECT_LE(q, surfaceY + 1) << "far terrain must never rise above the real surface plane";
            EXPECT_GT(q, surfaceY + 1 - step) << "y=" << surfaceY << " s=" << step;
        }
    }
}

TEST(FarTerrainMesherTest, BuildTile_NonEmptyAndDeterministic) {
    FarTerrainMesher mesherA(makeGen(), fakeResolver);
    FarTerrainMesher mesherB(makeGen(), fakeResolver);
    FarTileMesh a = mesherA.buildTile(kKey, kStep);
    FarTileMesh b = mesherB.buildTile(kKey, kStep);

    ASSERT_GT(a.vertices.size(), 0u) << "tile over Perlin terrain must produce geometry";
    ASSERT_EQ(a.vertices.size(), b.vertices.size());
    ASSERT_EQ(a.indices.size(), b.indices.size());
    EXPECT_EQ(0, std::memcmp(a.vertices.data(), b.vertices.data(),
                             a.vertices.size() * sizeof(FarVertex)));
    EXPECT_EQ(0, std::memcmp(a.indices.data(), b.indices.data(),
                             a.indices.size() * sizeof(uint32_t)));
    EXPECT_EQ(a.originXZ, glm::ivec2(kKey.x * N * kStep, kKey.z * N * kStep));
    EXPECT_EQ(a.step, kStep);
    EXPECT_EQ(a.tileSize, N * kStep);
    EXPECT_EQ(a.minY, b.minY);
    EXPECT_EQ(a.maxY, b.maxY);
}

TEST(FarTerrainMesherTest, BuildTile_EveryColumnCoveredOnceAtQuantizedHeight) {
    FarTerrainMesher mesher(makeGen(), fakeResolver);
    FarTileMesh mesh = mesher.buildTile(kKey, kStep);
    GroundTruth gt = computeGroundTruth(kKey, kStep);
    std::vector<Quad> quads = extractQuads(mesh);

    const float tileSize = float(N * kStep);
    float topArea = 0.0f;
    for (const auto& q : quads) {
        if (q.faceID == 4) topArea += q.area();
    }
    EXPECT_FLOAT_EQ(topArea, tileSize * tileSize) << "tops must tile the whole footprint";

    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            const float cx = (i + 0.5f) * kStep;   // tile-local column center
            const float cz = (j + 0.5f) * kStep;
            int covering = 0;
            float coveredY = 0.0f;
            for (const auto& q : quads) {
                if (q.faceID != 4) continue;
                if (cx > q.minC(0) && cx < q.maxC(0) && cz > q.minC(2) && cz < q.maxC(2)) {
                    ++covering;
                    coveredY = q.v[0].y;
                }
            }
            ASSERT_EQ(covering, 1) << "column (" << i << "," << j << ") must have exactly one top";
            EXPECT_FLOAT_EQ(coveredY, float(gt.at(i, j)))
                << "column (" << i << "," << j << ") top at wrong height";
        }
    }
}

TEST(FarTerrainMesherTest, BuildTile_InteriorWallAreaMatchesHeightDiffs) {
    FarTerrainMesher mesher(makeGen(), fakeResolver);
    FarTileMesh mesh = mesher.buildTile(kKey, kStep);
    GroundTruth gt = computeGroundTruth(kKey, kStep);
    std::vector<Quad> quads = extractQuads(mesh);

    // Interior X planes (tile-local x = k*step, k = 1..N-1): no skirts live there, so
    // emitted wall area must equal the sum of adjacent-column height differences.
    for (int k = 1; k < N; ++k) {
        float expected = 0.0f;
        for (int j = 0; j < N; ++j) expected += float(std::abs(gt.at(k - 1, j) - gt.at(k, j))) * kStep;
        float emitted = 0.0f;
        for (const auto& q : quads) {
            if ((q.faceID == 2 || q.faceID == 3) && q.allOnPlane(0, float(k * kStep))) emitted += q.area();
        }
        EXPECT_FLOAT_EQ(emitted, expected) << "X wall plane k=" << k;
    }
    // Interior Z planes.
    for (int k = 1; k < N; ++k) {
        float expected = 0.0f;
        for (int i = 0; i < N; ++i) expected += float(std::abs(gt.at(i, k - 1) - gt.at(i, k))) * kStep;
        float emitted = 0.0f;
        for (const auto& q : quads) {
            if ((q.faceID == 0 || q.faceID == 1) && q.allOnPlane(2, float(k * kStep))) emitted += q.area();
        }
        EXPECT_FLOAT_EQ(emitted, expected) << "Z wall plane k=" << k;
    }
}

TEST(FarTerrainMesherTest, BuildTile_BorderSkirtsAndExactBorderWalls) {
    FarTerrainMesher mesher(makeGen(), fakeResolver);
    FarTileMesh mesh = mesher.buildTile(kKey, kStep);
    GroundTruth gt = computeGroundTruth(kKey, kStep);
    std::vector<Quad> quads = extractQuads(mesh);

    const float tileSize = float(N * kStep);
    const float skirtArea = float(N) * FarTerrainMesher::kSkirtSteps * kStep * kStep;

    // -X border plane (x=0): skirts only, facing outward (-X => faceID 3).
    float minusX = 0.0f;
    for (const auto& q : quads) {
        if (q.faceID != 4 && q.allOnPlane(0, 0.0f)) {
            EXPECT_EQ(q.faceID, 3u) << "-X border geometry must face outward";
            minusX += q.area();
        }
    }
    EXPECT_FLOAT_EQ(minusX, skirtArea);

    // +X border plane (x=tileSize): skirts (outward, faceID 2) plus exact walls whose
    // facing points at the LOWER column — outward (2) when our column is higher, inward
    // (3) when the neighbor column is higher.
    float plusXExpected2 = skirtArea, plusXExpected3 = 0.0f;
    for (int j = 0; j < N; ++j) {
        int diff = gt.at(N - 1, j) - gt.at(N, j);
        if (diff > 0) plusXExpected2 += float(diff) * kStep;
        else          plusXExpected3 += float(-diff) * kStep;
    }
    float plusX2 = 0.0f, plusX3 = 0.0f;
    for (const auto& q : quads) {
        if (q.faceID != 4 && q.allOnPlane(0, tileSize)) {
            ASSERT_TRUE(q.faceID == 2u || q.faceID == 3u);
            (q.faceID == 2u ? plusX2 : plusX3) += q.area();
        }
    }
    EXPECT_FLOAT_EQ(plusX2, plusXExpected2);
    EXPECT_FLOAT_EQ(plusX3, plusXExpected3);

    // -Z border plane (z=0): skirts only, facing outward (-Z => faceID 1).
    float minusZ = 0.0f;
    for (const auto& q : quads) {
        if (q.faceID != 4 && q.allOnPlane(2, 0.0f)) {
            EXPECT_EQ(q.faceID, 1u) << "-Z border geometry must face outward";
            minusZ += q.area();
        }
    }
    EXPECT_FLOAT_EQ(minusZ, skirtArea);

    // +Z border plane: skirts (outward, faceID 0) plus exact walls facing the lower
    // column — outward (0) when our column is higher, inward (1) when neighbor is higher.
    float plusZExpected0 = skirtArea, plusZExpected1 = 0.0f;
    for (int i = 0; i < N; ++i) {
        int diff = gt.at(i, N - 1) - gt.at(i, N);
        if (diff > 0) plusZExpected0 += float(diff) * kStep;
        else          plusZExpected1 += float(-diff) * kStep;
    }
    float plusZ0 = 0.0f, plusZ1 = 0.0f;
    for (const auto& q : quads) {
        if (q.faceID != 4 && q.allOnPlane(2, tileSize)) {
            ASSERT_TRUE(q.faceID == 0u || q.faceID == 1u);
            (q.faceID == 0u ? plusZ0 : plusZ1) += q.area();
        }
    }
    EXPECT_FLOAT_EQ(plusZ0, plusZExpected0);
    EXPECT_FLOAT_EQ(plusZ1, plusZExpected1);
}

TEST(FarTerrainMesherTest, BuildTile_YBoundsAreTight) {
    FarTerrainMesher mesher(makeGen(), fakeResolver);
    FarTileMesh mesh = mesher.buildTile(kKey, kStep);
    ASSERT_GT(mesh.vertices.size(), 0u);

    float lo = mesh.vertices[0].pos.y, hi = lo;
    for (const auto& v : mesh.vertices) {
        lo = std::min(lo, v.pos.y);
        hi = std::max(hi, v.pos.y);
    }
    EXPECT_FLOAT_EQ(mesh.minY, lo);
    EXPECT_FLOAT_EQ(mesh.maxY, hi);
}
