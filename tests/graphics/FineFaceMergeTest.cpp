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
#include <set>
#include <array>

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

// ── Decode REAL emitted InstanceData geometry (origin bits + extents), for the geometry-truth test ──
// This is the check that actually exercises the mesher's ORIGIN SELECTION: it reads the packedData
// grid-position bits the mesher wrote (not an assumed origin) and enumerates the world cells the
// merged quad covers, so a wrong origin/extent produces wrong cell positions and the set diverges
// from the per-face oracle. Coordinates are returned in 1/18-of-a-cube integer units (1/3 = 6/18,
// 1/9 = 2/18, so every sub- and micro-cell centre is an exact integer — no float compare).
void faceAxes(int faceID, int& uAxis, int& vAxis, int& dAxis) {
    if (faceID == 0 || faceID == 1) { uAxis = 0; vAxis = 1; dAxis = 2; }       // U=x V=y D=z
    else if (faceID == 2 || faceID == 3) { uAxis = 2; vAxis = 1; dAxis = 0; }  // U=z V=y D=x
    else { uAxis = 0; vAxis = 2; dAxis = 1; }                                  // U=x V=z D=y
}

// Multiset of covered cell centres (×18, integer) for faces of the given faceID at the given scale
// level. Works for the per-face path (extents 1 → one cell) AND the merged path (extents>1 → the
// uExt×vExt run enumerated from the DECODED origin). A wrong origin shifts these centres.
std::multiset<std::array<int,3>> coveredCellCentres(
    const std::vector<InstanceData>& faces, uint32_t faceID, uint32_t scaleLevel)
{
    std::multiset<std::array<int,3>> out;
    for (const auto& f : faces) {
        if (faceIdOf(f) != faceID || scaleLevelOf(f) != scaleLevel) continue;
        int px = (f.packedData >> 0) & 0x1F;
        int py = (f.packedData >> 5) & 0x1F;
        int pz = (f.packedData >> 10) & 0x1F;
        int subE = (f.packedData >> 20) & 0x3F;
        int sx = subE % 3, sy = (subE / 3) % 3, sz = subE / 9;
        int microE = (f.packedData >> 26) & 0x3F;
        int mxi = microE % 3, myi = (microE / 3) % 3, mzi = microE / 9;
        // Origin local coord (0..2 for sub, 0..8 for micro) per axis.
        int loc[3];
        if (scaleLevel == 1u) { loc[0] = sx; loc[1] = sy; loc[2] = sz; }
        else                  { loc[0] = sx*3 + mxi; loc[1] = sy*3 + myi; loc[2] = sz*3 + mzi; }
        int uAxis, vAxis, dAxis; faceAxes(faceID, uAxis, vAxis, dAxis);
        int eu = extentU(f), ev = extentV(f);
        const int pc3[3]    = { px, py, pz };
        const int perCube   = (scaleLevel == 1u) ? 3 : 9;            // cells per cube per axis
        const int chunkSpan = (scaleLevel == 1u) ? 96 : 288;        // cells per chunk per axis
        const int cellHalf  = (scaleLevel == 1u) ? 3 : 1;            // half-cell in 1/18 (1/6 or 1/18)
        const int cellFull  = (scaleLevel == 1u) ? 6 : 2;            // full cell in 1/18
        for (int i = 0; i < eu; ++i) for (int j = 0; j < ev; ++j) {
            int c[3]; c[uAxis] = loc[uAxis] + i; c[vAxis] = loc[vAxis] + j; c[dAxis] = loc[dAxis];
            // Absolute chunk-cell coord (cross-cube runs legitimately exceed one cube). Guard against
            // a max-origin overflow bug pushing a cell past the CHUNK border, and use the absolute
            // coord for the centre so within- and cross-cube runs share one code path.
            std::array<int,3> centre;
            for (int a = 0; a < 3; ++a) {
                int absolute = pc3[a] * perCube + c[a];
                EXPECT_GE(absolute, 0) << "cell below chunk bounds";
                EXPECT_LT(absolute, chunkSpan) << "cell out of chunk bounds";
                centre[a] = absolute * cellFull + cellHalf;
            }
            out.insert(centre);
        }
    }
    return out;
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

// Subcube analogue: N x N cubes at y=0, each fully packed with a 3x3x3 grid of subcubes (27/cube).
std::vector<std::unique_ptr<Subcube>> buildSubcubeSlab(int N, const std::string& material) {
    std::vector<std::unique_ptr<Subcube>> subs;
    for (int cx = 0; cx < N; ++cx) for (int cz = 0; cz < N; ++cz)
        for (int sx = 0; sx < 3; ++sx) for (int sy = 0; sy < 3; ++sy) for (int sz = 0; sz < 3; ++sz)
            subs.push_back(std::make_unique<Subcube>(
                glm::ivec3(cx, 0, cz), glm::ivec3(sx, sy, sz), material));
    return subs;
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
// Mirrors static_voxel.vert scaleLevel==1 (subcube): SUBCUBE_UV_SCALE=1/3, subcubeGridPos table,
// same fineFlipU/fineUVOriginShift. No microcube term.
UV shaderSubUV(int faceID, int sx, int sy, int sz, int extU, int extV, double s, double t) {
    double bu, bv;
    switch (faceID) {
        case 1:  bu = 1 - s; bv = 1 - t; break;
        case 4:  bu = 1 - s; bv = t;     break;
        default: bu = s;     bv = 1 - t; break;
    }
    double sgU, sgV;
    switch (faceID) {
        case 0: case 1: sgU = sx;     sgV = 2 - sy; break;
        case 2:         sgU = 2 - sz; sgV = 2 - sy; break;
        case 3:         sgU = sz;     sgV = 2 - sy; break;
        case 4:         sgU = 2 - sx; sgV = 2 - sz; break;
        default:        sgU = sx;     sgV = 2 - sz; break;
    }
    const bool flipU = (faceID == 2 || faceID == 4);
    const double shU = flipU ? -(extU - 1.0) : 0.0;
    const double shV = -(extV - 1.0);
    const double S3 = 1.0 / 3.0;
    return UV{ bu * extU * S3 + sgU * S3 + shU * S3,
              bv * extV * S3 + sgV * S3 + shV * S3 };
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

// ── Increment 3/4a: subcube merging (cross-cube) ───────────────────────────────────────────────────────────
// A uniform N×N cube slab's top (+Y) is a 3N×3N same-material plane with uniform (open-sky) light →
// cross-cube merging (Inc 4a) collapses it to ONE rectangle. Within-cube (Inc 3) would give N² (one
// per cube); the per-face path gives 9·N². Coverage stays exact.
TEST(FineFaceMerge, SubcubeMerge_TopFaceCollapsesAcrossSlab) {
    const int N = 4;
    auto micros = std::vector<std::unique_ptr<Microcube>>{};
    auto cubes = emptyCubes();

    // Unmerged reference (toggle off): 9 top faces per cube.
    {
        FineMergeScope off(false);
        ChunkRenderManager crm;
        auto s2 = buildSubcubeSlab(N, "Stone");
        crm.rebuildAllFaces(cubes, s2, micros, glm::ivec3(0, 0, 0));
        EXPECT_EQ(countFacesWithId(crm.getFaces(), FACE_PLUS_Y), static_cast<size_t>(9 * N * N));
    }
    // Merged (toggle on, cross-cube): the whole slab top is one rectangle; coverage still 9·N².
    {
        FineMergeScope on(true);
        ChunkRenderManager crm;
        auto subs = buildSubcubeSlab(N, "Stone");
        crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
        EXPECT_EQ(countFacesWithId(crm.getFaces(), FACE_PLUS_Y), static_cast<size_t>(1));
        size_t covered = 0;
        for (const auto& f : crm.getFaces())
            if (faceIdOf(f) == FACE_PLUS_Y && scaleLevelOf(f) == 1u) covered += extentU(f) * extentV(f);
        EXPECT_EQ(covered, static_cast<size_t>(9 * N * N));
    }
}

// Coverage invariant for subcubes: merged Σ(extentU*extentV) over subcube (scale level 1) faces ==
// per-face subcube count, in all 6 directions. Holed (checkerboard) + mixed tint within cubes.
TEST(FineFaceMerge, SubcubeMerge_CoverageMatchesPerFacePathWithHolesAndMixed) {
    const glm::ivec3 pc(6, 6, 6);
    auto build = [&]() {
        std::vector<std::unique_ptr<Subcube>> v;
        for (int x = 0; x < 3; ++x) for (int y = 0; y < 3; ++y) for (int z = 0; z < 3; ++z) {
            if (((x + y + z) % 2) != 0) continue;  // checkerboard holes
            auto sc = std::make_unique<Subcube>(pc, glm::ivec3(x, y, z), "Stone");
            sc->setTint((x % 2 == 0) ? 0x112233u : 0x445566u);  // mixed keys within one cube
            v.push_back(std::move(sc));
        }
        return v;
    };
    auto micros = std::vector<std::unique_ptr<Microcube>>{};
    auto cubes = emptyCubes();

    size_t perFace[6];
    {
        FineMergeScope off(false);
        auto subs = build();
        ChunkRenderManager crm;
        crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
        for (uint32_t f = 0; f < 6; ++f) {
            perFace[f] = 0;
            for (const auto& fi : crm.getFaces())
                if (faceIdOf(fi) == f && scaleLevelOf(fi) == 1u) ++perFace[f];
        }
    }
    {
        FineMergeScope on(true);
        auto subs = build();
        ChunkRenderManager crm;
        crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
        for (uint32_t f = 0; f < 6; ++f) {
            size_t covered = 0;
            for (const auto& fi : crm.getFaces())
                if (faceIdOf(fi) == f && scaleLevelOf(fi) == 1u) covered += extentU(fi) * extentV(fi);
            EXPECT_EQ(covered, perFace[f]) << "subcube holed/mixed coverage mismatch on faceID " << f;
        }
    }
    EXPECT_GT(perFace[FACE_PLUS_Y], static_cast<size_t>(0));
}

// UV correctness of the subcube merge + flip correction (scale level 1), all 6 faces, 3-wide run.
TEST(FineFaceMerge, SubcubeMerge_UVReplicaMatchesPerFaceEveryFace) {
    const int depth = 1;
    const int EXT = 3;
    for (int faceID = 0; faceID < 6; ++faceID) {
        int ox, oy, oz; uvdToLocal(faceID, 0, 0, depth, ox, oy, oz);
        for (int i = 0; i < EXT; ++i) for (int j = 0; j < EXT; ++j) {
            int lx, ly, lz; uvdToLocal(faceID, i, j, depth, lx, ly, lz);
            UV perFace = shaderSubUV(faceID, lx, ly, lz, 1, 1, 0.5, 0.5);
            double s, t; cellCentreST(faceID, i, j, EXT, EXT, s, t);
            UV merged  = shaderSubUV(faceID, ox, oy, oz, EXT, EXT, s, t);
            EXPECT_NEAR(merged.u, perFace.u, 1e-9) << "sub face " << faceID << " cell (" << i << "," << j << ") U";
            EXPECT_NEAR(merged.v, perFace.v, 1e-9) << "sub face " << faceID << " cell (" << i << "," << j << ") V";
        }
    }
}

// ── Geometry truth: the merged quads occupy the SAME world cells as the per-face path ─────────────
// THE test the earlier UVReplica/coverage checks lacked: it decodes the ACTUAL origin bits + extents
// the mesher emitted and enumerates the covered world cells, so a wrong ORIGIN (e.g. min-cell ->
// max-cell) or wrong extent shifts the cells and diverges from the per-face oracle. Non-trivial
// configs (checkerboard holes + mixed tint) put merged runs at NON-corner origins, and the multiset
// compare + in-bounds guard catch both position errors and cube-border overflow. Falsifiability was
// confirmed by injecting a min->max origin bug into rebuildSub/MicrocubeFacesMerged and watching
// these tests (and only these) go red.
TEST(FineFaceMerge, MicrocubeMerge_EmittedGeometryMatchesPerFaceEveryDirection) {
    // OFFSET SOLID BLOCK (local 3..8 on each axis) — produces MULTI-cell runs (extent up to 6) whose
    // origin is NOT at the cube corner, so a min->max origin bug shifts the covered cells (a
    // checkerboard of 1x1 runs would hide it — extent-1 runs have identical min/max origin).
    auto present  = [](int x, int y, int z) { return x >= 3 && y >= 3 && z >= 3; };
    auto material = [](int, int, int) { return "Stone"; };
    const glm::ivec3 pc(4, 7, 2);
    auto cubes = emptyCubes();
    std::multiset<std::array<int,3>> offC[6];
    {
        FineMergeScope off(false);
        auto micros = buildMicrocubeCube(pc, present, material); auto subs = emptySubs();
        ChunkRenderManager crm; crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0,0,0));
        for (uint32_t f = 0; f < 6; ++f) offC[f] = coveredCellCentres(crm.getFaces(), f, 2u);
    }
    {
        FineMergeScope on(true);
        auto micros = buildMicrocubeCube(pc, present, material); auto subs = emptySubs();
        ChunkRenderManager crm; crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0,0,0));
        for (uint32_t f = 0; f < 6; ++f)
            EXPECT_EQ(coveredCellCentres(crm.getFaces(), f, 2u), offC[f])
                << "microcube emitted geometry mismatch on faceID " << f;
    }
    EXPECT_GT(offC[FACE_PLUS_Y].size(), static_cast<size_t>(0));
}

