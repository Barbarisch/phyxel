// ChunkTransparentCullingTest.cpp — opaque faces behind glass must be drawn.
//
// docs/GlassTransparency.md §17. Every mesher drops a voxel face when the neighbouring cell is
// occupied. That is right when the neighbour is opaque and wrong when it is glass: with glass now
// genuinely see-through, the stone lining a window opening (its "reveal") is simply missing.
//
// THE RULE (§17.2). A face of voxel A pointing at neighbour cell B is hidden iff
//     B is occupied AND (B is opaque OR A is transparent)
// so opaque-behind-glass is drawn, glass-against-glass stays culled (no stacked layers inside a
// thick pane), and glass-against-stone stays culled (the stone face is what you see).
//
// HOW THIS MEASURES. Every chunk's real emitted instances are expanded into the unit faces they
// cover at microcube resolution (graphics/FaceCoverage.h, the same decoder the live debug route
// uses). That count does not depend on greedy merging, so "is this face drawn" has one answer.
// Cross-chunk cases run a real headless ChunkManager (the ChunkSealedTest pattern): the real
// managed rebuild, the real neighbour lookup and the real edit routes.
//
// Each RED test says, in its failure message, what the missing face means on screen.

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "core/MaterialRegistry.h"
#include "graphics/ChunkRenderManager.h"
#include "graphics/FaceCoverage.h"
#include "physics/PhysicsWorld.h"

using namespace Phyxel;
using Phyxel::Graphics::ChunkRenderManager;
using Phyxel::Graphics::CoveredUnitFace;
using Phyxel::Graphics::expandCoveredUnitFaces;

namespace {

constexpr int kPZ = 0, kNZ = 1, kPX = 2, kNX = 3, kPY = 4, kNY = 5;   // face IDs

std::string findMaterialsJson() {
    for (const auto& path : {"resources/materials.json", "../resources/materials.json",
                             "../../resources/materials.json", "../../../resources/materials.json"}) {
        if (std::filesystem::exists(path)) return path;
    }
    return "resources/materials.json";
}

// A world-space unit face: owning micro cell in WORLD micro coordinates + direction.
using WorldFace = std::tuple<int, int, int, int>;

std::set<WorldFace> worldFaces(const Chunk& c) {
    std::set<WorldFace> out;
    const glm::ivec3 o = c.getWorldOrigin() * 9;
    for (const auto& u : expandCoveredUnitFaces(c.getFaces()))
        out.insert({o.x + u.x, o.y + u.y, o.z + u.z, u.faceID});
    return out;
}

// Unit faces with direction `faceID` whose owner lies in the WORLD micro box [lo, lo + size).
int countIn(const std::set<WorldFace>& s, const glm::ivec3& lo, int size, int faceID) {
    int n = 0;
    for (const auto& [x, y, z, f] : s)
        if (f == faceID && x >= lo.x && x < lo.x + size && y >= lo.y && y < lo.y + size &&
            z >= lo.z && z < lo.z + size)
            ++n;
    return n;
}
// World micro box of a cube cell, a subcube cell, a microcube cell.
glm::ivec3 cubeBox(const glm::ivec3& w) { return w * 9; }
glm::ivec3 subBox(const glm::ivec3& w, const glm::ivec3& s) { return w * 9 + s * 3; }
glm::ivec3 microBox(const glm::ivec3& w, const glm::ivec3& s, const glm::ivec3& m) {
    return w * 9 + s * 3 + m;
}

struct FineMergeScope {
    bool prev_;
    explicit FineMergeScope(bool on) : prev_(ChunkRenderManager::getFineGreedyMerge()) {
        ChunkRenderManager::setFineGreedyMerge(on);
    }
    ~FineMergeScope() { ChunkRenderManager::setFineGreedyMerge(prev_); }
};

struct FoliageScope {
    bool prevOn_; float prevDensity_;
    FoliageScope() : prevOn_(ChunkRenderManager::getFoliageEnabled()),
                     prevDensity_(ChunkRenderManager::getFoliageDensity()) {
        ChunkRenderManager::setFoliageEnabled(true);
        ChunkRenderManager::setFoliageDensity(1.0f);   // no hash thinning: exposure alone decides
    }
    ~FoliageScope() {
        ChunkRenderManager::setFoliageEnabled(prevOn_);
        ChunkRenderManager::setFoliageDensity(prevDensity_);
    }
};

}  // namespace

// =================================================================================================
// In-chunk: one standalone chunk at the origin, cells kept away from its borders.
// =================================================================================================
class TransparentCullingInChunk : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(Core::MaterialRegistry::instance().loadFromJson(findMaterialsJson()));
        ASSERT_TRUE(Core::isTransparentMaterial(Core::MaterialRegistry::instance().getMaterial("Glass")))
            << "control: Glass must be the transparent material these tests are about";
        ASSERT_FALSE(Core::isTransparentMaterial(Core::MaterialRegistry::instance().getMaterial("Stone")));
        chunk = std::make_unique<Chunk>(glm::ivec3(0));
        chunk->initializeForLoading();
    }
    std::set<WorldFace> mesh() { chunk->rebuildFaces(); return worldFaces(*chunk); }
    std::unique_ptr<Chunk> chunk;
};

