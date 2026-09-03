// LightBleedTest.cpp — does an interior light escape through a sub-voxel WALL?
//
// USER REPORT (2026-08-27, re-raised 2026-08-28 as "clearly we have something fucked up with the
// light system"): "a lit interior will result in the exterior facing side of the structure to be
// lit up, and that light carries out on the exterior of the house, even when there is no window
// there to spread that light."
//
// That observation rules out the forward point lights, which is what every previous explanation
// (mine included) blamed: pbrBRDF returns black at N·L <= 0 (voxel.frag:174-175), so an interior
// point light CANNOT light the outward face of its own wall. Baked block light can — it is added
// per-vertex with no normal test (voxel.frag:385) — and it only stops at cells marked
// m_lightOpaque, which requires kLightOpaqueFill = 243 of 729 micro-equivalents of fill
// (ChunkRenderManager.h:353, .cpp:474).
//
// Generated buildings do NOT build walls out of full cubes. StructureRealizer::thicknessMicro
// clamps a style thickness to [1,9] micro, and the shipped defaults are:
//     exterior_wall 0.333 cubes -> 3 micro -> fill 9*9*3 = 243  (EXACTLY the threshold)
//     interior_wall 0.222 cubes -> 2 micro -> fill 9*9*2 = 162  (BELOW it)
//     ceiling       0.222 cubes -> 2 micro -> fill 162          (BELOW it)
// LightBakeOcclusionTest covers the ROOF at subcube resolution; nothing has ever tested a
// sub-voxel WALL against an interior source. This does.
//
// THE VARIABLE is wall thickness in micro. THE CONTROLS are at both ends: a full-cube wall must
// hold the light in (proving the rig can see darkness), and the interior probe must be lit
// (proving the rig can see the light at all).
//
// PREDICTIONS, written before the first run:
//   cube wall (9 micro, fill 729) ....... exterior 0   — control, must block
//   3-micro wall (fill 243 == threshold)  exterior 0   — the exterior_wall default, on the knife edge
//   2-micro wall (fill 162 < threshold) . exterior > 0 — LEAKS: the interior_wall/ceiling default
//   1-micro wall (fill  81 < threshold) . exterior > 0 — LEAKS
//   3 micro STRADDLING a cube boundary .. exterior > 0 — LEAKS: 243 split into 162 + 81, neither
//                                                        cell reaching the bar. Same wall, same
//                                                        thickness, different alignment.
//
// GPU-free: the bake is pure CPU (rebuildAllFaces fills the light arrays, never touches Vulkan).

// ===========================================================================================
// ⚑ PARKED BY THE LIGHTING REBUILD (M0, 2026-08-29).
//
// The tests below marked DISABLED_ assert behaviour of the per-cell "flood" light field, which
// M0 DELETED (see ChunkRenderManager::rebuildCubeFaces). They are kept, not removed, because
// each states a REQUIREMENT the replacement has to meet:
//
//   * a sealed room admits no daylight            -> gate for M3 (sky as a traced emitter)
//   * a wall holds an interior light in           -> gate for M2 (traced point-light visibility)
//   * a sub-voxel roof/wall occludes at all       -> gate for M2 and M3
//
// Re-enable them as each milestone lands; they must pass against the new system unchanged, on
// the same geometry. If a replacement cannot satisfy one of these, that is a finding about the
// replacement, not a reason to weaken the test.
// ===========================================================================================

#include <gtest/gtest.h>

#include "graphics/ChunkRenderManager.h"
#include "core/MaterialRegistry.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::Graphics;
using Phyxel::Core::MaterialRegistry;