TEST(FineFaceMerge, SubcubeMerge_EmittedGeometryMatchesPerFaceEveryDirection) {
    const glm::ivec3 pc(3, 8, 5);
    // OFFSET SOLID BLOCK (local 1..2 on each axis) — 2x2 runs whose origin is at local 1, not the
    // corner, so a min->max origin bug (origin 1 -> 2, extent 2 -> cells 2,3) is caught by the
    // cell-centre set compare AND the in-bounds guard. A 1x1-run config would not expose it.
    auto build = [&]() {
        std::vector<std::unique_ptr<Subcube>> v;
        for (int x = 1; x < 3; ++x) for (int y = 1; y < 3; ++y) for (int z = 1; z < 3; ++z) {
            auto sc = std::make_unique<Subcube>(pc, glm::ivec3(x, y, z), "Stone");
            v.push_back(std::move(sc));
        }
        return v;
    };
    auto micros = std::vector<std::unique_ptr<Microcube>>{}; auto cubes = emptyCubes();
    std::multiset<std::array<int,3>> offC[6];
    {
        FineMergeScope off(false);
        auto subs = build(); ChunkRenderManager crm; crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0,0,0));
        for (uint32_t f = 0; f < 6; ++f) offC[f] = coveredCellCentres(crm.getFaces(), f, 1u);
    }
    {
        FineMergeScope on(true);
        auto subs = build(); ChunkRenderManager crm; crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0,0,0));
        for (uint32_t f = 0; f < 6; ++f)
            EXPECT_EQ(coveredCellCentres(crm.getFaces(), f, 1u), offC[f])
                << "subcube emitted geometry mismatch on faceID " << f;
    }
    EXPECT_GT(offC[FACE_PLUS_Y].size(), static_cast<size_t>(0));
}

