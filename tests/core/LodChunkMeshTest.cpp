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

// ===========================================================================
// QUAD FOOTPRINT vs LEVEL — the measurement that was missing.
//
// Nothing measured the SIZE of the quad an isolated thin voxel produces once coarsened.
// CoverageReflectsSubCubeFillLevel only checks the coverage NUMBER at level 0. That gap is
// why a "~9x fattening" estimate went unchallenged when the real figure is level-dependent
// and reaches chunk scale.
//
// Helper: squash `src` to `level` under `cfg` and report whether the probe cell is still
// solid, plus the cell edge length in cubes at that level.
// ===========================================================================
namespace {
struct Footprint { bool solid; int cellCubes; };

Footprint footprintAt(const LodVolume& src, int level, const SquashConfig& cfg) {
    LodVolume v = src;
    for (int i = 0; i < level; ++i) v = squash(v, cfg);
    bool anySolid = false;
    for (int x = 0; x < v.dim().x && !anySolid; ++x)
        for (int y = 0; y < v.dim().y && !anySolid; ++y)
            for (int z = 0; z < v.dim().z && !anySolid; ++z)
                if (v.at(x, y, z).solid()) anySolid = true;
    return {anySolid, v.cellSizeInCubes()};
}

/// A 1-MICROCUBE-thick wall spanning one cube face: 9*9*1 = 81 of 729 microcubes = 11.1%.
/// This is the geometry the structure generator actually emits (timber_cottage interior_wall).
LodVolume thinWallVolume() {
    LodVolume v(glm::ivec3(8, 8, 8), 0);
    for (int y = 0; y < 8; ++y)
        for (int z = 0; z < 8; ++z) v.at(4, y, z).coverage = 81;   // 11.1% per cube
    return v;
}
LodVolume isolatedMicrocubeVolume() {
    LodVolume v(glm::ivec3(8, 8, 8), 0);
    v.at(3, 3, 3).coverage = 1;                                    // 1 of 729
    return v;
}
LodVolume solidBlockVolume() {
    LodVolume v(glm::ivec3(8, 8, 8), 0);
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
            for (int z = 0; z < 8; ++z) v.at(x, y, z).coverage = LodVolume::kFullCoverage;
    return v;
}
} // namespace

// INVARIANT WE REQUIRE: the generator's 1-microcube wall must SURVIVE coarsening. If it
// vanishes, buildings become invisible at distance -- a worse failure than fattening.
// This is what makes HalfThreshold unusable as a default, per LodBrick.h's own note.
TEST(LodQuadFootprintTest, ThinWallSurvivesUnderEveryOrFamilyRule) {
    const LodVolume wall = thinWallVolume();
    for (OccupancyRule rule : {OccupancyRule::Or, OccupancyRule::OrPreserveOpenings,
                               OccupancyRule::OrWithOpeningMask}) {
        SquashConfig cfg; cfg.occupancy = rule;
        for (int level = 1; level <= 3; ++level) {
            EXPECT_TRUE(footprintAt(wall, level, cfg).solid)
                << "rule " << int(rule) << " level " << level
                << ": the 1-microcube generator wall VANISHED -- buildings would be invisible";
        }
    }
}

// The documented reason HalfThreshold is not a candidate: it deletes that same wall.
// Pinning it so nobody "fixes" the fattening by switching to it (I nearly proposed exactly that).
TEST(LodQuadFootprintTest, HalfThresholdDeletesTheGeneratorWall) {
    SquashConfig cfg; cfg.occupancy = OccupancyRule::HalfThreshold;
    EXPECT_FALSE(footprintAt(thinWallVolume(), 1, cfg).solid)
        << "HalfThreshold kept an 11.1%-coverage wall -- if this now passes, the rule changed "
           "and it may have become a viable default; re-evaluate";
    EXPECT_TRUE(footprintAt(solidBlockVolume(), 3, cfg).solid) << "solid mass must always survive";
}

// Solid mass survives under every rule -- the control.
TEST(LodQuadFootprintTest, SolidMassSurvivesEveryRule) {
    for (OccupancyRule rule : {OccupancyRule::Or, OccupancyRule::HalfThreshold,
                               OccupancyRule::OrPreserveOpenings,
                               OccupancyRule::OrWithOpeningMask}) {
        SquashConfig cfg; cfg.occupancy = rule;
        EXPECT_TRUE(footprintAt(solidBlockVolume(), 3, cfg).solid) << "rule " << int(rule);
    }
}

