// Structure-gen item placement — L2 (docs/structure-generation/ItemPlacementPlan.md).
//
// Contract: generated interiors place tableware/lighting as ITEM PROPS
// (pickable, static-first) on MEASURED fixture surfaces:
//  1. The per-purpose surface-item sets come from furnishing_recipes.json
//     ("surface_items") and every listed id resolves to a holdable
//     ItemDefinition (the ItemCatalog gate — twin of FurnitureCatalogTest).
//  2. Items sit at the fixture's MEASURED top surface (template geometry),
//     not the historical floorY+1 cube guess — a tavern table is ~0.78 u
//     tall, so the guess floated tankards ~0.22 u above the wood.
//  3. Structure-placed props are PARENTED to the structure, so removing /
//     rebuilding the structure removes its items (no duplicate accumulation).

#include <gtest/gtest.h>

#include "core/FurniturePlacer.h"
#include "core/ItemPropManager.h"
#include "core/ItemRegistry.h"
#include "core/ItemDefinition.h"
#include "core/KinematicVoxelManager.h"
#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"
#include "physics/VoxelDynamicsWorld.h"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace Phyxel;
using namespace Phyxel::Core;

TEST(ItemPlacement, SurfaceItemsRecipeResolvesToHoldableItems) {
    if (!fs::exists("resources/furnishing_recipes.json"))
        GTEST_SKIP() << "repo-root CWD required";
    ASSERT_TRUE(FurniturePlacer::loadRecipesFromFile("resources/furnishing_recipes.json"));
    ItemRegistry::instance().loadFromFile("resources/items.json");

    const auto taproom = FurniturePlacer::surfaceItemsFor("taproom");
    EXPECT_FALSE(taproom.empty()) << "taproom must have a surface-item set";
    for (const std::string& purpose : {"taproom", "kitchen", "bedchamber"}) {
        for (const auto& id : FurniturePlacer::surfaceItemsFor(purpose)) {
            const auto* def = ItemRegistry::instance().getItem(id);
            ASSERT_NE(def, nullptr) << purpose << " surface item '" << id
                                    << "' is not a registered item";
            EXPECT_TRUE(def->holdable) << id;
            EXPECT_FALSE(def->templateFile.empty()) << id;
            EXPECT_TRUE(fs::exists("resources/templates/" + def->templateFile))
                << id << " template missing on disk";
        }
    }
    // Recipes are a process-global registry: leaking them changed which recipe
    // (data vs hardcoded) LATER suites furnished with (bench-seating flake).
    FurniturePlacer::clearRecipes();
}

// `as:"item"` recipe realization (ItemPlacementPlan.md step 2): a recipe piece
// may declare it realizes as a pickable ITEM PROP instead of a baked template —
// rugs are the flagship (user rule: "rugs should be items, not static
// microcubes"). RED before the schema existed: itemFormFor was unknown/empty.
TEST(ItemPlacement, RugRecipeRealizesAsItemForm) {
    if (!fs::exists("resources/furnishing_recipes.json"))
        GTEST_SKIP() << "repo-root CWD required";
    FurniturePlacer::clearRecipes();
    ASSERT_TRUE(FurniturePlacer::loadRecipesFromFile("resources/furnishing_recipes.json"));
    EXPECT_EQ(FurniturePlacer::itemFormFor("rug"), "rug_woven")
        << "rug must realize as the rug_woven item prop";
    EXPECT_EQ(FurniturePlacer::itemFormFor("bed"), std::string())
        << "non-item pieces must stay baked templates";
    // The mapped id must be a registered, holdable item with a real template.
    ItemRegistry::instance().loadFromFile("resources/items.json");
    const auto* def = ItemRegistry::instance().getItem("rug_woven");
    ASSERT_NE(def, nullptr);
    EXPECT_TRUE(def->holdable);
    EXPECT_TRUE(fs::exists("resources/templates/" + def->templateFile));
    FurniturePlacer::clearRecipes();
}

