// FineFaceMergeTest.cpp — Binary greedy meshing for subcube/microcube faces.
//
// Red-before-green harness for docs/BinaryGreedyMeshingPlan.md. The project's #1 render issue is
// that sub/microcube faces are emitted one InstanceData per visible face with NO merging, while
// cube faces are greedy-merged. A flat same-material surface built from microcubes therefore
// explodes into 81 face-instances per cube face instead of collapsing to one quad.
//
// This file pins the CURRENT (unmerged) behavior as a characterization guard, and stages the
// post-merge target assertions as DISABLED_ tests. Increment 2 (within-cube microcube merging)
// enables MicrocubeMerge_TopFaceCollapsesPerCube; Increment 3 does the subcube sibling; Increment 4
// (cross-cube runs) enables the whole-slab collapse target.
//
// The mesher path under test (ChunkRenderManager::rebuildAllFaces) is pure CPU — it fills the
// `faces` vector and never touches Vulkan — so this is a true GPU-free unit test.

#include <gtest/gtest.h>

#include "graphics/ChunkRenderManager.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"

#include <memory>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::Graphics;

namespace {

// Face IDs (must match static_voxel.vert / packSubcubeFaceData): 0=+Z,1=-Z,2=+X,3=-X,4=+Y,5=-Y.
constexpr uint32_t FACE_PLUS_Y = 4u;

uint32_t faceIdOf(const InstanceData& inst) {
    return (inst.packedData >> 15) & 0x7u;
}

uint32_t scaleLevelOf(const InstanceData& inst) {
    return (inst.packedData >> 18) & 0x3u;
}

// Count face instances with the given faceID (any scale level).
size_t countFacesWithId(const std::vector<InstanceData>& faces, uint32_t faceID) {
    size_t n = 0;
    for (const auto& f : faces) if (faceIdOf(f) == faceID) ++n;
    return n;
}

// Build a flat, one-cube-thick slab of N x N cubes at y=0, each cube FULLY packed with a 9x9x9 grid
// of microcubes (729 per cube) of the same material. Returns the microcube vector; cubes/subcubes
// stay empty (the slab is pure microcube geometry, the worst case for the unmerged path).
std::vector<std::unique_ptr<Microcube>> buildMicrocubeSlab(int N, const std::string& material) {
    std::vector<std::unique_ptr<Microcube>> micros;
    micros.reserve(static_cast<size_t>(N) * N * 729);
    for (int cx = 0; cx < N; ++cx) {
        for (int cz = 0; cz < N; ++cz) {
            glm::ivec3 parentCube(cx, 0, cz);
            for (int sx = 0; sx < 3; ++sx)
            for (int sy = 0; sy < 3; ++sy)
            for (int sz = 0; sz < 3; ++sz)
            for (int mx = 0; mx < 3; ++mx)
            for (int my = 0; my < 3; ++my)
            for (int mz = 0; mz < 3; ++mz) {
                micros.push_back(std::make_unique<Microcube>(
                    parentCube, glm::ivec3(sx, sy, sz), glm::ivec3(mx, my, mz), material));
            }
        }
    }
    return micros;
}

std::vector<std::unique_ptr<Subcube>> emptySubs() { return {}; }
std::vector<std::unique_ptr<Cube>>    emptyCubes() { return {}; }

} // namespace

// ── Characterization guard: the current per-face path ────────────────────────────────────────────
// Documents the RED baseline: a microcube slab emits 81 top faces per cube (the 9x9 top grid),
// i.e. 81*N^2 for the +Y direction. This test PASSES today and exists so any accidental change to
// the unmerged path is caught. When Increment 2 lands, the merged path is selected by a toggle, so
// this baseline (toggle OFF) must still hold.
TEST(FineFaceMerge, MicrocubeSlab_UnmergedTopFaceCountIsElevenSquaredTimesEightyOne) {
    const int N = 4;
    auto micros = buildMicrocubeSlab(N, "Stone");
    auto subs = emptySubs();
    auto cubes = emptyCubes();

    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));

    const auto& faces = crm.getFaces();
    const size_t topFaces = countFacesWithId(faces, FACE_PLUS_Y);

    // 9x9 exposed microcubes on top of each cube, N*N cubes.
    EXPECT_EQ(topFaces, static_cast<size_t>(81 * N * N));
    // Every emitted face is a microcube (scale level 2) — no merging has collapsed them.
    for (const auto& f : faces) {
        if (faceIdOf(f) == FACE_PLUS_Y) EXPECT_EQ(scaleLevelOf(f), 2u);
    }
}

// ── Increment 2 target (DISABLED until within-cube microcube merging lands) ───────────────────────
// Within-cube merging collapses each cube's 9x9 top microcube grid into ONE quad → at most N^2 top
// faces for the whole slab. Run today with --gtest_also_run_disabled_tests to observe RED
// (81*N^2 emitted, assertion fails). Increment 2 removes the DISABLED_ prefix.
TEST(FineFaceMerge, DISABLED_MicrocubeMerge_TopFaceCollapsesPerCube) {
    const int N = 4;
    auto micros = buildMicrocubeSlab(N, "Stone");
    auto subs = emptySubs();
    auto cubes = emptyCubes();

    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));

    const size_t topFaces = countFacesWithId(crm.getFaces(), FACE_PLUS_Y);
    // One merged quad per cube face is the Increment-2 bar.
    EXPECT_LE(topFaces, static_cast<size_t>(N * N));
}

// ── Increment 4 target (DISABLED until cross-cube runs land) ──────────────────────────────────────
// Cross-cube merging collapses the entire same-material slab top into a small constant number of
// quads (extent-capped, so a 36-wide micro run may split — allow generous slack). Enabled at Inc 4.
TEST(FineFaceMerge, DISABLED_MicrocubeMerge_TopFaceCollapsesAcrossSlab) {
    const int N = 4;
    auto micros = buildMicrocubeSlab(N, "Stone");
    auto subs = emptySubs();
    auto cubes = emptyCubes();

    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));

    const size_t topFaces = countFacesWithId(crm.getFaces(), FACE_PLUS_Y);
    // The whole slab top is one same-material plane; expect a handful of quads, not per-cube.
    EXPECT_LE(topFaces, static_cast<size_t>(4));
}