// MEASURES THE DEFECT. A lone microcube (1/729) stays solid at every level, so the emitted
// quad grows with the cell while the geometry does not: 2 cubes at level 1 (18x the
// microcube's 1/9-cube edge) up to 32 cubes at level 5 (288x). Unbounded upward because the
// OR family never dilutes coverage.
TEST(LodQuadFootprintTest, IsolatedMicrocubeFattensWithLevel_KNOWN_DEFECT) {
    SquashConfig cfg;   // the shipped default
    const LodVolume speck = isolatedMicrocubeVolume();
    for (int level = 1; level <= 3; ++level) {
        Footprint f = footprintAt(speck, level, cfg);
        EXPECT_TRUE(f.solid) << "level " << level;
        EXPECT_EQ(f.cellCubes, 1 << level);
        // linear fattening = cell edge in cubes / (1/9 cube) = cellCubes * 9
        const int linearFatten = f.cellCubes * 9;
        EXPECT_GE(linearFatten, 18) << "level " << level << " fattening " << linearFatten << "x";
    }
}

// The invariant we WANT once the appearance tier (plan M2) consumes fractional coverage:
// a 1/729-coverage speck must not render as a multi-cube solid. Disabled because it is a
// known, recorded gap -- enable it when M2 lands. Left visible rather than silently absent.
TEST(LodQuadFootprintTest, DISABLED_IsolatedMicrocubeMustNotFatten_REQUIRES_M2) {
    SquashConfig cfg;
    Footprint f = footprintAt(isolatedMicrocubeVolume(), 3, cfg);
    EXPECT_FALSE(f.solid && f.cellCubes > 1)
        << "a 1/729 speck still emits a " << f.cellCubes << "-cube solid quad";
}

// ===========================================================================
// THE SHIPPED DEFAULT IS A RULE ITS OWN DOCS FORBID ABOVE LEVEL 1.
//
// LodBrick.h on OrPreserveOpenings: "Correct at level 1 (2 cubes) and CATASTROPHIC at brick
// sizes -- measured to erase 49.7% (4^3) to 100% (16^3) of a settlement block. NOT recommended
// above level 1." But RenderCoordinator::updateChunkLod runs levelForDistance(..., maxLevel=5),
// so the LIVE renderer reaches 32-cube cells with that rule as the default SquashConfig.
//
// Mechanism: `solid = (cov > 0) && !anyOpening`, and preserveOpening propagates upward, so ONE
// authored window keeps blanking an ever-larger cell at every level.
// ===========================================================================
namespace {
/// A solid wall slab carrying ONE authored opening (a window) at a single cube.
LodVolume walledRoomWithOneWindow() {
    LodVolume v(glm::ivec3(8, 8, 8), 0);
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
            for (int z = 0; z < 8; ++z) v.at(x, y, z).coverage = LodVolume::kFullCoverage;
    LodCell& win = v.at(2, 2, 2);
    win.preserveOpening = true;
    win.openingCoverage = LodVolume::kFullCoverage / 2;
    return v;
}
size_t solidCellCount(const LodVolume& v) {
    size_t n = 0;
    for (int x = 0; x < v.dim().x; ++x)
        for (int y = 0; y < v.dim().y; ++y)
            for (int z = 0; z < v.dim().z; ++z) if (v.at(x, y, z).solid()) ++n;
    return n;
}
LodVolume squashTo(const LodVolume& src, int level, const SquashConfig& cfg) {
    LodVolume v = src;
    for (int i = 0; i < level; ++i) v = squash(v, cfg);
    return v;
}
} // namespace

TEST(LodQuadFootprintTest, OrPreserveOpeningsErasesSolidMassAsLevelRises) {
    SquashConfig cfg; cfg.occupancy = OccupancyRule::OrPreserveOpenings;
    const LodVolume room = walledRoomWithOneWindow();
    // 8^3 = 512 solid cells, ONE of which carries a window.
    ASSERT_EQ(solidCellCount(room), 512u);
    // level 3 collapses the whole 8^3 into a single cell -- and that cell inherits the window,
    // so the ENTIRE solid mass is erased by one authored opening.
    EXPECT_EQ(solidCellCount(squashTo(room, 3, cfg)), 0u)
        << "if this is non-zero the rule changed; re-check whether it is still unsafe above L1";
}