const glm::ivec3 A(10, 10, 10), B(11, 10, 10), C(12, 10, 10);

// ---- controls ----------------------------------------------------------------------------------
TEST_F(TransparentCullingInChunk, T0_StoneAgainstStoneIsCulled) {
    chunk->addCube(A, "Stone"); chunk->addCube(B, "Stone");
    EXPECT_EQ(countIn(mesh(), cubeBox(A), 9, kPX), 0) << "control: opaque against opaque stays culled";
}
TEST_F(TransparentCullingInChunk, T0b_StoneAgainstAirIsDrawn) {
    chunk->addCube(A, "Stone");
    EXPECT_EQ(countIn(mesh(), cubeBox(A), 9, kPX), 81) << "control: a full cube face is 81 unit faces";
}

// ---- the fix -----------------------------------------------------------------------------------
TEST_F(TransparentCullingInChunk, T1_StoneFaceBehindGlassCubeIsDrawn) {
    chunk->addCube(A, "Stone"); chunk->addCube(B, "Glass");
    EXPECT_EQ(countIn(mesh(), cubeBox(A), 9, kPX), 81)
        << "the stone face behind a glass cube is missing: looking through the pane shows a hole";
}

// ---- must stay culled --------------------------------------------------------------------------
TEST_F(TransparentCullingInChunk, T2_GlassAgainstGlassStaysCulled_ThreeThick) {
    chunk->addCube(A, "Glass"); chunk->addCube(B, "Glass"); chunk->addCube(C, "Glass");
    auto s = mesh();
    EXPECT_EQ(countIn(s, cubeBox(A), 9, kPX), 0) << "interior pane face drawn: OIT layers stack";
    EXPECT_EQ(countIn(s, cubeBox(B), 9, kNX), 0);
    EXPECT_EQ(countIn(s, cubeBox(B), 9, kPX), 0);
    EXPECT_EQ(countIn(s, cubeBox(C), 9, kNX), 0);
    EXPECT_EQ(countIn(s, cubeBox(A), 9, kNX), 81) << "control: the pane's outer faces are drawn";
    EXPECT_EQ(countIn(s, cubeBox(C), 9, kPX), 81);
}
TEST_F(TransparentCullingInChunk, T3_GlassFaceAgainstStoneStaysCulled) {
    chunk->addCube(A, "Glass"); chunk->addCube(B, "Stone");
    EXPECT_EQ(countIn(mesh(), cubeBox(A), 9, kPX), 0)
        << "a glass face lying on stone would tint the stone a second time";
}

