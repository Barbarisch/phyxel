// LightBakeOcclusionTest.cpp — does the skylight bake actually respect the geometry the player sees?
//
// RED-BEFORE-GREEN harness for the lighting revamp (P2, "make the light field tell the truth").
// Two defects are pinned here, both found by reading ChunkRenderManager::rebuildCubeFaces:
//
//   D5  `solidVis` is populated from CUBES ONLY (ChunkRenderManager.cpp, the cube scan). The
//       sub/microcube occupancy masks (m_subOcc/m_microOcc, buildSubMicroOccupancy) are built
//       AFTER the bake and feed face culling only — neither skylight BFS loop reads them. So a
//       roof built at subcube or microcube resolution is TRANSPARENT to skylight. Structure
//       generation builds heavily at sub-voxel resolution, so generated interiors are lit as if
//       roofless.
//
//   D6  `skyLightAt` falls back to `return 15` (full daylight) for any cell it cannot resolve —
//       out-of-chunk with no neighbour-light provider, or a neighbour whose light arrays were
//       released by clearForUniform(). The per-corner AO averaging samples cells in the air
//       cell's plane, so faces on a chunk seam pull full daylight into an interior. AO inverts
//       into a bright halo exactly where it should darken.
//
// The mesher/bake path is pure CPU — rebuildAllFaces fills `faces` and the m_skyLight/m_block*
// arrays and never touches Vulkan — so these are true GPU-free unit tests.
//
// Every case carries its CONTROL: an open-topped room must reach sky 15 (proving the rig can see
// light at all) and a CUBE roof must reach sky 0 (proving the rig can see darkness).

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

// ── Geometry of the test room ────────────────────────────────────────────────────────────────────
// A sealed shell over [kMin, kMax]^3 with interior air [kMin+1, kMax-1]^3. Small on purpose: one
// variable (what the roof is made of), one chunk, well inside the 32^3 bounds.
constexpr int kMin = 4;
constexpr int kMax = 8;
constexpr int kCentre = 6;   // interior centre — the probe cell

constexpr uint32_t FACE_PLUS_Y = 4u;

int posX(const InstanceData& f) { return static_cast<int>(f.packedData & 0x1Fu); }
int posY(const InstanceData& f) { return static_cast<int>((f.packedData >> 5) & 0x1Fu); }
int posZ(const InstanceData& f) { return static_cast<int>((f.packedData >> 10) & 0x1Fu); }
uint32_t faceIdOf(const InstanceData& f) { return (f.packedData >> 15) & 0x7u; }

// Per-corner skylight nibble: light bits 0-15 hold four 4-bit corner values (corner = vertexID & 3).
uint32_t cornerSky(const InstanceData& f, int corner) {
    return (f.light >> (static_cast<uint32_t>(corner) * 4u)) & 0xFu;
}
uint32_t maxCornerSky(const InstanceData& f) {
    uint32_t m = 0;
    for (int c = 0; c < 4; ++c) m = std::max(m, cornerSky(f, c));
    return m;
}

bool onShell(int x, int y, int z) {
    return x == kMin || x == kMax || y == kMin || y == kMax || z == kMin || z == kMax;
}
// The roof cells directly over the interior — the only part whose material/resolution varies.
bool isInteriorRoof(int x, int y, int z) {
    return y == kMax && x > kMin && x < kMax && z > kMin && z < kMax;
}

enum class Roof {
    Cubes,        // control: a solid cube roof. Interior MUST be dark.
    Open,         // control: no roof over the interior. Interior MUST be fully lit.
    Subcubes,     // D5: interior roof cells fully packed with 3x3x3 subcubes.
    Microcubes,   // D5: interior roof cells fully packed with 9x9x9 microcubes.
    Glass,        // interior roof cells are Glass cubes — should ADMIT light, currently blocks.
    SubcubeSlab,  // threshold: ONE subcube-thick layer (9 of 27) — a real 1/3-thick roof. Blocks.
    SubcubeSpeck, // threshold: a single decorative subcube (1 of 27). Must NOT block.
};

