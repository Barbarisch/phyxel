// C4 of docs/ContinuousLodPlan.md — THE CUT. Renders a chunk at a CHOSEN LOD level.
//
// This is what the whole plan exists for: C0 built the squash, C1 built the metric that
// picks the level, and this turns level N into actual renderable faces.
//
// The tests are built around one falsifiable anchor: at level 0 the coarse path must
// reproduce the fine surface EXACTLY (same face count, unit-sized quads). Without that
// identity the coarse path could be arbitrarily wrong and only "look plausible".

#include <gtest/gtest.h>

#include <set>
#include <utility>

#include "core/Chunk.h"
#include "core/LodChunkMesh.h"
#include "core/Types.h"

using namespace Phyxel::Core;
using Phyxel::Chunk;
using Phyxel::InstanceData;
namespace IDU = Phyxel::InstanceDataUtils;

namespace {

/// A solid axis-aligned box of cubes inside one chunk.
std::unique_ptr<Chunk> boxChunk(int x0, int y0, int z0, int x1, int y1, int z1,
                                const char* mat = "Stone") {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                c->addCube(glm::ivec3(x, y, z), mat);
    return c;
}

size_t countLevel(const std::vector<InstanceData>& v, uint32_t level) {
    size_t n = 0;
    for (const auto& i : v)
        if (IDU::isLodCell(i.packedData) &&
            IDU::lodCellLevel(i.packedData) == level) ++n;
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// THE ANCHOR: level 0 must reproduce the fine surface exactly.
// ---------------------------------------------------------------------------
TEST(LodChunkMeshTest, LevelZeroReproducesTheFineSurfaceExactly) {
    auto chunk = boxChunk(4, 4, 4, 11, 11, 11);      // 8x8x8 solid box
    std::vector<InstanceData> faces;
    LodChunkMesh::buildForLevel(*chunk, 0, SquashConfig{}, faces);

    // A solid 8-cube box exposes 6 faces of 8x8 cubes = 384 unit quads.
    EXPECT_EQ(faces.size(), 6u * 8u * 8u)
        << "level 0 must emit exactly the exposed unit faces of the box";
    EXPECT_EQ(countLevel(faces, 0), faces.size()) << "every face must be tagged level 0";
    EXPECT_EQ(faces.size(), LodChunkMesh::fineFaceCount(*chunk));
}

TEST(LodChunkMeshTest, InteriorFacesAreNotEmitted) {
    auto chunk = boxChunk(0, 0, 0, 3, 3, 3);          // 4x4x4 = 64 cubes
    std::vector<InstanceData> faces;
    LodChunkMesh::buildForLevel(*chunk, 0, SquashConfig{}, faces);
    EXPECT_EQ(faces.size(), 6u * 4u * 4u)
        << "only the shell may be emitted; 64 cubes x 6 would be 384 if interiors leaked";
}

// ---------------------------------------------------------------------------
// THE POINT: coarser levels emit dramatically fewer faces for the same solid.
// ---------------------------------------------------------------------------
TEST(LodChunkMeshTest, CoarserLevelsCollapseFaceCount) {
    auto chunk = boxChunk(0, 0, 0, 15, 15, 15);       // 16^3 solid box
    size_t prev = SIZE_MAX;
    for (int level = 0; level <= 3; ++level) {
        std::vector<InstanceData> faces;
        LodChunkMesh::buildForLevel(*chunk, level, SquashConfig{}, faces);
        ASSERT_GT(faces.size(), 0u) << "level " << level << " emitted nothing";
        EXPECT_EQ(countLevel(faces, uint32_t(level)), faces.size())
            << "level " << level << " faces must all carry lodLevel " << level;
        EXPECT_LT(faces.size(), prev)
            << "level " << level << " did not reduce the face count (" << faces.size()
            << " vs " << prev << ")";
        prev = faces.size();
    }
    // 16^3 box: level 0 = 6*16*16 = 1536 unit faces; level 2 (4-cube cells) = 6*4*4 = 96.
    std::vector<InstanceData> l0, l2;
    LodChunkMesh::buildForLevel(*chunk, 0, SquashConfig{}, l0);
    LodChunkMesh::buildForLevel(*chunk, 2, SquashConfig{}, l2);
    EXPECT_EQ(l0.size(), 1536u);
    EXPECT_EQ(l2.size(), 96u);
    EXPECT_EQ(l0.size() / l2.size(), 16u) << "a 4x cell edge must cut faces 16x (area scaling)";
}

// ---------------------------------------------------------------------------
// The coarse quad must COVER the same volume it replaces — the encoded cell origin
// and level have to survive the round-trip, or the shader draws it in the wrong place.
// ---------------------------------------------------------------------------
TEST(LodChunkMeshTest, CellOriginsAreCubeAlignedToTheirLevel) {
    auto chunk = boxChunk(0, 0, 0, 15, 15, 15);
    for (int level = 1; level <= 3; ++level) {
        std::vector<InstanceData> faces;
        LodChunkMesh::buildForLevel(*chunk, level, SquashConfig{}, faces);
        const uint32_t cell = 1u << level;
        for (const auto& f : faces) {
            const uint32_t x = f.packedData & 0x1F;
            const uint32_t y = (f.packedData >> 5) & 0x1F;
            const uint32_t z = (f.packedData >> 10) & 0x1F;
            ASSERT_EQ(x % cell, 0u) << "level " << level << " origin x not cell-aligned";
            ASSERT_EQ(y % cell, 0u) << "level " << level << " origin y not cell-aligned";
            ASSERT_EQ(z % cell, 0u) << "level " << level << " origin z not cell-aligned";
        }
    }
}

TEST(LodChunkMeshTest, EncodingRoundTrips) {
    for (uint32_t level = 0; level <= 7; ++level) {
        const uint32_t p = IDU::packLodCellData(8, 16, 24, 4, level);
        EXPECT_TRUE(IDU::isLodCell(p)) << "level " << level;
        EXPECT_EQ(IDU::lodCellLevel(p), level);
        EXPECT_EQ(p & 0x1F, 8u);
        EXPECT_EQ((p >> 5) & 0x1F, 16u);
        EXPECT_EQ((p >> 10) & 0x1F, 24u);
        EXPECT_EQ((p >> 15) & 0x7, 4u);
        EXPECT_EQ((p >> 18) & 0x3, 3u) << "scaleLevel must be 3 (the LOD-cell code)";
    }
}

// An empty chunk must produce nothing at every level (no phantom geometry from padding).
TEST(LodChunkMeshTest, EmptyChunkEmitsNothingAtEveryLevel) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    for (int level = 0; level <= 4; ++level) {
        std::vector<InstanceData> faces;
        LodChunkMesh::buildForLevel(*c, level, SquashConfig{}, faces);
        EXPECT_TRUE(faces.empty()) << "level " << level << " invented " << faces.size() << " faces";
    }
}

// A hollow shell must stay hollow at level 0 — proves the mesher reads real occupancy
// rather than a bounding box.
TEST(LodChunkMeshTest, HollowShellIsNotFilledIn) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 6; ++y)
            for (int z = 0; z < 6; ++z) {
                const bool shell = (x == 0 || x == 5 || y == 0 || y == 5 || z == 0 || z == 5);
                if (shell) c->addCube(glm::ivec3(x, y, z), "Stone");
            }
    std::vector<InstanceData> faces;
    LodChunkMesh::buildForLevel(*c, 0, SquashConfig{}, faces);
    // Outer shell 6*6*6=216 faces, plus the inner cavity's 6*4*4=96 inward faces.
    EXPECT_EQ(faces.size(), 6u * 6u * 6u + 6u * 4u * 4u)
        << "hollow interior faces missing => the mesher is filling the shell in";
}

