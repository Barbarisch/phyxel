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
#include <functional>
#include <cmath>

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

// Merge extents packed into the light word (bits 16-23 = sizeU-1, 24-31 = sizeV-1). A merged fine
// instance covers sizeU*sizeV cells; unmerged/cube instances have both extents 1.
uint32_t extentU(const InstanceData& inst) { return ((inst.light >> 16) & 0xFFu) + 1u; }
uint32_t extentV(const InstanceData& inst) { return ((inst.light >> 24) & 0xFFu) + 1u; }

// Count face instances with the given faceID (any scale level).
size_t countFacesWithId(const std::vector<InstanceData>& faces, uint32_t faceID) {
    size_t n = 0;
    for (const auto& f : faces) if (faceIdOf(f) == faceID) ++n;
    return n;
}

// Sum of covered cells (extentU*extentV) over microcube (scale level 2) faces with the given faceID.
size_t coveredMicroCells(const std::vector<InstanceData>& faces, uint32_t faceID) {
    size_t n = 0;
    for (const auto& f : faces)
        if (faceIdOf(f) == faceID && scaleLevelOf(f) == 2u) n += extentU(f) * extentV(f);
    return n;
}

// RAII guard so a test's toggle change never leaks into sibling tests (the flag is global static).
struct FineMergeScope {
    explicit FineMergeScope(bool on) { ChunkRenderManager::setFineGreedyMerge(on); }
    ~FineMergeScope() { ChunkRenderManager::setFineGreedyMerge(false); }
};

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

// Build a single parent cube's microcubes with per-cell control: `present(x,y,z)` decides whether the
// 0..8 local cell exists (holes/partial occlusion), `material(x,y,z)` picks its material (mixed keys
// within one 9x9 mask — the case that actually stresses the greedy mesher). x/y/z are 0..8.
std::vector<std::unique_ptr<Microcube>> buildMicrocubeCube(
    const glm::ivec3& parentCube,
    const std::function<bool(int,int,int)>& present,
    const std::function<const char*(int,int,int)>& material)
{
    std::vector<std::unique_ptr<Microcube>> micros;
    for (int x = 0; x < 9; ++x) for (int y = 0; y < 9; ++y) for (int z = 0; z < 9; ++z) {
        if (!present(x, y, z)) continue;
        micros.push_back(std::make_unique<Microcube>(
            parentCube, glm::ivec3(x/3, y/3, z/3), glm::ivec3(x%3, y%3, z%3), material(x, y, z)));
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
    FineMergeScope off(false);  // per-face path
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

// ── Increment 2: within-cube microcube merging ───────────────────────────────────────────────────
// Within-cube merging collapses each cube's 9x9 top microcube grid into ONE quad → exactly N^2 top
// faces for the whole slab (one per cube). This was RED (1296) on the per-face path; Increment 2
// greens it.
TEST(FineFaceMerge, MicrocubeMerge_TopFaceCollapsesPerCube) {
    FineMergeScope on(true);
    const int N = 4;
    auto micros = buildMicrocubeSlab(N, "Stone");
    auto subs = emptySubs();
    auto cubes = emptyCubes();

    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));

    const size_t topFaces = countFacesWithId(crm.getFaces(), FACE_PLUS_Y);
    // Each cube's top slice is a full 9x9 of one material -> one merged rectangle per cube.
    EXPECT_EQ(topFaces, static_cast<size_t>(N * N));
}

// Coverage invariant: the merged path must cover EXACTLY the same microcube faces as the per-face
// path — no loss, no overlap. For each face direction, sum(extentU*extentV) over merged micro
// instances must equal the per-face micro instance count. (Rectangles are disjoint by construction,
// so equal area ⇒ same cell set.) This makes the per-face path the oracle for the merger.
TEST(FineFaceMerge, MicrocubeMerge_CoverageMatchesPerFacePathEveryDirection) {
    const int N = 3;
    auto cubes = emptyCubes();

    // Per-face reference counts (toggle OFF).
    size_t perFace[6];
    {
        FineMergeScope off(false);
        auto micros = buildMicrocubeSlab(N, "Stone");
        auto subs = emptySubs();
        ChunkRenderManager crm;
        crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
        for (uint32_t f = 0; f < 6; ++f) {
            perFace[f] = 0;
            for (const auto& fi : crm.getFaces())
                if (faceIdOf(fi) == f && scaleLevelOf(fi) == 2u) ++perFace[f];
        }
    }
    // Merged coverage (toggle ON) must match per direction.
    {
        FineMergeScope on(true);
        auto micros = buildMicrocubeSlab(N, "Stone");
        auto subs = emptySubs();
        ChunkRenderManager crm;
        crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
        for (uint32_t f = 0; f < 6; ++f)
            EXPECT_EQ(coveredMicroCells(crm.getFaces(), f), perFace[f])
                << "coverage mismatch on faceID " << f;
    }
}

