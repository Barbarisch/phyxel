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