// ---------------------------------------------------------------------------
// RED (solution-auditor, 2026-07-30): SUB/MICROCUBE-ONLY CONTENT WAS INVISIBLE TO THE CUT.
//
// volumeFromChunk originally marked a cell solid only via getCubeAt(), which reports FULL-CUBE
// presence. A cell holding only subcubes/microcubes — exactly how the structure generator
// authors thin walls (timber_cottage interior_wall is ONE microcube,
// resources/structure_styles.json) — never sets that flag, so the coarse mesh emitted ZERO
// faces and every structure silently vanished at distance.
//
// This contradicted the plan's own §2.1 premise: sub/micro detail is the cube's APPEARANCE,
// carried as `coverage` — which is precisely what LodCell::coverage exists for and what the
// first implementation failed to populate.
// ---------------------------------------------------------------------------
TEST(LodChunkMeshTest, SubcubeOnlyWallIsVisibleToTheCut) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    // A thin wall made ONLY of subcubes — no addCube anywhere.
    for (int y = 0; y < 4; ++y)
        for (int z = 0; z < 4; ++z)
            c->addSubcube(glm::ivec3(5, y, z), glm::ivec3(0, 1, 1), "Wood");

    std::vector<InstanceData> faces;
    LodChunkMesh::buildForLevel(*c, 0, SquashConfig{}, faces);
    EXPECT_GT(faces.size(), 0u)
        << "a subcube-only wall produced NO coarse faces — the cut deletes structure detail";
}