// Merging must NOT cross an APPEARANCE boundary WITHIN a single parent cube's 9x9 mask — the one
// place the Key-equality check is actually exercised (per-cube grouping means two different cubes
// never share a mask). We split by TINT (a merge-key field set directly on the voxel), so the test
// is independent of whether MaterialRegistry/textures are loaded in the unit environment. Left 3
// columns tinted red, right 6 tinted blue → the merger must NOT fuse across the tint boundary.
TEST(FineFaceMerge, MicrocubeMerge_SplitsOnAppearanceBoundaryWithinOneCube) {
    FineMergeScope on(true);
    std::vector<std::unique_ptr<Microcube>> micros;
    for (int x = 0; x < 9; ++x) for (int y = 0; y < 9; ++y) for (int z = 0; z < 9; ++z) {
        auto mc = std::make_unique<Microcube>(
            glm::ivec3(0, 0, 0), glm::ivec3(x/3, y/3, z/3), glm::ivec3(x%3, y%3, z%3), "Stone");
        mc->setTint(x < 3 ? 0xFF0000u : 0x0000FFu);  // tint split along X at x=3
        micros.push_back(std::move(mc));
    }
    auto subs = emptySubs(); auto cubes = emptyCubes();
    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
    // The +Y top mask (9x9) has two tints split along X → at least 2 rectangles, never one merged
    // rectangle spanning the boundary.
    EXPECT_GE(countFacesWithId(crm.getFaces(), FACE_PLUS_Y), static_cast<size_t>(2));
    // Coverage still exact: 81 top microcube faces (one 9x9 layer). Combined with >=2 rects, this
    // rules out an over-merge (a boundary-spanning rectangle would still sum to 81 but give 1 rect).
    EXPECT_EQ(coveredMicroCells(crm.getFaces(), FACE_PLUS_Y), static_cast<size_t>(81));
}

// Coverage invariant on the REAL complexity class: a single parent cube with HOLES (checkerboard
// occlusion → non-rectangular visible regions) AND mixed materials. The per-face path is the oracle;
// the merged path must cover exactly the same microcube faces in every one of the 6 directions.
// This is the case (window/door cuts, furniture footprints, mixed keys) the trivial slab never hits.
TEST(FineFaceMerge, MicrocubeMerge_CoverageMatchesWithHolesAndMixedMaterials) {
    // A 3D checkerboard of present cells (holes everywhere) with material alternating on a different
    // parity — so visible regions are non-rectangular and keys vary cell-to-cell.
    auto present  = [](int x, int y, int z) { return ((x + y + z) % 2) == 0; };
    auto material = [](int x, int y, int z) { return ((x*2 + y + z) % 3 == 0) ? "Stone" : "Wood"; };
    const glm::ivec3 pc(5, 5, 5);
    auto cubes = emptyCubes();

    size_t perFace[6];
    {
        FineMergeScope off(false);
        auto micros = buildMicrocubeCube(pc, present, material);
        auto subs = emptySubs();
        ChunkRenderManager crm;
        crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
        for (uint32_t f = 0; f < 6; ++f) {
            perFace[f] = 0;
            for (const auto& fi : crm.getFaces())
                if (faceIdOf(fi) == f && scaleLevelOf(fi) == 2u) ++perFace[f];
        }
    }
    {
        FineMergeScope on(true);
        auto micros = buildMicrocubeCube(pc, present, material);
        auto subs = emptySubs();
        ChunkRenderManager crm;
        crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
        for (uint32_t f = 0; f < 6; ++f)
            EXPECT_EQ(coveredMicroCells(crm.getFaces(), f), perFace[f])
                << "holed/mixed coverage mismatch on faceID " << f;
    }
    // Guard against the degenerate pass (all-zero): the checkerboard must actually produce faces.
    EXPECT_GT(perFace[FACE_PLUS_Y], static_cast<size_t>(0));
}