TEST(ItemPlacement, MeasuredSurfaceHeightNotFloorPlusOne) {
    if (!fs::exists("resources/templates/furniture/tavern_table.voxel"))
        GTEST_SKIP() << "repo-root CWD required";
    ObjectTemplateManager mgr(nullptr, nullptr);
    ASSERT_TRUE(mgr.loadTemplate("resources/templates/furniture/tavern_table.voxel"));
    const auto* table = mgr.getTemplate("tavern_table");
    ASSERT_NE(table, nullptr);

    const float top = FurniturePlacer::templateTopUnits(*table);
    // The real tavern table is ~0.78 u tall (metrics sidecar surface_y 0.778).
    // The historical clutter pass guessed floor+1 CUBE — items floated.
    EXPECT_GT(top, 0.5f);
    EXPECT_LT(top, 0.95f) << "measured top must not be the 1-cube guess";
}

TEST(ItemPlacement, StructureRemovalCascadesItemProps) {
    // Cascade mechanics control: an item prop PARENTED to another placed
    // object dies with it (registry entry, kinematic render, manager entry).
    // This is what makes structure rebuilds idempotent for their items.
    ObjectTemplateManager templates(nullptr, nullptr);
    KinematicVoxelManager kvm;
    PlacedObjectManager placed(nullptr, &templates, nullptr);
    Physics::VoxelDynamicsWorld world;
    ItemPropManager props;
    props.setDependencies(&placed, &templates, &kvm, nullptr);
    props.setDynamicsWorld(&world);
    // Mirror the Application wiring: registry removal tears down the prop.
    placed.setPreRemoveCallback([&](const std::string& id) {
        props.onPlacedObjectRemoved(id);
    });

    auto path = fs::temp_directory_path() / "cascade_item.voxel";
    { std::ofstream f(path); f << "# grid: 27\nV 0 0 0 Wood\nV 0 1 0 Wood\n"; }
    ASSERT_TRUE(templates.loadTemplate(path.string()));
    ItemDefinition def;
    def.id = "cascade_item";
    def.name = "Cascade Item";
    def.templateFile = "cascade_item";
    def.holdable = true;
    ItemRegistry::instance().registerItem(def);

    auto parentId = props.spawnProp("cascade_item", {5.0f, 10.0f, 5.0f}, 0.0f, false);
    auto childId  = props.spawnProp("cascade_item", {6.0f, 10.0f, 5.0f}, 0.0f, false);
    ASSERT_FALSE(parentId.empty());
    ASSERT_FALSE(childId.empty());
    ASSERT_TRUE(placed.setParent(childId, parentId));

    ASSERT_TRUE(placed.remove(parentId));
    EXPECT_EQ(placed.get(childId), nullptr) << "child registry entry survived";
    EXPECT_EQ(props.count(), 0u) << "prop manager leaked entries";
    EXPECT_EQ(kvm.count(), 0u) << "kinematic render objects leaked";
}

// ============================================================================
// placeSurfaceItems — spots on the ACTUAL placed tabletop (issue: items spawned
// on the table edge or hovering beside it, because the old path used the
// unrotated plan-time cube rect while the real table is wall-inset + rotated).
// ============================================================================
namespace {
// Synthetic asymmetric table (micro AABB x 0..17, y 0..8, z 0..8):
//  - tabletop: subcube slab rows y 6..8 spanning micro x 0..11, z 0..8 ONLY
//  - a leg: floor-level microcube out at x 17 (extends the AABB but NOT the top)
// So the top-surface rect (x 0..11) is a strict subset of the AABB (x 0..17):
// spots must come from the SURFACE, not the box.
VoxelTemplate makeAsymmetricTable() {
    VoxelTemplate t;
    for (int sx = 0; sx < 3; ++sx)
        for (int sz = 0; sz < 3; ++sz)
            t.subcubes.push_back({{0, 0, 0}, {sx, 2, sz}, "Wood"});
    for (int sz = 0; sz < 3; ++sz)
        t.subcubes.push_back({{1, 0, 0}, {0, 2, sz}, "Wood"});
    t.microcubes.push_back({{1, 0, 0}, {2, 0, 2}, {2, 0, 2}, "Wood"});  // leg @ x17,z8
    return t;
}
} // namespace