// ---- sub-voxels --------------------------------------------------------------------------------
TEST_F(TransparentCullingInChunk, T4a_StoneSubcubeBehindGlassSubcubeIsDrawn) {
    chunk->addSubcube(A, {2, 1, 1}, "Stone"); chunk->addSubcube(B, {0, 1, 1}, "Glass");
    EXPECT_EQ(countIn(mesh(), subBox(A, {2, 1, 1}), 3, kPX), 9)
        << "stone subcube face behind a glass subcube is missing";
}
TEST_F(TransparentCullingInChunk, T4b_StoneSubcubeBehindGlassCubeIsDrawn) {
    chunk->addSubcube(A, {2, 1, 1}, "Stone"); chunk->addCube(B, "Glass");
    EXPECT_EQ(countIn(mesh(), subBox(A, {2, 1, 1}), 3, kPX), 9)
        << "stone subcube face behind a glass cube is missing";
}
TEST_F(TransparentCullingInChunk, T4c_GlassSubcubeAgainstGlassOrStoneStaysCulled) {
    chunk->addSubcube(A, {2, 1, 1}, "Glass"); chunk->addSubcube(B, {0, 1, 1}, "Glass");
    chunk->addSubcube(A, {2, 2, 1}, "Glass"); chunk->addSubcube(B, {0, 2, 1}, "Stone");
    auto s = mesh();
    EXPECT_EQ(countIn(s, subBox(A, {2, 1, 1}), 3, kPX), 0) << "glass|glass subcube face drawn";
    EXPECT_EQ(countIn(s, subBox(A, {2, 2, 1}), 3, kPX), 0) << "glass|stone subcube face drawn";
    EXPECT_EQ(countIn(s, subBox(B, {0, 2, 1}), 3, kNX), 9) << "the stone subcube behind it IS drawn";
}
TEST_F(TransparentCullingInChunk, T5a_StoneMicroBehindGlassMicroIsDrawn) {
    chunk->addMicrocube(A, {2, 1, 1}, {2, 1, 1}, "Stone");
    chunk->addMicrocube(B, {0, 1, 1}, {0, 1, 1}, "Glass");
    EXPECT_EQ(countIn(mesh(), microBox(A, {2, 1, 1}, {2, 1, 1}), 1, kPX), 1)
        << "stone microcube face behind a glass microcube is missing (generated window jambs)";
}
TEST_F(TransparentCullingInChunk, T5b_StoneMicroBehindGlassSubcubeAndCubeIsDrawn) {
    chunk->addMicrocube(A, {2, 1, 1}, {2, 1, 1}, "Stone");
    chunk->addSubcube(B, {0, 1, 1}, "Glass");
    EXPECT_EQ(countIn(mesh(), microBox(A, {2, 1, 1}, {2, 1, 1}), 1, kPX), 1)
        << "stone microcube face behind a glass subcube is missing";

    chunk = std::make_unique<Chunk>(glm::ivec3(0)); chunk->initializeForLoading();
    chunk->addMicrocube(A, {2, 1, 1}, {2, 1, 1}, "Stone"); chunk->addCube(B, "Glass");
    EXPECT_EQ(countIn(mesh(), microBox(A, {2, 1, 1}, {2, 1, 1}), 1, kPX), 1)
        << "stone microcube face behind a glass cube is missing";
}
TEST_F(TransparentCullingInChunk, T5c_GlassMicroAgainstGlassMicroStaysCulled) {
    chunk->addMicrocube(A, {2, 1, 1}, {2, 1, 1}, "Glass");
    chunk->addMicrocube(B, {0, 1, 1}, {0, 1, 1}, "Glass");
    EXPECT_EQ(countIn(mesh(), microBox(A, {2, 1, 1}, {2, 1, 1}), 1, kPX), 0)
        << "glass|glass microcube face drawn: a 1-micro pane would double up";
}

// ---- merged and per-face paths agree -----------------------------------------------------------
TEST_F(TransparentCullingInChunk, T6_MergedAndPerFacePathsCoverTheSameFaces) {
    auto build = [&] {
        chunk = std::make_unique<Chunk>(glm::ivec3(0)); chunk->initializeForLoading();
        chunk->addCube(A, "Stone"); chunk->addCube(B, "Glass");
        chunk->addSubcube({10, 12, 10}, {2, 1, 1}, "Stone"); chunk->addSubcube({11, 12, 10}, {0, 1, 1}, "Glass");
        chunk->addMicrocube({10, 14, 10}, {2, 1, 1}, {2, 1, 1}, "Stone");
        chunk->addMicrocube({11, 14, 10}, {0, 1, 1}, {0, 1, 1}, "Glass");
        return mesh();
    };
    std::set<WorldFace> on, off;
    { FineMergeScope s(true);  on  = build(); }
    { FineMergeScope s(false); off = build(); }
    EXPECT_EQ(on, off) << "fine greedy merge ON and OFF must cover exactly the same unit faces";
}

// ---- KEEP: grass, foliage exposure, physics ----------------------------------------------------
TEST_F(TransparentCullingInChunk, T10_NoGrassUnderGlass) {
    chunk->addCube(A, "Grass");
    chunk->rebuildFaces();
    ASSERT_EQ(chunk->getGrassInstances().size(), 1u) << "control: an exposed grass top grows blades";
    chunk->addCube(A + glm::ivec3(0, 1, 0), "Glass");
    chunk->rebuildFaces();
    EXPECT_EQ(chunk->getGrassInstances().size(), 0u)
        << "grass would grow INSIDE the glass voxel: cover is physical, not visual";
}
TEST_F(TransparentCullingInChunk, T11_LeafBehindGlassIsExposed) {
    FoliageScope fs;
    const glm::ivec3 L(10, 10, 10);
    const glm::ivec3 d[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    chunk->addCube(L, "Leaf");
    for (int i = 0; i < 6; ++i) chunk->addCube(L + d[i], "Stone");
    chunk->rebuildFaces();
    ASSERT_EQ(chunk->getFoliageInstances().size(), 0u)
        << "control: a leaf enclosed by stone is not exposed";
    chunk->removeCube(L + d[2]);
    chunk->addCube(L + d[2], "Glass");
    chunk->rebuildFaces();
    EXPECT_EQ(chunk->getFoliageInstances().size(), 1u)
        << "a leaf seen through glass must emit its foliage cards";
}
TEST_F(TransparentCullingInChunk, T13_GlassIsStillAPhysicsSolid) {
    chunk->addCube(A, "Glass");
    EXPECT_TRUE(chunk->visibleSolidCubeAt(A)) << "glass must stay solid for physics (K1)";
}

// =================================================================================================
// Cross-chunk: a real headless ChunkManager.
// =================================================================================================
class TransparentCullingCrossChunk : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(Core::MaterialRegistry::instance().loadFromJson(findMaterialsJson()));
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    }
    Chunk* chunk(const glm::ivec3& coord) {
        cm.ensureChunkAt(coord * 32);
        return cm.getChunkAtCoord(coord);
    }
    void remesh(std::initializer_list<glm::ivec3> coords) {
        for (const auto& c : coords) cm.rebuildChunkFacesWithCrosschunkCulling(*chunk(c));
    }
    void drain() { for (int i = 0; i < 4; ++i) cm.updateDirtyChunks(); }

    Phyxel::Physics::PhysicsWorld physics;   // declared before cm (destruction order)
    ChunkManager cm;
};

