#include <gtest/gtest.h>

#include "core/AssetValidator.h"
#include "core/MicroCanvas.h"
#include "core/DimensionCanon.h"

using namespace Phyxel::Core;

namespace {
// A 1x1x1-cube archetype: simplest thing that exercises the size gate.
ArchetypeDims unitBox() {
    ArchetypeDims a;
    a.name = "test_box";
    a.tolerance = 0.1;
    a.values = {{"height", 1.0}, {"width", 1.0}, {"depth", 1.0}};
    return a;
}
// Only constrains height — lets us isolate connectivity/anchor gates.
ArchetypeDims heightOnly(double h, double tol = 0.15) {
    ArchetypeDims a;
    a.name = "test_h";
    a.tolerance = tol;
    a.values = {{"height", h}};
    return a;
}
} // namespace

TEST(AssetValidatorTest, GoodUnitCubePasses) {
    MicroCanvas c;
    c.addCube(0, 0, 0, "Wood");          // exactly 1x1x1, base on floor, one piece
    auto r = AssetValidator::validate(c, unitBox());
    EXPECT_TRUE(r.ok()) << r.summary();
}

TEST(AssetValidatorTest, EmptyAssetFails) {
    MicroCanvas c;
    auto r = AssetValidator::validate(c, unitBox());
    EXPECT_FALSE(r.ok());
}

TEST(AssetValidatorTest, WrongHeightFailsDimensionGate) {
    MicroCanvas c;
    c.addCube(0, 0, 0, "Wood");
    c.addCube(0, 1, 0, "Wood");          // 2 cubes tall vs canon 1.0
    auto r = AssetValidator::validate(c, unitBox());
    EXPECT_FALSE(r.ok());
}

TEST(AssetValidatorTest, FloatingAssetFails) {
    MicroCanvas c;
    c.addCube(0, 1, 0, "Wood");          // base at y=9 micro -> floats one cube
    auto r = AssetValidator::validate(c, heightOnly(1.0));
    bool floated = false;
    for (const auto& i : r.issues()) if (i.code == "not_on_floor") floated = true;
    EXPECT_TRUE(floated) << r.summary();
    EXPECT_FALSE(r.ok());
}

TEST(AssetValidatorTest, DisconnectedPartsFail) {
    MicroCanvas c;
    c.addCube(0, 0, 0, "Wood");
    c.addCube(2, 0, 0, "Wood");          // gap at cube x=1 -> two components
    auto r = AssetValidator::validate(c, heightOnly(1.0));
    EXPECT_EQ(AssetValidator::connectedComponents(c), 2);
    bool disc = false;
    for (const auto& i : r.issues()) if (i.code == "disconnected") disc = true;
    EXPECT_TRUE(disc) << r.summary();
}

TEST(AssetValidatorTest, JoinedPartsAreOneComponent) {
    MicroCanvas c;
    c.addCube(0, 0, 0, "Wood");
    c.addCube(1, 0, 0, "Wood");          // adjacent -> joined
    EXPECT_EQ(AssetValidator::connectedComponents(c), 1);
}

TEST(AssetValidatorTest, MissingRequiredAnchorFails) {
    ArchetypeDims chair = heightOnly(1.0);
    chair.name = "chair_dining";
    chair.anchors = {"seat_0"};
    MicroCanvas c;
    c.addCube(0, 0, 0, "Wood");

    auto missing = AssetValidator::validate(c, chair, /*anchors*/ {});
    EXPECT_FALSE(missing.ok());

    auto present = AssetValidator::validate(c, chair, {"seat_0"});
    EXPECT_TRUE(present.ok()) << present.summary();
}

TEST(AssetValidatorTest, OverBudgetIsWarningNotError) {
    MicroCanvas c;
    c.fillCubeBox(0, 0, 0, 4, 1, 4, "Wood");      // 16 cubes
    AssetValidator::Options opts;
    opts.maxVoxelBudget = 4;                       // force the warning
    auto r = AssetValidator::validate(c, heightOnly(1.0), {}, opts);
    EXPECT_TRUE(r.hasWarnings());
    EXPECT_TRUE(r.ok());                           // advisory only
}

// The user's headline example: a "picket fence" that comes out too tall must FAIL
// before anyone looks at it. fence_picket canon height = 0.9 cubes, tol 0.15.
TEST(AssetValidatorTest, PicketFenceHeightGate) {
    DimensionCanonRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(
        R"({ "fence_picket": { "category": "fence", "height": 0.9, "tol": 0.15 } })"));
    const ArchetypeDims* picket = reg.get("fence_picket");
    ASSERT_NE(picket, nullptr);

    MicroCanvas good;
    good.fillMicroBox(0, 0, 0, 18, 8, 1, "Wood");   // ~0.889 cubes tall -> within tol
    EXPECT_TRUE(AssetValidator::validate(good, *picket).ok());

    MicroCanvas tooTall;
    tooTall.fillMicroBox(0, 0, 0, 18, 14, 1, "Wood"); // ~1.556 cubes -> way off 0.9
    EXPECT_FALSE(AssetValidator::validate(tooTall, *picket).ok());
}