struct Room {
    std::vector<std::unique_ptr<Cube>>       cubes;
    std::vector<std::unique_ptr<Subcube>>    subs;
    std::vector<std::unique_ptr<Microcube>>  micros;
};

Room buildRoom(Roof roof) {
    Room r;
    for (int x = kMin; x <= kMax; ++x)
    for (int y = kMin; y <= kMax; ++y)
    for (int z = kMin; z <= kMax; ++z) {
        if (!onShell(x, y, z)) continue;

        if (isInteriorRoof(x, y, z)) {
            switch (roof) {
                case Roof::Open:
                    continue;                                   // leave the hole
                case Roof::Glass:
                    r.cubes.push_back(std::make_unique<Cube>(glm::ivec3(x, y, z), "Glass"));
                    continue;
                case Roof::Subcubes:
                    for (int sx = 0; sx < 3; ++sx)
                    for (int sy = 0; sy < 3; ++sy)
                    for (int sz = 0; sz < 3; ++sz)
                        r.subs.push_back(std::make_unique<Subcube>(
                            glm::ivec3(x, y, z), glm::ivec3(sx, sy, sz), "Stone"));
                    continue;
                case Roof::SubcubeSlab:
                    // The bottom 3x3 subcube layer only: a roof one subcube thick, which is what a
                    // sub-voxel-detailed structure actually builds. 9/27 of the cell volume.
                    for (int sx = 0; sx < 3; ++sx)
                    for (int sz = 0; sz < 3; ++sz)
                        r.subs.push_back(std::make_unique<Subcube>(
                            glm::ivec3(x, y, z), glm::ivec3(sx, 0, sz), "Stone"));
                    continue;
                case Roof::SubcubeSpeck:
                    // A lone decorative subcube — a hook, a knot, a nail head. 1/27 of the cell.
                    r.subs.push_back(std::make_unique<Subcube>(
                        glm::ivec3(x, y, z), glm::ivec3(1, 1, 1), "Stone"));
                    continue;
                case Roof::Microcubes:
                    for (int mx = 0; mx < 9; ++mx)
                    for (int my = 0; my < 9; ++my)
                    for (int mz = 0; mz < 9; ++mz)
                        r.micros.push_back(std::make_unique<Microcube>(
                            glm::ivec3(x, y, z),
                            glm::ivec3(mx / 3, my / 3, mz / 3),
                            glm::ivec3(mx % 3, my % 3, mz % 3), "Stone"));
                    continue;
                case Roof::Cubes:
                    break;                                       // fall through to a Stone cube
            }
        }
        r.cubes.push_back(std::make_unique<Cube>(glm::ivec3(x, y, z), "Stone"));
    }
    return r;
}

// Bake the room and report the skylight at the interior centre.
// getNeighborCube/getNeighborLight are both null: columnOpenAbove() then answers "open" for every
// column, so the sky is available from above and the ROOF is the only thing that can block it.
// That is the single variable under test.
uint8_t bakeAndProbeCentre(Roof roof, ChunkRenderManager& crm) {
    Room r = buildRoom(roof);
    crm.rebuildAllFaces(r.cubes, r.subs, r.micros, glm::ivec3(0, 0, 0));
    ChunkRenderManager::BakedLight out{};
    if (!crm.bakedLightAt(kCentre, kCentre, kCentre, out)) return 255;   // not baked at all
    return out.sky;
}

std::string findMaterialsJson() {
    for (const auto& path : {
        "resources/materials.json",
        "../resources/materials.json",
        "../../resources/materials.json",
        "../../../resources/materials.json"
    }) {
        if (std::filesystem::exists(path)) return path;
    }
    return "resources/materials.json";
}

class LightBakeOcclusion : public ::testing::Test {
protected:
    void SetUp() override {
        // The bake reads material flags (emissive, alpha, billboarded) off the singleton.
        loaded_ = MaterialRegistry::instance().loadFromJson(findMaterialsJson());
    }
    bool loaded_ = false;
};

} // namespace