TEST_F(TransparentCullingCrossChunk, T7_StoneFaceBehindGlassAcrossABorderIsDrawn) {
    Chunk* a = chunk({0, 0, 0}); Chunk* b = chunk({1, 0, 0});
    a->addCube({31, 10, 10}, "Stone");
    b->addCube({0, 10, 10}, "Stone");
    remesh({{0, 0, 0}, {1, 0, 0}});
    ASSERT_EQ(countIn(worldFaces(*a), cubeBox({31, 10, 10}), 9, kPX), 0)
        << "control: stone against stone across the border stays culled";
    b->removeCube({0, 10, 10}); b->addCube({0, 10, 10}, "Glass");
    remesh({{0, 0, 0}, {1, 0, 0}});
    EXPECT_EQ(countIn(worldFaces(*a), cubeBox({31, 10, 10}), 9, kPX), 81)
        << "stone face behind glass in the NEXT chunk is missing: a hole that appears only where "
           "a chunk border happens to fall";
}

TEST_F(TransparentCullingCrossChunk, T10b_NoGrassUnderGlassAcrossABorder) {
    Chunk* a = chunk({0, 0, 0}); chunk({0, 1, 0});
    a->addCube({10, 31, 10}, "Grass");
    remesh({{0, 0, 0}});
    ASSERT_EQ(a->getGrassInstances().size(), 1u) << "control";
    chunk({0, 1, 0})->addCube({10, 0, 10}, "Glass");
    remesh({{0, 0, 0}});
    EXPECT_EQ(a->getGrassInstances().size(), 0u) << "grass under glass in the chunk above";
}

// T8 — CHUNK INDEPENDENCE (design key). The same pattern, placed wholly inside one chunk and placed
// straddling a chunk border, must cover exactly the same faces (after translation). One pattern per
// voxel size, each a row along +X: stone, glass, glass, stone, air, glass.
namespace {
enum class Size { Cube, Sub, Micro };
const char* kRow[6] = {"Stone", "Glass", "Glass", "Stone", nullptr, "Glass"};
}  // namespace

class TransparentCullingChunkIndependence : public TransparentCullingCrossChunk,
                                            public ::testing::WithParamInterface<std::tuple<Size, int>> {
protected:
    // Place the row starting at world coordinate `base` (in units of the voxel size), y = z = 10 cubes.
    void place(Size sz, int base) {
        for (int k = 0; k < 6; ++k) {
            if (!kRow[k]) continue;
            const int x = base + k;
            if (sz == Size::Cube) {
                glm::ivec3 w(x, 10, 10);
                Chunk* c = chunk({w.x >= 32 ? 1 : 0, 0, 0});
                c->addCube(w - c->getWorldOrigin(), kRow[k]);
            } else if (sz == Size::Sub) {
                const int cx = x / 3, sx = x % 3;
                glm::ivec3 w(cx, 10, 10);
                Chunk* c = chunk({w.x >= 32 ? 1 : 0, 0, 0});
                c->addSubcube(w - c->getWorldOrigin(), {sx, 1, 1}, kRow[k]);
            } else {
                const int cx = x / 9, sx = (x % 9) / 3, mx = x % 3;
                glm::ivec3 w(cx, 10, 10);
                Chunk* c = chunk({w.x >= 32 ? 1 : 0, 0, 0});
                c->addMicrocube(w - c->getWorldOrigin(), {sx, 1, 1}, {mx, 1, 1}, kRow[k]);
            }
        }
    }
    std::set<WorldFace> meshAndCollect(int shiftMicroX) {
        remesh({{0, 0, 0}, {1, 0, 0}});
        std::set<WorldFace> out;
        for (auto& f : worldFaces(*chunk({0, 0, 0}))) out.insert({std::get<0>(f) - shiftMicroX, std::get<1>(f), std::get<2>(f), std::get<3>(f)});
        for (auto& f : worldFaces(*chunk({1, 0, 0}))) out.insert({std::get<0>(f) - shiftMicroX, std::get<1>(f), std::get<2>(f), std::get<3>(f)});
        return out;
    }
};