TEST(ItemPlacement, SurfaceSpotsSitOnTheMeasuredTabletopNotTheAabb) {
    const VoxelTemplate table = makeAsymmetricTable();
    const glm::ivec3 worldMicro(900, 450, 1800);   // cube (100, 50, 200), on-grid
    const auto spots = FurniturePlacer::placeSurfaceItems(
        "taproom", table, worldMicro, 0, {"tankard", "plate", "goblet"}, 7u);
    ASSERT_FALSE(spots.empty());
    // Top surface rect in world units: x [100, 101.333), z [200, 201).
    for (const auto& s : spots) {
        EXPECT_GE(s.worldPos.x, 100.0f);
        EXPECT_LE(s.worldPos.x, 101.334f) << "spot over the LEG (AABB), off the top";
        EXPECT_GE(s.worldPos.z, 200.0f);
        EXPECT_LE(s.worldPos.z, 201.0f);
        // Measured top of THIS instance: (450 + 8 + 1)/9 + 0.01 lift.
        EXPECT_NEAR(s.worldPos.y, 51.01f, 1e-3f) << "item not ON the tabletop plane";
    }
}

TEST(ItemPlacement, SurfaceSpotsFollowRotationAndInset) {
    const VoxelTemplate table = makeAsymmetricTable();
    // Wall-inset, OFF-grid placement (the realizer insets by wall thickness),
    // rotated 90 deg — exactly the case the plan-rect path got wrong.
    const glm::ivec3 worldMicro(903, 450, 1805);
    const auto spots = FurniturePlacer::placeSurfaceItems(
        "taproom", table, worldMicro, 90, {"tankard", "plate"}, 11u);
    ASSERT_FALSE(spots.empty());
    // rot90 about pivot mmax=(17,8,8): (x,y,z)->(8-z, y, x). Top rect (x 0..11,
    // z 0..8) maps to x' 0..8, z' 0..11. World: x [100.333, 101.334),
    // z [200.556, 201.889).
    for (const auto& s : spots) {
        EXPECT_GE(s.worldPos.x, 100.333f);
        EXPECT_LE(s.worldPos.x, 101.334f);
        EXPECT_GE(s.worldPos.z, 200.555f);
        EXPECT_LE(s.worldPos.z, 201.889f);
        EXPECT_NEAR(s.worldPos.y, 51.01f, 1e-3f);
    }

    // CONTROL (the bug this replaces): the legacy plan-rect path scattered on
    // cube centers of the UNROTATED catalog footprint. For this placement that
    // rect is x [100,102), z [200,201) — its cell center (101.5, 200.5) lies
    // clean OFF the rotated tabletop (x > 101.334, z < 200.555): an item there
    // hovers beside the table at tabletop height. Keep the numbers honest:
    EXPECT_GT(101.5f, 101.334f);   // old x outside the real top
    EXPECT_LT(200.5f, 200.555f);   // old z outside the real top
}

TEST(ItemPlacement, SurfaceSpotsKeepMinimumSpacing) {
    VoxelTemplate slab;   // solid 3x2-cube tabletop
    for (int x = 0; x < 3; ++x)
        for (int z = 0; z < 2; ++z)
            slab.cubes.push_back({{x, 0, z}, "Wood"});
    const auto spots = FurniturePlacer::placeSurfaceItems(
        "taproom", slab, {0, 90, 0}, 0,
        {"tankard", "plate", "goblet", "bowl", "jug", "candle"}, 3u);
    EXPECT_EQ(spots.size(), 6u) << "a 3x2 top holds the full taproom set";
    for (size_t i = 0; i < spots.size(); ++i)
        for (size_t j = i + 1; j < spots.size(); ++j) {
            const float dx = spots[i].worldPos.x - spots[j].worldPos.x;
            const float dz = spots[i].worldPos.z - spots[j].worldPos.z;
            EXPECT_GE(std::sqrt(dx * dx + dz * dz), 0.35f)
                << "items " << i << "/" << j << " overlap on the table";
        }
}