namespace {

// A sealed shell over [kMin,kMax]^3, interior air inside it, one glow cube at the centre.
// The -Z wall (z == kMin) is the one whose construction varies; every other face stays cubes so
// the only escape route under test is that wall.
constexpr int kMin = 6;
constexpr int kMax = 10;
constexpr int kCentre = 8;
constexpr int kOutside = kMin - 1;   // the air cell immediately outside the -Z wall

/// How the -Z wall is built. Micro variants pack a slab of microcubes of the given depth,
/// measured INTO the room from the wall cell's outer face.
enum class Wall {
    Cubes,          ///< control: full stone cubes. Must block.
    Micro3,         ///< the shipped exterior_wall default: 3 micro, fill 243 == threshold.
    Micro2,         ///< the shipped interior_wall / ceiling default: 2 micro, fill 162.
    Micro1,         ///< 1 micro, fill 81.
    Micro3Straddle, ///< 3 micro of wall SPLIT across two cube cells: 2 + 1, i.e. 162 and 81.
};

struct Room {
    std::vector<std::unique_ptr<Cube>>      cubes;
    std::vector<std::unique_ptr<Subcube>>   subs;
    std::vector<std::unique_ptr<Microcube>> micros;
};

/// Fill depth `d` micro layers of cube cell (x,y,z), counting from micro z-index `z0`.
void packMicroLayers(Room& r, int x, int y, int z, int z0, int d) {
    for (int mz = z0; mz < z0 + d && mz < 9; ++mz)
        for (int mx = 0; mx < 9; ++mx)
            for (int my = 0; my < 9; ++my)
                r.micros.push_back(std::make_unique<Microcube>(
                    glm::ivec3(x, y, z),
                    glm::ivec3(mx / 3, my / 3, mz / 3),
                    glm::ivec3(mx % 3, my % 3, mz % 3), "Stone"));
}

Room buildRoom(Wall wall) {
    Room r;
    auto shell = [](int x, int y, int z) {
        return x == kMin || x == kMax || y == kMin || y == kMax || z == kMin || z == kMax;
    };
    for (int x = kMin; x <= kMax; ++x)
    for (int y = kMin; y <= kMax; ++y)
    for (int z = kMin; z <= kMax; ++z) {
        if (!shell(x, y, z)) continue;
        // The -Z wall proper: the face cells, not the shared edges (corners stay cubes so the
        // shell can never spring a leak somewhere other than the surface under test).
        const bool isTestWall = (z == kMin) && x > kMin && x < kMax && y > kMin && y < kMax;
        if (isTestWall && wall != Wall::Cubes) {
            switch (wall) {
                case Wall::Micro3: packMicroLayers(r, x, y, z, 0, 3); break;
                case Wall::Micro2: packMicroLayers(r, x, y, z, 0, 2); break;
                case Wall::Micro1: packMicroLayers(r, x, y, z, 0, 1); break;
                case Wall::Micro3Straddle:
                    // The same 3 micro of masonry, offset so it spans the cube seam: the last
                    // 2 micro of the wall cell plus the first 1 micro of the cell behind it.
                    packMicroLayers(r, x, y, z, 7, 2);
                    packMicroLayers(r, x, y, z - 1, 0, 1);
                    break;
                default: break;
            }
            continue;
        }
        r.cubes.push_back(std::make_unique<Cube>(glm::ivec3(x, y, z), "Stone"));
    }
    // The source: one emissive cube in the middle of the sealed room.
    r.cubes.push_back(std::make_unique<Cube>(glm::ivec3(kCentre, kCentre, kCentre), "glow"));
    return r;
}

struct Probe { int inside = -1; int outside = -1; };

/// Bake, then read the block light just inside the room and just outside the test wall.
/// Block light is RGB; the brightest channel is what "is it lit" means here.
Probe bakeAndProbe(Wall wall, ChunkRenderManager& crm) {
    Room r = buildRoom(wall);
    crm.rebuildAllFaces(r.cubes, r.subs, r.micros, glm::ivec3(0, 0, 0));
    auto readMax = [&](int x, int y, int z) -> int {
        ChunkRenderManager::BakedLight out{};
        if (!crm.bakedLightAt(x, y, z, out)) return -1;
        return std::max({static_cast<int>(out.r), static_cast<int>(out.g),
                         static_cast<int>(out.b)});
    };
    Probe p;
    p.inside  = readMax(kCentre, kCentre, kMin + 1);   // air just inside the test wall
    p.outside = readMax(kCentre, kCentre, kOutside);   // air just outside it
    return p;
}

std::string findMaterialsJson() {
    for (const auto& path : {"resources/materials.json", "../resources/materials.json",
                             "../../resources/materials.json", "../../../resources/materials.json"})
        if (std::filesystem::exists(path)) return path;
    return "resources/materials.json";
}

class LightBleed : public ::testing::Test {
protected:
    void SetUp() override {
        loaded_ = MaterialRegistry::instance().loadFromJson(findMaterialsJson());
        ASSERT_TRUE(loaded_) << "materials.json did not load — the bake reads emissive off it";
    }
    bool loaded_ = false;
};

}  // namespace