// The documented recommendation conserves the opening instead of deleting the wall.
TEST(LodQuadFootprintTest, OrWithOpeningMaskKeepsTheMassAndCarriesTheOpening) {
    SquashConfig cfg; cfg.occupancy = OccupancyRule::OrWithOpeningMask;
    const LodVolume room = walledRoomWithOneWindow();
    const LodVolume top = squashTo(room, 3, cfg);
    EXPECT_EQ(solidCellCount(top), 1u) << "the wall must survive coarsening";
    EXPECT_GT(top.at(0, 0, 0).openingCoverage, 0u)
        << "the authored opening must be CONSERVED upward, not silently dropped";
}

// The default the engine actually ships must be safe at the levels the renderer reaches.
TEST(LodQuadFootprintTest, DefaultSquashConfigIsSafeAtRendererMaxLevel) {
    SquashConfig shipped;   // default-constructed -- what every live call site uses
    const LodVolume room = walledRoomWithOneWindow();
    for (int level = 1; level <= 5; ++level) {   // updateChunkLod caps at maxLevel = 5
        EXPECT_GT(solidCellCount(squashTo(room, level, shipped)), 0u)
            << "level " << level << ": the DEFAULT rule erased an entire walled room because it "
               "contained one window -- this is what the live renderer does at distance";
    }
}

// ===========================================================================
// THE NO-OP CLAIM, TESTED DIRECTLY.
//
// The default OccupancyRule changed OrPreserveOpenings -> OrWithOpeningMask. The claim is that
// this is a NO-OP for real chunks, because volumeFromChunk never authors preserveOpening /
// openingCoverage, so both rules reduce to `solid = cov > 0`.
//
// That was originally argued from cross-day face counts on LodBench -- a weak proxy, and it did
// not even hold to the digit (archived pre-fix level 1 was 409908 vs 410004 today; the delta is
// attributable to the C5 striping fix landing in between, but the archives cannot prove that).
// This asserts the property itself on a REAL chunk instead: identical output at every level the
// renderer can reach. If a future structure path starts authoring openings, this test FAILS and
// tells you the two rules have diverged -- which is exactly when the choice starts to matter.
// ===========================================================================
TEST(LodQuadFootprintTest, BothOrRulesAgreeOnRealChunksBecauseNoOpeningsAreAuthored) {
    auto c = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    c->initializeForLoading();
    for (int x = 0; x < 32; ++x)                       // terrain mass
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 5; ++y) c->addCube(glm::ivec3(x, y, z), "Stone");
    for (int x = 8; x < 20; ++x)                       // a wall with a gap in it (a "doorway")
        for (int y = 5; y < 11; ++y)
            if (!(x >= 13 && x <= 15 && y < 9)) c->addCube(glm::ivec3(x, y, 10), "WoodPlanks");
    for (int y = 0; y < 3; ++y)                        // sub/micro detail
        for (int z = 0; z < 3; ++z) c->addSubcube(glm::ivec3(6, 6, 6), glm::ivec3(1, y, z), "Wood");
    c->addMicrocube(glm::ivec3(7, 6, 6), glm::ivec3(1, 1, 1), glm::ivec3(1, 1, 1), "Metal");

    SquashConfig preserve; preserve.occupancy = OccupancyRule::OrPreserveOpenings;
    SquashConfig mask;     mask.occupancy     = OccupancyRule::OrWithOpeningMask;

    for (int level = 1; level <= 5; ++level) {         // updateChunkLod caps at maxLevel = 5
        std::vector<InstanceData> a, b;
        LodChunkMesh::buildForLevel(*c, level, preserve, a);
        LodChunkMesh::buildForLevel(*c, level, mask, b);
        ASSERT_EQ(a.size(), b.size())
            << "level " << level << ": the two occupancy rules DISAGREE on a real chunk, so the "
               "default swap is NOT a no-op -- something now authors openings and the runtime "
               "impact must be re-measured";
        for (size_t i = 0; i < a.size(); ++i)
            EXPECT_EQ(a[i].packedData, b[i].packedData) << "level " << level << " face " << i;
    }
}