TEST_P(TransparentCullingChunkIndependence, T8_StraddlingEqualsWhole) {
    const auto [sz, straddleBase] = GetParam();
    const int unit = sz == Size::Cube ? 9 : (sz == Size::Sub ? 3 : 1);   // micro units per voxel
    const int insideBase = sz == Size::Cube ? 10 : (sz == Size::Sub ? 30 : 90);

    place(sz, insideBase);
    const auto whole = meshAndCollect(insideBase * unit);
    chunk({0, 0, 0})->clearAll(); chunk({1, 0, 0})->clearAll();
    place(sz, straddleBase);
    const auto straddling = meshAndCollect(straddleBase * unit);

    ASSERT_FALSE(whole.empty()) << "control: the pattern produced faces";
    std::vector<WorldFace> missing, extra;
    std::set_difference(whole.begin(), whole.end(), straddling.begin(), straddling.end(), std::back_inserter(missing));
    std::set_difference(straddling.begin(), straddling.end(), whole.begin(), whole.end(), std::back_inserter(extra));
    EXPECT_TRUE(missing.empty()) << missing.size() << " unit faces drawn inside one chunk are MISSING "
                                 << "when the pattern straddles a border (a hole at the chunk seam)";
    EXPECT_TRUE(extra.empty()) << extra.size() << " unit faces are drawn ONLY when the pattern straddles "
                               << "a border (for glass: a doubled layer at the chunk seam)";
}

// Straddle bases put the chunk border between the row's cells 1|2 (glass|glass), 2|3 (glass|stone)
// and 0|1 (stone|glass), for every voxel size.
INSTANTIATE_TEST_SUITE_P(
    AllSizesAndSeams, TransparentCullingChunkIndependence,
    ::testing::Values(std::make_tuple(Size::Cube, 30), std::make_tuple(Size::Cube, 29), std::make_tuple(Size::Cube, 31),
                      std::make_tuple(Size::Sub, 94), std::make_tuple(Size::Sub, 93), std::make_tuple(Size::Sub, 95),
                      std::make_tuple(Size::Micro, 286), std::make_tuple(Size::Micro, 285), std::make_tuple(Size::Micro, 287)));

