#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"

namespace fs = std::filesystem;
using namespace Phyxel;
using namespace Phyxel::Core;

// ============================================================================
// REGISTERED-BBOX ACCURACY (the "reservation == registration == render" fix). placeTemplateMicro used
// to register computeTemplateBounds(floorDiv(worldMicro/9)) — a CUBE-anchored box that silently dropped
// the sub-cube MICRO-SPILL an off-grid (wall-inset) worldMicro pushes into the next cube. The overlap +
// chest-facing detectors and the V8 chimney-centering all read this box, so a wall-inset fixture read
// smaller / mis-centred than its real geometry (settlement proof: chest_closed_5 rendered z23..z24 but
// its registered box was z23 only — the Metal clasp at z24 was invisible to chest-facing).
//
// These tests pin computeMicroPlacedBounds against a template whose micro AABB, placed off-grid,
// straddles an extra cube — with a cube-aligned control proving the extra cube is the inset's spill,
// not a vacuous always-bigger box.
// ============================================================================

namespace {
fs::path writeTempTemplate(const std::string& name, const std::string& body) {
    auto path = fs::temp_directory_path() / (name + ".voxel");
    std::ofstream f(path);
    f << body;
    return path;
}
// A 2-cube-wide micro coffer: micro AABB x 0..10 (11 wide, >1 cube), y 0..5, z 0..4 (5 deep, <1 cube).
// Just the two extreme micro corners define that AABB — all computeMicroPlacedBounds needs.
//   M 0 0 0 0 0 0 0 0 0  -> micro (0,0,0)
//   M 1 0 0 0 1 1 1 2 1  -> micro (9+1, 3+2, 3+1) = (10,5,4)
const char* kCofferBody =
    "# name: test_coffer\n"
    "M 0 0 0 0 0 0 0 0 0 WoodWalnut\n"
    "M 1 0 0 0 1 1 1 2 1 WoodWalnut\n";
} // namespace

// GREEN: an off-grid worldMicro whose z-remainder pushes the 5-micro-deep coffer across a cube seam is
// registered as a 2-cube-deep box (the spill captured). RED baseline (teeth): the SAME template on a
// cube-aligned worldMicro registers 1 cube deep — so the +1 cube above is the inset's micro-spill, not
// a box that is always fatter. This is the exact chest_closed_5 case: worldMicro.z=214 -> cube 23 rem 7,
// a 5-deep body spilling to cube 24.
TEST(PlacedObjectBoundsTest, MicroSpillIsCapturedNotDroppedByCubeAnchor) {
    auto path = writeTempTemplate("test_coffer", kCofferBody);
    ObjectTemplateManager otm(nullptr, nullptr);
    ASSERT_TRUE(otm.loadTemplate(path.string()));
    PlacedObjectManager pom(nullptr, &otm, nullptr);

    // chest_closed_5's real placement: worldMicro (299,147,214), rot 180.
    auto [mn, mx] = pom.computeMicroPlacedBounds("test_coffer", glm::ivec3(299, 147, 214), 180);
    EXPECT_EQ(mn.z, 23);
    EXPECT_EQ(mx.z, 24) << "the 5-deep coffer at z-remainder 7 spills into cube 24 — the registered box "
                           "must include it (the clasp the old cube-anchored box dropped)";
    EXPECT_EQ(mx.z - mn.z + 1, 2) << "render-accurate depth is 2 cubes";
    // x spans 2 cubes (11 micro wide); rot 180 keeps the same extent (symmetric reflection).
    EXPECT_EQ(mn.x, 33);
    EXPECT_EQ(mx.x, 34);

    // RED baseline: cube-ALIGNED z (multiple of 9, no inset) -> the 5-deep coffer stays in ONE cube.
    // If computeMicroPlacedBounds were vacuously always-2-deep this would be 2 and the test above would
    // prove nothing.
    auto [an, ax] = pom.computeMicroPlacedBounds("test_coffer", glm::ivec3(297, 144, 207), 180);
    EXPECT_EQ(ax.z - an.z + 1, 1) << "cube-aligned (no inset) -> no spill; the extra cube above is the "
                                     "inset's micro-spill, which a cube-anchored bbox drops";
}

// rotation swaps which extent runs along x vs z, exactly as spawnTemplateMicro's rotMicro does — so the
// registered box tracks the render under rotation too (bed/chest long-axis turns).
TEST(PlacedObjectBoundsTest, RotationSwapsExtentsLikeRender) {
    auto path = writeTempTemplate("test_coffer", kCofferBody);
    ObjectTemplateManager otm(nullptr, nullptr);
    ASSERT_TRUE(otm.loadTemplate(path.string()));
    PlacedObjectManager pom(nullptr, &otm, nullptr);

    // Cube-aligned so rotation is the only variable. rot 0: 11-wide in x (2 cubes), 5-deep in z (1).
    auto [n0, x0] = pom.computeMicroPlacedBounds("test_coffer", glm::ivec3(0, 0, 0), 0);
    EXPECT_EQ(x0.x - n0.x + 1, 2) << "rot0: 11 micro wide -> 2 cubes in x";
    EXPECT_EQ(x0.z - n0.z + 1, 1) << "rot0: 5 micro deep -> 1 cube in z";
    // rot 90: the 11-wide extent turns onto z, the 5-deep onto x.
    auto [n9, x9] = pom.computeMicroPlacedBounds("test_coffer", glm::ivec3(0, 0, 0), 90);
    EXPECT_EQ(x9.x - n9.x + 1, 1) << "rot90: x takes the 5-micro (short) extent";
    EXPECT_EQ(x9.z - n9.z + 1, 2) << "rot90: z takes the 11-micro (long) extent";
}