TEST(LodChunkMeshTest, MicrocubeOnlyContentIsVisibleToTheCut) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    // The thinnest thing the generator emits: a single microcube.
    c->addMicrocube(glm::ivec3(2, 2, 2), glm::ivec3(1, 1, 1), glm::ivec3(1, 1, 1), "Stone");
    std::vector<InstanceData> faces;
    LodChunkMesh::buildForLevel(*c, 0, SquashConfig{}, faces);
    EXPECT_GT(faces.size(), 0u)
        << "a single microcube vanished from the coarse mesh";
}

// Coverage must reflect HOW MUCH of the cube is filled, not just that something is there —
// that is the quantity the squash rules (thin-wall survival, HalfThreshold) reason about.
TEST(LodChunkMeshTest, CoverageReflectsSubCubeFillLevel) {
    auto full = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    full->initializeForLoading();
    full->addCube(glm::ivec3(1, 1, 1), "Stone");

    auto thin = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    thin->initializeForLoading();
    thin->addMicrocube(glm::ivec3(1, 1, 1), glm::ivec3(0, 0, 0), glm::ivec3(0, 0, 0), "Stone");

    const LodVolume vf = LodChunkMesh::volumeFromChunk(*full);
    const LodVolume vt = LodChunkMesh::volumeFromChunk(*thin);
    EXPECT_EQ(vf.at(1, 1, 1).coverage, LodVolume::kFullCoverage);
    EXPECT_GT(vt.at(1, 1, 1).coverage, 0u) << "microcube contributed no coverage";
    EXPECT_LT(vt.at(1, 1, 1).coverage, vf.at(1, 1, 1).coverage)
        << "one microcube must not read as a FULL cube — coverage is a quantity, not a flag";
}

// ---------------------------------------------------------------------------
// RED (visual defect found 2026-07-30): coarse terrain renders with long STRIPE-SHAPED GAPS.
// Full detail is solid; uniform level 2 is full of holes, so it is the coarse mesher, not
// mixed-level cracking. Turning the screenshot into a falsifiable invariant:
//
//   For a HEIGHTFIELD (terrain-like) chunk, every (x,z) column that contains solid voxels must
//   produce at least one UPWARD-facing (+Y, faceID 4) quad at every LOD level. A missing top
//   face is exactly a hole you can see the sky through from above.
// ---------------------------------------------------------------------------
TEST(LodChunkMeshTest, HeightfieldHasNoHolesFromAboveAtEveryLevel) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    // A sloped heightfield filling from y=0 up to a varying surface, like real terrain.
    auto heightAt = [](int x, int z) { return 6 + ((x / 3) + (z / 5)) % 9; };
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y <= heightAt(x, z); ++y)
                c->addCube(glm::ivec3(x, y, z), "Grass");

    for (int level = 0; level <= 3; ++level) {
        std::vector<InstanceData> faces;
        LodChunkMesh::buildForLevel(*c, level, SquashConfig{}, faces);
        const uint32_t cell = 1u << level;
        const int cells = 32 / int(cell);

        // Collect which (x,z) CELL columns got an upward face.
        std::set<std::pair<int,int>> topCovered;
        for (const auto& f : faces) {
            const uint32_t faceID = (f.packedData >> 15) & 0x7;
            if (faceID != 4u) continue;                    // +Y only
            const int x = int((f.packedData & 0x1F) / cell);
            const int z = int(((f.packedData >> 10) & 0x1F) / cell);
            topCovered.insert({x, z});
        }
        for (int cx = 0; cx < cells; ++cx)
            for (int cz = 0; cz < cells; ++cz)
                EXPECT_TRUE(topCovered.count({cx, cz}) > 0)
                    << "level " << level << ": cell column (" << cx << "," << cz
                    << ") has solid terrain but NO upward face -- a hole in the coarse surface";
    }
}