// ── Increment 4a: cross-cube SUBCUBE merging ─────────────────────────────────────────────────────
// A row of same-appearance cubes along X, each fully subcube-filled. The +Z exposed layer forms one
// 12(x)×3(y) same-material plane spanning all 4 cubes with uniform light → cross-cube merging
// collapses it to ONE rectangle. Within-cube merging (Inc 3) gives one per cube = 4. RED before Inc 4a.
TEST(FineFaceMerge, SubcubeMerge_CrossCubeCollapsesAlongRow) {
    FineMergeScope on(true);
    const int N = 4;
    std::vector<std::unique_ptr<Subcube>> subs;
    for (int cx = 0; cx < N; ++cx)
        for (int sx = 0; sx < 3; ++sx) for (int sy = 0; sy < 3; ++sy) for (int sz = 0; sz < 3; ++sz)
            subs.push_back(std::make_unique<Subcube>(glm::ivec3(cx, 0, 0), glm::ivec3(sx, sy, sz), "Stone"));
    auto micros = std::vector<std::unique_ptr<Microcube>>{}; auto cubes = emptyCubes();
    ChunkRenderManager crm; crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
    // The whole +Z plane (uniform appearance + light) is one rectangle across all 4 cubes.
    EXPECT_LE(countFacesWithId(crm.getFaces(), 0u /*+Z*/), static_cast<size_t>(2))
        << "cross-cube: +Z plane should collapse across the 4-cube row";
    // Coverage still exact: 4 cubes * 9 (+Z) subcube faces.
    EXPECT_EQ([&]{ size_t s=0; for (auto&f:crm.getFaces()) if(faceIdOf(f)==0u&&scaleLevelOf(f)==1u) s+=extentU(f)*extentV(f); return s; }(),
              static_cast<size_t>(N * 9));
}