// ── UV correctness of the merge + flip correction (addresses the "UV unfalsified" gap) ────────────
// A CPU replica of static_voxel.vert's microcube UV math. The test asserts that a MERGED rectangle
// samples, at each covered cell's centre, the SAME UV as the per-face path samples for that cell —
// i.e. merging + the fineUVOriginShift flip correction reproduce the union of the per-cell texture
// windows, for all 6 faces including runs that cross subcube boundaries and invert both axes. A wrong
// flip (the single most fragile piece, otherwise only eyeballed) makes merged != per-face here.
namespace {
struct UV { double u, v; };
// Mirrors static_voxel.vert scaleLevel==2: baseUV table, subcube/microcube grid tables, fineFlipU,
// fineUVOriginShift. (s,t) is the continuous quad parameter (bit0->s, bit1->t).
UV shaderMicroUV(int faceID, int sx, int sy, int sz, int mx, int my, int mz,
                 int extU, int extV, double s, double t) {
    double bu, bv;
    switch (faceID) {
        case 1:  bu = 1 - s; bv = 1 - t; break;
        case 4:  bu = 1 - s; bv = t;     break;
        default: bu = s;     bv = 1 - t; break;   // 0,2,3,5
    }
    double sgU, sgV, mgU, mgV;
    switch (faceID) {
        case 0: case 1: sgU = sx;     sgV = 2 - sy; mgU = mx;     mgV = 2 - my; break;
        case 2:         sgU = 2 - sz; sgV = 2 - sy; mgU = 2 - mz; mgV = 2 - my; break;
        case 3:         sgU = sz;     sgV = 2 - sy; mgU = mz;     mgV = 2 - my; break;
        case 4:         sgU = 2 - sx; sgV = 2 - sz; mgU = 2 - mx; mgV = 2 - mz; break;
        default:        sgU = sx;     sgV = 2 - sz; mgU = mx;     mgV = 2 - mz; break;  // 5
    }
    const bool flipU = (faceID == 2 || faceID == 4);
    const double shU = flipU ? -(extU - 1.0) : 0.0;
    const double shV = -(extV - 1.0);              // V inverted on all faces
    const double S3 = 1.0 / 3.0, S9 = 1.0 / 9.0;
    return UV{ bu * extU * S9 + sgU * S3 + mgU * S9 + shU * S9,
              bv * extV * S9 + sgV * S3 + mgV * S9 + shV * S9 };
}
// Map (u,v,depth) local grid indices (0..8) to (lx,ly,lz) for a face direction — matches the mesher.
void uvdToLocal(int faceID, int u, int v, int depth, int& lx, int& ly, int& lz) {
    if (faceID == 0 || faceID == 1) { lx = u; ly = v; lz = depth; }
    else if (faceID == 2 || faceID == 3) { lz = u; ly = v; lx = depth; }
    else { lx = u; lz = v; ly = depth; }
}
// The quad parameter (s,t) at which the merged rectangle samples cell (i,j). For faces whose
// faceOffset runs OPPOSITE to +local along an axis (static_voxel.vert: x=1-bit0 on face 1, z=1-bit0
// on face 2, z=1-bit1 on face 4), world +local corresponds to DECREASING s or t, so the cell sits at
// 1-(k+0.5)/ext rather than (k+0.5)/ext. This is a property of the shader's vertex geometry, derived
// independently of the UV math under test.
void cellCentreST(int faceID, int i, int j, int extU, int extV, double& s, double& t) {
    const bool revU = (faceID == 1 || faceID == 2);  // faceOffset U runs against +local
    const bool revV = (faceID == 4);                 // faceOffset V runs against +local
    double su = (i + 0.5) / extU, tv = (j + 0.5) / extV;
    s = revU ? 1.0 - su : su;
    t = revV ? 1.0 - tv : tv;
}
} // namespace

TEST(FineFaceMerge, MicrocubeMerge_UVReplicaMatchesPerFaceEveryFace) {
    const int depth = 4;      // arbitrary fixed slice
    const int EXT = 9;        // full within-cube run: crosses both subcube boundaries, both axes
    for (int faceID = 0; faceID < 6; ++faceID) {
        // Merged rectangle origin = min-local cell (0,0,depth), extents 9x9.
        int ox, oy, oz; uvdToLocal(faceID, 0, 0, depth, ox, oy, oz);
        for (int i = 0; i < EXT; ++i) for (int j = 0; j < EXT; ++j) {
            // Cell (i,j) within the run.
            int lx, ly, lz; uvdToLocal(faceID, i, j, depth, lx, ly, lz);
            // Per-face samples cell (i,j) at ITS OWN centre (unit quad → s=t=0.5).
            UV perFace = shaderMicroUV(faceID, lx/3, ly/3, lz/3, lx%3, ly%3, lz%3,
                                       1, 1, 0.5, 0.5);
            // Merged samples the same world point at the merged quad's (s,t) for that cell.
            double s, t; cellCentreST(faceID, i, j, EXT, EXT, s, t);
            UV merged  = shaderMicroUV(faceID, ox/3, oy/3, oz/3, ox%3, oy%3, oz%3,
                                       EXT, EXT, s, t);
            EXPECT_NEAR(merged.u, perFace.u, 1e-9)
                << "face " << faceID << " cell (" << i << "," << j << ") U";
            EXPECT_NEAR(merged.v, perFace.v, 1e-9)
                << "face " << faceID << " cell (" << i << "," << j << ") V";
        }
    }
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