// ── Controls: the rig can see both light and darkness ────────────────────────────────────────────

// POSITIVE control. Without a roof over the interior, the column seed walks straight down and the
// centre must read full sky. If this fails the rig is broken, not the engine.
TEST_F(LightBakeOcclusion, Control_OpenRoomInteriorIsFullyLit) {
    ASSERT_TRUE(loaded_);
    ChunkRenderManager crm;
    EXPECT_EQ(bakeAndProbeCentre(Roof::Open, crm), 15u);
}

// NEGATIVE control. A cube roof already occludes correctly — this is the behaviour the sub-voxel
// cases must match, and it passes today.
TEST_F(LightBakeOcclusion, Control_CubeRoofedRoomInteriorIsDark) {
    ASSERT_TRUE(loaded_);
    ChunkRenderManager crm;
    EXPECT_EQ(bakeAndProbeCentre(Roof::Cubes, crm), 0u);
}

// ── D5: sub-voxel geometry must occlude skylight ─────────────────────────────────────────────────

// RED until P2. A roof of fully-packed subcubes occupies exactly the same volume as a cube roof,
// so the interior must be equally dark. Today `solidVis` never sees the subcubes and the interior
// bakes to 15 — a subcube-roofed room is lit as if it had no roof at all.
TEST_F(LightBakeOcclusion, SubcubeRoofOccludesSkylight) {
    ASSERT_TRUE(loaded_);
    ChunkRenderManager crm;
    EXPECT_EQ(bakeAndProbeCentre(Roof::Subcubes, crm), 0u)
        << "A fully-packed subcube roof fills the same volume as a cube roof; it must block "
           "skylight identically. See D5 in the lighting revamp plan.";
}

// RED until P2. Same argument one resolution finer (9x9x9 microcubes per cell).
TEST_F(LightBakeOcclusion, MicrocubeRoofOccludesSkylight) {
    ASSERT_TRUE(loaded_);
    ChunkRenderManager crm;
    EXPECT_EQ(bakeAndProbeCentre(Roof::Microcubes, crm), 0u)
        << "A fully-packed microcube roof fills the same volume as a cube roof; it must block "
           "skylight identically. See D5 in the lighting revamp plan.";
}

// ── The occlusion threshold: what counts as "enough sub-voxel fill to block light" ──────────────
// Light opacity is decided by sub-voxel fill in micro-equivalents against kLightOpaqueFill = 243
// (729/3, the volume of one full subcube-thick slab). These two tests pin BOTH ends of that rule so
// it cannot drift into "any subcube seals a cell" (which would black out decorated interiors) or
// back into "sub-voxels never occlude".

// A roof exactly one subcube thick is 9/27 of the cell = 243 micro-equivalents = the threshold.
// This is the shape structure generation actually builds, so it must block.
TEST_F(LightBakeOcclusion, OneSubcubeThickRoofBlocksSkylight) {
    ASSERT_TRUE(loaded_);
    ChunkRenderManager crm;
    EXPECT_EQ(bakeAndProbeCentre(Roof::SubcubeSlab, crm), 0u)
        << "A roof one subcube thick occupies a full slab of each cell and must block skylight.";
}

// A single decorative subcube is 1/27 of the cell = 27 micro-equivalents, far below the threshold.
// It must leave the cell transparent, or every hook, knot and nail head would cast a hole of night.
TEST_F(LightBakeOcclusion, LoneDecorativeSubcubeDoesNotBlockSkylight) {
    ASSERT_TRUE(loaded_);
    ChunkRenderManager crm;
    EXPECT_EQ(bakeAndProbeCentre(Roof::SubcubeSpeck, crm), 15u)
        << "A lone decorative subcube must not seal its cell against light.";
}

// ── Transparency: glass is a window, not a wall ──────────────────────────────────────────────────