// Cross-cube geometry safety net: positions must stay correct across cube boundaries (decode REAL
// emitted origin + extents; a cross-cube run's cells must equal the per-face oracle's, per direction).
TEST(FineFaceMerge, SubcubeMerge_CrossCubeGeometryMatchesPerFace) {
    // 2x1x1 cubes along X, offset from chunk origin, fully filled, one appearance → cross-cube runs.
    auto build = [&]() {
        std::vector<std::unique_ptr<Subcube>> v;
        for (int cx = 5; cx <= 6; ++cx)
            for (int sx=0;sx<3;++sx) for (int sy=0;sy<3;++sy) for (int sz=0;sz<3;++sz)
                v.push_back(std::make_unique<Subcube>(glm::ivec3(cx, 4, 7), glm::ivec3(sx,sy,sz), "Stone"));
        return v;
    };
    auto micros = std::vector<std::unique_ptr<Microcube>>{}; auto cubes = emptyCubes();
    std::multiset<std::array<int,3>> offC[6];
    {
        FineMergeScope off(false);
        auto subs = build(); ChunkRenderManager crm; crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0,0,0));
        for (uint32_t f = 0; f < 6; ++f) offC[f] = coveredCellCentres(crm.getFaces(), f, 1u);
    }
    {
        FineMergeScope on(true);
        auto subs = build(); ChunkRenderManager crm; crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0,0,0));
        for (uint32_t f = 0; f < 6; ++f)
            EXPECT_EQ(coveredCellCentres(crm.getFaces(), f, 1u), offC[f])
                << "cross-cube subcube geometry mismatch on faceID " << f;
    }
    EXPECT_GT(offC[0].size(), static_cast<size_t>(0));
}