// T12 — a glass layer must not SEAL the chunk behind it (a sealed chunk skips meshing its wall).
TEST_F(TransparentCullingCrossChunk, T12_GlassNeighbourLayerDoesNotSeal) {
    const glm::ivec3 d[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    chunk({0, 0, 0})->fillAllCubes("Stone");
    for (int i = 0; i < 6; ++i) chunk(d[i])->fillAllCubes(i == 0 ? "Glass" : "Stone");
    remesh({{0, 0, 0}});
    EXPECT_FALSE(chunk({0, 0, 0})->isSealed())
        << "a chunk capped by GLASS was sealed: its wall is not meshed and shows as a hole through the glass";
    EXPECT_EQ(countIn(worldFaces(*chunk({0, 0, 0})), cubeBox({31, 10, 10}), 9, kPX), 81);
}

// T12b — a sealed chunk must unseal when a cell of the layer capping it turns to glass.
TEST_F(TransparentCullingCrossChunk, T12b_SealedChunkUnsealsWhenCapTurnsToGlass) {
    const glm::ivec3 d[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    chunk({0, 0, 0})->fillAllCubes("Stone");
    for (int i = 0; i < 6; ++i) chunk(d[i])->fillAllCubes("Stone");
    remesh({{0, 0, 0}});
    for (int i = 0; i < 32; ++i) cm.updateDirtyChunks();   // settle load-time idle re-meshes (see T9 setup)
    ASSERT_TRUE(chunk({0, 0, 0})->isSealed()) << "control: an all-stone neighbourhood seals";
    const glm::ivec3 cap(32, 10, 10);
    ASSERT_TRUE(cm.removeCubeFast(cap));
    ASSERT_TRUE(cm.m_voxelModificationSystem.addCubeWithMaterial(cap, "Glass"));
    drain();
    EXPECT_FALSE(chunk({0, 0, 0})->isSealed()) << "still sealed behind the new glass cell";
    EXPECT_EQ(countIn(worldFaces(*chunk({0, 0, 0})), cubeBox({31, 10, 10}), 9, kPX), 81)
        << "the wall face behind the new glass cell is missing";
}

// T9 — EDIT-ROUTE MATRIX (§17.6). A border cell in chunk B changes through one edit route; after the
// dirty pass, chunk A's facing face must match what a from-scratch rebuild would give.
namespace {
enum class Route { R2_Api, R3_Fast, R4_FastAdd, R6_DirectRebuild, R7_BatchDirty };
const char* routeName(Route r) {
    switch (r) {
        case Route::R2_Api: return "R2 addCubeWithMaterial/removeCube";
        case Route::R3_Fast: return "R3 removeCubeFast";
        case Route::R4_FastAdd: return "R4 addCubeFast";
        case Route::R6_DirectRebuild: return "R6 direct chunk edit + rebuildFaces()";
        case Route::R7_BatchDirty: return "R7 chunk batch edit + markChunkDirty(own)";
    }
    return "?";
}
}  // namespace

class TransparentCullingRoutes : public TransparentCullingCrossChunk,
                                 public ::testing::WithParamInterface<Route> {
protected:
    const glm::ivec3 aCell{31, 10, 10};
    const glm::ivec3 bWorld{32, 10, 10}, bLocal{0, 10, 10};
    bool removeB(Route r) {
        Chunk* b = chunk({1, 0, 0});
        switch (r) {
            case Route::R2_Api: return cm.removeCube(bWorld);
            case Route::R3_Fast: return cm.removeCubeFast(bWorld);
            case Route::R6_DirectRebuild: { bool ok = b->removeCube(bLocal); r6Rebuild(b); return ok; }
            case Route::R7_BatchDirty: { int n = b->removeCubesBatch({bLocal}); cm.markChunkDirty(b); return n == 1; }
            default: return false;
        }
    }
    // R6 = the template/structure stamp's shape AS FIXED by §17.6 item 7: an immediate
    // null-lookup rebuild (what ObjectTemplateManager does for instant feedback) followed by a
    // MANAGED re-mesh mark. The mark alone never re-meshed the neighbour; the border ripple must.
    void r6Rebuild(Chunk* b) { b->rebuildFaces(); cm.markChunkForRemesh(b); }
    bool placeB(Route r, const std::string& mat) {
        Chunk* b = chunk({1, 0, 0});
        switch (r) {
            case Route::R2_Api: return cm.m_voxelModificationSystem.addCubeWithMaterial(bWorld, mat);
            case Route::R4_FastAdd: return cm.addCubeFast(bWorld);   // material "Default" (opaque)
            case Route::R6_DirectRebuild: { bool ok = b->addCube(bLocal, mat); r6Rebuild(b); return ok; }
            case Route::R7_BatchDirty: { bool ok = b->addCube(bLocal, mat); cm.markChunkDirty(b); return ok; }
            default: return false;
        }
    }
    int aFace() { return countIn(worldFaces(*chunk({0, 0, 0})), cubeBox(aCell), 9, kPX); }
    void setup(const char* aMat, const char* bMat) {
        chunk({0, 0, 0})->addCube(aCell, aMat);
        if (bMat) chunk({1, 0, 0})->addCube(bLocal, bMat);
        // B is never an all-AIR chunk: unrelated filler far from the border. An air chunk takes the
        // uniform short-circuit, and its first content is a different transition from an ordinary
        // border edit; the matrix measures the ordinary edit.
        chunk({1, 0, 0})->addCube({20, 20, 20}, "Stone");
        remesh({{0, 0, 0}, {1, 0, 0}});
        // SETTLE before the edit. A chunk's arrival queues an IDLE re-mesh of its neighbours
        // (ChunkManager.cpp:114-122), and the dirty pass promotes one idle chunk per quiet call.
        // Left pending, that load-time entry re-meshes A after the edit for a reason that has
        // nothing to do with the edit, and a route that never notifies its neighbour passes
        // (measured: T9a/T9b passed for R3/R7 on the first run for exactly this reason).
        for (int i = 0; i < 32; ++i) cm.updateDirtyChunks();
    }
};

TEST_P(TransparentCullingRoutes, T9a_RemoveStoneBesideStoneRevealsTheFace) {
    const Route r = GetParam();
    if (r == Route::R4_FastAdd) GTEST_SKIP() << "R4 only adds";
    setup("Stone", "Stone");
    ASSERT_EQ(aFace(), 0) << "control";
    ASSERT_TRUE(removeB(r));
    drain();
    EXPECT_EQ(aFace(), 81) << routeName(r) << ": the neighbour chunk was never re-meshed, so the stone face "
                                            "stays culled: a hole in the wall at the chunk border";
}
TEST_P(TransparentCullingRoutes, T9b_RemoveGlassBesideGlassRevealsTheGlassFace) {
    const Route r = GetParam();
    if (r == Route::R4_FastAdd) GTEST_SKIP() << "R4 only adds";
    setup("Glass", "Glass");
    ASSERT_EQ(aFace(), 0) << "control";
    ASSERT_TRUE(removeB(r));
    drain();
    EXPECT_EQ(aFace(), 81) << routeName(r) << ": a shattered pane on the border leaves the neighbour pane open";
}
TEST_P(TransparentCullingRoutes, T9c_PlaceGlassBesideGlassHidesTheFace) {
    const Route r = GetParam();
    if (r == Route::R3_Fast || r == Route::R4_FastAdd) GTEST_SKIP() << "route cannot place glass";
    setup("Glass", nullptr);
    ASSERT_EQ(aFace(), 81) << "control";
    ASSERT_TRUE(placeB(r, "Glass"));
    drain();
    EXPECT_EQ(aFace(), 0) << routeName(r) << ": two glass faces stacked at the chunk border (doubled smudges)";
}
TEST_P(TransparentCullingRoutes, T9d_PlaceStoneBesideGlassHidesTheGlassFace) {
    const Route r = GetParam();
    if (r == Route::R3_Fast) GTEST_SKIP() << "R3 only removes";
    setup("Glass", nullptr);
    ASSERT_EQ(aFace(), 81) << "control";
    ASSERT_TRUE(placeB(r, "Stone"));
    drain();
    EXPECT_EQ(aFace(), 0) << routeName(r) << ": the glass face on the stone is still drawn after the edit";
}
TEST_P(TransparentCullingRoutes, T9e_StoneFaceAppearsWhenItsNeighbourBecomesGlass) {
    const Route r = GetParam();
    if (r != Route::R2_Api && r != Route::R6_DirectRebuild && r != Route::R7_BatchDirty)
        GTEST_SKIP() << "needs remove + place-with-material";
    setup("Stone", "Stone");
    ASSERT_EQ(aFace(), 0) << "control";
    ASSERT_TRUE(removeB(r));
    ASSERT_TRUE(placeB(r, "Glass"));
    drain();
    EXPECT_EQ(aFace(), 81) << routeName(r) << ": stone behind the new glass cell is not drawn";
}

INSTANTIATE_TEST_SUITE_P(AllRoutes, TransparentCullingRoutes,
                         ::testing::Values(Route::R2_Api, Route::R3_Fast, Route::R4_FastAdd,
                                           Route::R6_DirectRebuild, Route::R7_BatchDirty));

// T14 — C7 CONVERGENCE AND GATING. The ripple must fire exactly when a border cell's render class
// changes, and never otherwise: not on a chunk's first build (that would cascade across the world
// on load), not on a re-mesh with unchanged content (it would loop), not on a same-class swap.
TEST_F(TransparentCullingCrossChunk, T14_BorderRippleFiresOnlyOnARenderClassChange) {
    Chunk* a = chunk({0, 0, 0}); Chunk* b = chunk({1, 0, 0});
    b->addCube({20, 20, 20}, "Stone");
    remesh({{0, 0, 0}, {1, 0, 0}});
    for (int i = 0; i < 32; ++i) cm.updateDirtyChunks();
    EXPECT_EQ(cm.borderRippleCount(), 0u) << "(a) first builds rippled: this cascades across the world on load";

    a->addCube({31, 10, 10}, "Stone");
    cm.markChunkDirty(a); drain();
    const size_t afterFirstBorderCell = cm.borderRippleCount();
    EXPECT_EQ(afterFirstBorderCell, 1u) << "control: a new border cell must ripple exactly once (one face)";

    remesh({{0, 0, 0}}); drain();
    EXPECT_EQ(cm.borderRippleCount(), afterFirstBorderCell) << "(b) a re-mesh with unchanged content rippled";

    a->removeCube({31, 10, 10}); a->addCube({31, 10, 10}, "Bricks");
    cm.markChunkDirty(a); drain();
    EXPECT_EQ(cm.borderRippleCount(), afterFirstBorderCell) << "(c) stone -> brick is the same render class";

    a->addCube({10, 10, 10}, "Glass");
    cm.markChunkDirty(a); drain();
    EXPECT_EQ(cm.borderRippleCount(), afterFirstBorderCell) << "(c) an INTERIOR edit rippled";

    const uint32_t bBefore = b->rebuildCount();
    a->removeCube({31, 10, 10}); a->addCube({31, 10, 10}, "Glass");
    cm.markChunkDirty(a); drain();
    EXPECT_EQ(cm.borderRippleCount(), afterFirstBorderCell + 1) << "(d) opaque -> transparent on the border must ripple once";
    EXPECT_GT(b->rebuildCount(), bBefore) << "(d) the facing neighbour was not re-meshed";
}

// T16 — C7's COST (plan §17.6 item 8: a rise over 5% of rebuild time is a finding). The border
// signature runs on every rebuild, so it is timed directly against a full rebuild of the same chunk.
// Two chunks: terrain (the common case) and a worst case for the signature relative to the mesh,
// a border layer made entirely of subcubes (the signature walks every sub/micro voxel).
TEST_F(TransparentCullingCrossChunk, T16_BorderSignatureCostsUnderFivePercentOfARebuild) {
    auto measure = [](Chunk* c, const char* label) {
        using clock = std::chrono::steady_clock;
        constexpr int kReps = 20;
        c->rebuildFaces();                              // warm
        const auto t0 = clock::now();
        for (int i = 0; i < kReps; ++i) c->rebuildFaces();
        const double rebuildMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count() / kReps;
        uint64_t sig[6];
        const auto t1 = clock::now();
        for (int i = 0; i < kReps; ++i) c->computeBorderSignature(sig);
        const double sigMs = std::chrono::duration<double, std::milli>(clock::now() - t1).count() / kReps;
        const double ratio = sigMs / rebuildMs;
        std::printf("[T16] %-22s rebuild %.3f ms  signature %.4f ms  ratio %.2f%%\n",
                    label, rebuildMs, sigMs, 100.0 * ratio);
        EXPECT_LT(ratio, 0.05) << label << ": the border signature costs " << 100.0 * ratio
                               << "% of a rebuild (budget 5%)";
    };
    Chunk* terrain = chunk({0, 0, 0});
    for (int x = 0; x < 32; ++x) for (int z = 0; z < 32; ++z) {
        const int top = 12 + (x * 7 + z * 3) % 6;       // rolling surface, stone under grass
        for (int y = 0; y < top; ++y) terrain->addCube({x, y, z}, "Stone");
        terrain->addCube({x, top, z}, "Grass");
    }
    measure(terrain, "terrain");

    Chunk* wall = chunk({2, 0, 0});
    for (int y = 0; y < 8; ++y) for (int z = 0; z < 32; ++z)
        for (int sx = 0; sx < 3; ++sx) for (int sy = 0; sy < 3; ++sy) for (int sz = 0; sz < 3; ++sz)
            wall->addSubcube({31, y, z}, {sx, sy, sz}, (y + z) % 3 ? "Stone" : "Glass");
    measure(wall, "subcube border wall");
}

// T9f — the same ripple for a SUB-VOXEL border change (generated panes are microcubes).
TEST_F(TransparentCullingCrossChunk, T9f_MicroGlassPlacedAcrossTheBorderRemeshesTheNeighbour) {
    Chunk* a = chunk({0, 0, 0}); Chunk* b = chunk({1, 0, 0});
    a->addMicrocube({31, 10, 10}, {2, 1, 1}, {2, 1, 1}, "Glass");
    b->addCube({20, 20, 20}, "Stone");   // B is not an all-air chunk (see T9 setup)
    remesh({{0, 0, 0}, {1, 0, 0}});
    for (int i = 0; i < 32; ++i) cm.updateDirtyChunks();   // settle load-time idle re-meshes (see T9 setup)
    ASSERT_EQ(countIn(worldFaces(*a), microBox({31, 10, 10}, {2, 1, 1}, {2, 1, 1}), 1, kPX), 1) << "control";
    b->addMicrocube({0, 10, 10}, {0, 1, 1}, {0, 1, 1}, "Glass");
    cm.markChunkDirty(b);
    drain();
    EXPECT_EQ(countIn(worldFaces(*a), microBox({31, 10, 10}, {2, 1, 1}, {2, 1, 1}), 1, kPX), 0)
        << "the neighbour was not re-meshed after a microcube edit across the border: doubled pane seam";
}

// T15 — a 1-micro glass pane straddling a chunk border (generated windows are exactly this).
TEST_F(TransparentCullingCrossChunk, T15_MicroGlassPaneAcrossABorderHasNoDoubledSeam) {
    Chunk* a = chunk({0, 0, 0}); Chunk* b = chunk({1, 0, 0});
    a->addMicrocube({31, 10, 10}, {2, 1, 1}, {2, 1, 1}, "Glass");
    b->addMicrocube({0, 10, 10},  {0, 1, 1}, {0, 1, 1}, "Glass");
    a->addMicrocube({31, 10, 10}, {2, 2, 1}, {2, 2, 1}, "Stone");   // a jamb micro beside the pane
    b->addMicrocube({0, 10, 10},  {0, 2, 1}, {0, 2, 1}, "Glass");
    remesh({{0, 0, 0}, {1, 0, 0}});
    auto sa = worldFaces(*a), sb = worldFaces(*b);
    EXPECT_EQ(countIn(sa, microBox({31, 10, 10}, {2, 1, 1}, {2, 1, 1}), 1, kPX), 0)
        << "glass micro face drawn against glass in the next chunk: a doubled seam through the pane";
    EXPECT_EQ(countIn(sb, microBox({32, 10, 10}, {0, 1, 1}, {0, 1, 1}), 1, kNX), 0);
    EXPECT_EQ(countIn(sa, microBox({31, 10, 10}, {2, 2, 1}, {2, 2, 1}), 1, kPX), 1)
        << "the stone jamb behind glass in the next chunk must be drawn";
    EXPECT_EQ(countIn(sb, microBox({32, 10, 10}, {0, 2, 1}, {0, 2, 1}), 1, kNX), 0)
        << "the glass micro lying on the stone jamb must stay culled";
}