// CONTROL, both ends. The rig must be able to see the light at all, and a full-cube wall must
// hold it in. Without this pair every result below is unreadable.
TEST_F(LightBleed, DISABLED_ControlACubeWallHoldsTheLightIn) {
    ChunkRenderManager crm;
    const Probe p = bakeAndProbe(Wall::Cubes, crm);
    ASSERT_GE(p.inside, 0) << "the interior cell was never baked — the rig is broken";
    EXPECT_GT(p.inside, 0) << "the sealed room is dark with a glow cube in it — the rig cannot "
                              "see block light, so nothing below means anything";
    EXPECT_EQ(p.outside, 0) << "light escaped a wall of SOLID CUBES — the leak is not about "
                               "sub-voxel thickness at all";
}

// The shipped exterior_wall default: 3 micro, fill exactly 243 == kLightOpaqueFill.
TEST_F(LightBleed, DISABLED_TheExteriorWallDefaultSitsExactlyOnTheThreshold) {
    ChunkRenderManager crm;
    const Probe p = bakeAndProbe(Wall::Micro3, crm);
    EXPECT_GT(p.inside, 0) << "the room is not lit";
    EXPECT_EQ(p.outside, 0)
        << "a 3-micro wall (fill 243, the exterior_wall default) leaks. It is EXACTLY on the "
           "kLightOpaqueFill bar, so the bar itself is the bug.";
}

// The shipped interior_wall and ceiling default: 2 micro, fill 162 — below the bar.
TEST_F(LightBleed, DISABLED_AnInteriorWallThicknessLeaks) {
    ChunkRenderManager crm;
    const Probe p = bakeAndProbe(Wall::Micro2, crm);
    EXPECT_GT(p.inside, 0) << "the room is not lit";
    EXPECT_EQ(p.outside, 0)
        << "REGRESSION: a 2-micro wall passes baked block light again. That is the shipped "
           "interior_wall and ceiling thickness AND timber_cottage's EXTERIOR wall, so light "
           "crosses partitions, floors and house fronts freely. Per-axis coverage is what fixes "
           "this; a fill-fraction bar cannot tell a thin wall from a knick-knack.";
}

TEST_F(LightBleed, DISABLED_AOneMicroWallLeaks) {
    ChunkRenderManager crm;
    const Probe p = bakeAndProbe(Wall::Micro1, crm);
    EXPECT_GT(p.inside, 0) << "the room is not lit";
    EXPECT_EQ(p.outside, 0) << "REGRESSION: a 1-micro wall passes baked block light again.";
}

// The nastiest case, because the wall is the RIGHT thickness and still leaks: the same 3 micro
// of masonry, offset so it spans a cube seam, splits into 162 + 81 and neither cell reaches the
// bar. Identical geometry, different alignment — which is a chunk/grid artifact leaking into
// appearance, the thing this engine explicitly forbids.
TEST_F(LightBleed, DISABLED_AWallOfTheRightThicknessLeaksWhenItStraddlesACubeSeam) {
    ChunkRenderManager crm;
    const Probe p = bakeAndProbe(Wall::Micro3Straddle, crm);
    EXPECT_GT(p.inside, 0) << "the room is not lit";
    EXPECT_EQ(p.outside, 0)
        << "REGRESSION: 3 micro of wall split across two cube cells passes light again, while the "
           "same 3 micro aligned to one cell does not — appearance depending on grid alignment is "
           "exactly what this engine forbids.";
}

// Diagnostic: print the whole profile in one place so the threshold behaviour is readable
// without running five tests and diffing failure text.
TEST_F(LightBleed, DiagnoseTheThresholdProfile) {
    struct Case { const char* name; Wall w; int fill; };
    const Case cases[] = {
        {"cube      (fill 729)", Wall::Cubes,          729},
        {"3 micro   (fill 243)", Wall::Micro3,         243},
        {"2 micro   (fill 162)", Wall::Micro2,         162},
        {"1 micro   (fill  81)", Wall::Micro1,          81},
        {"3 straddle(162 + 81)", Wall::Micro3Straddle, 243},
    };
    std::cout << "  kLightOpaqueFill = 243 of 729\n";
    for (const auto& c : cases) {
        ChunkRenderManager crm;
        const Probe p = bakeAndProbe(c.w, crm);
        std::cout << "  " << c.name << "  inside=" << p.inside
                  << "  OUTSIDE=" << p.outside
                  << (p.outside > 0 ? "   <-- LEAKS" : "") << "\n";
    }
}