// A merge run must NOT cross a tint boundary BETWEEN cubes: left 2 cubes tint A, right 2 tint B →
// the +Z plane keeps >=2 rectangles, coverage exact.
TEST(FineFaceMerge, SubcubeMerge_CrossCubeSplitsOnTintBoundaryBetweenCubes) {
    FineMergeScope on(true);
    const int N = 4;
    std::vector<std::unique_ptr<Subcube>> subs;
    for (int cx = 0; cx < N; ++cx)
        for (int sx=0;sx<3;++sx) for (int sy=0;sy<3;++sy) for (int sz=0;sz<3;++sz) {
            auto sc = std::make_unique<Subcube>(glm::ivec3(cx, 0, 0), glm::ivec3(sx, sy, sz), "Stone");
            sc->setTint(cx < 2 ? 0xAA1111u : 0x1111AAu);   // tint boundary between cube 1 and 2
            subs.push_back(std::move(sc));
        }
    auto micros = std::vector<std::unique_ptr<Microcube>>{}; auto cubes = emptyCubes();
    ChunkRenderManager crm; crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));
    EXPECT_GE(countFacesWithId(crm.getFaces(), 0u), static_cast<size_t>(2))
        << "cross-cube must split the +Z plane at the tint boundary";
    EXPECT_EQ([&]{ size_t s=0; for (auto&f:crm.getFaces()) if(faceIdOf(f)==0u&&scaleLevelOf(f)==1u) s+=extentU(f)*extentV(f); return s; }(),
              static_cast<size_t>(N * 9));
}

// A merge run must NOT cross a LIGHT boundary between cubes (the cross-cube merge key includes baked
// light). Two same-material/same-tint subcube cubes along X; a solid blocker cube 2 above column 0
// shades its +Y neighbour air cell (BFS skylight ~14) while column 1 stays open (15), so the two
// cubes' +Y faces carry DIFFERENT light and must not fuse. This is the light analogue of the tint
// split — the one that exercises the lightSky/light23 fields of the key on real baked output.
TEST(FineFaceMerge, SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes) {
    FineMergeScope on(true);
    std::vector<std::unique_ptr<Subcube>> subs;
    for (int cx = 0; cx <= 1; ++cx)
        for (int sx=0;sx<3;++sx) for (int sy=0;sy<3;++sy) for (int sz=0;sz<3;++sz)
            subs.push_back(std::make_unique<Subcube>(glm::ivec3(cx, 0, 0), glm::ivec3(sx, sy, sz), "Stone"));
    std::vector<std::unique_ptr<Cube>> cubes;
    cubes.push_back(std::make_unique<Cube>(glm::ivec3(0, 2, 0), "Stone"));  // blocker shading column 0
    auto micros = std::vector<std::unique_ptr<Microcube>>{};
    ChunkRenderManager crm; crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));

    auto topSubFaces = [&]{ size_t n=0; for (auto&f:crm.getFaces()) if (faceIdOf(f)==FACE_PLUS_Y && scaleLevelOf(f)==1u) ++n; return n; };
    auto topSubCovered = [&]{ size_t s=0; for (auto&f:crm.getFaces()) if (faceIdOf(f)==FACE_PLUS_Y && scaleLevelOf(f)==1u) s+=extentU(f)*extentV(f); return s; };
    EXPECT_GE(topSubFaces(), static_cast<size_t>(2)) << "cross-cube must split +Y at the light boundary";
    EXPECT_EQ(topSubCovered(), static_cast<size_t>(2 * 9));  // coverage exact (2 cubes * 9 top faces)
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