// ---------------------------------------------------------------------------
// RED: a SOLID cell must never be dropped because its material failed to resolve.
//
// emitFaces had `if (mat.empty()) continue;` — a solid cell whose palette entry is the empty
// string (id 0) emitted NO faces. Dropping solid geometry is never correct: the result is a
// hole you can see through, which is exactly the striping observed on the real world.
// ---------------------------------------------------------------------------
TEST(LodChunkMeshTest, SolidCellWithUnresolvedMaterialStillEmitsFaces) {
    // Drive emitFaces DIRECTLY. Going through Chunk::addCube cannot exercise this bug:
    // ChunkVoxelManager::addCube silently rewrites an empty material to "Default"
    // (ChunkVoxelManager.cpp:553), so the unresolved-material branch is never reached and the
    // test would pass on the broken code — a check named for something it cannot catch.
    // Hand-building the LodVolume is the only way to get a SOLID cell whose palette entry
    // is genuinely empty.
    for (int level = 0; level <= 2; ++level) {
        LodVolume v(glm::ivec3(4, 4, 4), level);
        LodCell& c = v.at(1, 1, 1);
        c.coverage = LodVolume::kFullCoverage;
        c.bulkMaterial = c.skinMaterial = 7;      // id 7 with a 1-entry palette => unresolvable

        std::vector<std::string> palette{""};     // only air; id 7 is out of range
        std::vector<InstanceData> faces;
        LodChunkMesh::emitFaces(v, palette, faces);
        EXPECT_EQ(faces.size(), 6u)
            << "level " << level << ": an isolated SOLID cell must emit all 6 faces even when its "
               "material cannot be resolved — dropping it is a see-through hole";
    }
}

// The same must hold when the solidity came from sub/microcubes.
TEST(LodChunkMeshTest, SolidSubCellWithUnresolvedMaterialStillEmitsFaces) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    for (int y = 0; y < 3; ++y)
        for (int z = 0; z < 3; ++z)
            c->addSubcube(glm::ivec3(4, y, z), glm::ivec3(1, 1, 1), "");
    std::vector<InstanceData> faces;
    LodChunkMesh::buildForLevel(*c, 0, SquashConfig{}, faces);
    EXPECT_GT(faces.size(), 0u) << "sub-cell solidity dropped for an unresolved material";
}

// ---------------------------------------------------------------------------
// RED: swapping in a coarse LOD mesh must update the DRAW COUNT.
//
// This is the striping defect. `numInstances` was assigned in exactly one place — the end of
// the fine rebuild — so setFacesFromLod() replaced faces[] but left numInstances at the FINE
// mesh's count. The renderer draws getNumInstances() instances, so:
//   * coarse faces > stale count -> the tail is never drawn  => see-through stripes
//   * coarse faces < stale count -> instances past the valid data are drawn from stale memory
// Flat terrain greedy-merges to very few fine faces, so its coarse mesh most easily exceeds
// that stale count -- which is exactly where the stripes appeared.
// ---------------------------------------------------------------------------
TEST(LodChunkMeshTest, SetLodFacesUpdatesDrawCount) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 4; ++y)
                c->addCube(glm::ivec3(x, y, z), "Stone");
    c->rebuildFaces();
    const uint32_t fineCount = c->getNumInstances();
    ASSERT_GT(fineCount, 0u);

    for (int level = 1; level <= 3; ++level) {
        std::vector<InstanceData> lod;
        LodChunkMesh::buildForLevel(*c, level, SquashConfig{}, lod);
        const size_t expected = lod.size();
        c->setLodFaces(std::move(lod), level);
        EXPECT_EQ(c->getNumInstances(), static_cast<uint32_t>(expected))
            << "level " << level << ": draw count is stale (fine count was " << fineCount
            << ") — the renderer would truncate or over-draw the coarse mesh";
        EXPECT_EQ(c->getFaces().size(), expected);
    }
}

// ---------------------------------------------------------------------------
// RED: the tracked LOD level must describe the mesh that was ACTUALLY built.
//
// POST /api/debug/lod_level rebuilt each chunk's mesh but never recorded the new level, so a
// chunk could report level 2 while holding the fine mesh. updateChunkLod() compares wanted-vs-
// current and `continue`s when they match, so distance-driven LOD then skipped every chunk and
// silently did nothing -- while chunks_by_level cheerfully reported {1:21, 2:359}.
// ---------------------------------------------------------------------------
TEST(LodChunkMeshTest, LodLevelTracksTheMeshActuallyBuilt) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 6; ++y)
                c->addCube(glm::ivec3(x, y, z), "Stone");

    c->rebuildFaces();
    EXPECT_EQ(c->getLodLevel(), 0) << "a fine rebuild is level 0";

    for (int level = 1; level <= 3; ++level) {
        std::vector<InstanceData> lod;
        LodChunkMesh::buildForLevel(*c, level, SquashConfig{}, lod);
        c->setLodFaces(std::move(lod), level);
        EXPECT_EQ(c->getLodLevel(), level)
            << "tracked level disagrees with the mesh just built -- updateChunkLod would skip "
               "this chunk forever";
    }

    // Returning to full detail must reset it, or the chunk stays "coarse" to the selector.
    c->rebuildFaces();
    EXPECT_EQ(c->getLodLevel(), 0)
        << "rebuildFaces() left a stale coarse level behind";
}