// RED until P2. `solidVis` is set for any visible cube regardless of material alpha, so a glass
// roof blocks skylight completely. A glazed room should be daylit.
TEST_F(LightBakeOcclusion, GlassRoofAdmitsSkylight) {
    ASSERT_TRUE(loaded_);
    const auto* glass = MaterialRegistry::instance().getMaterial("Glass");
    ASSERT_NE(glass, nullptr);
    ASSERT_LT(glass->alpha, 0.99f) << "test premise: Glass must be a transparent material";

    ChunkRenderManager crm;
    EXPECT_GT(bakeAndProbeCentre(Roof::Glass, crm), 0u)
        << "Skylight must pass through a transparent roof. See D5 in the lighting revamp plan.";
}

// ── D6: an unresolvable neighbour must not mean "outdoors" ───────────────────────────────────────

// RED until P2. A room whose interior air reaches the chunk boundary (its far wall lives in the
// neighbour chunk) is correctly baked DARK — the BFS finds no source. But the per-corner AO
// averaging samples the four cells touching each corner in the air cell's plane, and the samples
// that leave the chunk hit `skyLightAt`'s `return 15`. So the interior floor faces along x=0 and
// z=0 are shaded as if in full daylight while their neighbours two cells away are black.
TEST_F(LightBakeOcclusion, ChunkEdgeInteriorFacesDoNotReadFullSky) {
    ASSERT_TRUE(loaded_);

    // Floor + roof span x,z in [0, 8]; walls only at x=8 and z=8. The x=0 and z=0 sides are open
    // to the (absent) neighbour chunk. No neighbour-light provider is supplied, so no light may
    // legitimately enter from there.
    constexpr int kEdgeMax = 8;
    constexpr int kFloorY = 4;
    constexpr int kRoofY = 8;
    std::vector<std::unique_ptr<Cube>> cubes;
    for (int x = 0; x <= kEdgeMax; ++x)
    for (int z = 0; z <= kEdgeMax; ++z) {
        cubes.push_back(std::make_unique<Cube>(glm::ivec3(x, kFloorY, z), "Stone"));
        cubes.push_back(std::make_unique<Cube>(glm::ivec3(x, kRoofY,  z), "Stone"));
    }
    for (int y = kFloorY + 1; y < kRoofY; ++y) {
        for (int z = 0; z <= kEdgeMax; ++z)
            cubes.push_back(std::make_unique<Cube>(glm::ivec3(kEdgeMax, y, z), "Stone"));
        for (int x = 0; x < kEdgeMax; ++x)
            cubes.push_back(std::make_unique<Cube>(glm::ivec3(x, y, kEdgeMax), "Stone"));
    }

    std::vector<std::unique_ptr<Subcube>> subs;
    std::vector<std::unique_ptr<Microcube>> micros;
    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));

    // Premise: the BAKE itself is dark. If this fails the test is measuring the wrong thing.
    ChunkRenderManager::BakedLight probe{};
    ASSERT_TRUE(crm.bakedLightAt(1, kFloorY + 1, 1, probe));
    ASSERT_EQ(probe.sky, 0u) << "test premise: the sealed-within-chunk interior must bake dark";

    // The floor's +Y faces look into interior air. Every one of them must be fully unlit.
    int inspected = 0;
    int offenders = 0;
    uint32_t worst = 0;
    for (const auto& f : crm.getFaces()) {
        if (faceIdOf(f) != FACE_PLUS_Y || posY(f) != kFloorY) continue;
        // Only faces under the roof; the outer ring at x/z == kEdgeMax is exterior-facing.
        if (posX(f) >= kEdgeMax || posZ(f) >= kEdgeMax) continue;
        ++inspected;
        const uint32_t m = maxCornerSky(f);
        if (m != 0) { ++offenders; worst = std::max(worst, m); }
    }
    ASSERT_GT(inspected, 0) << "test premise: interior floor faces must be emitted";
    EXPECT_EQ(offenders, 0)
        << offenders << " of " << inspected << " interior floor faces carry skylight (worst corner "
        << worst << "/15). An out-of-chunk sample that cannot be resolved must not default to full "
           "daylight. See D6 in the lighting revamp plan.";
}
