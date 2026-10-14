#include <gtest/gtest.h>

#include "core/MeleeAnimMapper.h"
#include "core/ItemDefinition.h"
#include "core/ItemRegistry.h"
#include "core/RpgItem.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

using namespace Phyxel::Core;

// ============================================================================
// A3 item 3 (docs/AnimationSystemV3Plan.md §4 A3): the grip class and load band a character
// composes its carry layers on are DERIVED from the D&D weapon data (properties + weightLbs)
// and the off-hand — never hand-authored on the 52 `held` blocks. Every case below is a real
// data entry; `maul`/`greataxe` were added to weapons_martial.json for this (SRD stats).
// ============================================================================

namespace {

class GripClass : public ::testing::Test {
protected:
    void SetUp() override {
        auto& rpg = RpgItemRegistry::instance();
        rpg.clear();
        ASSERT_GT(rpg.loadFromDirectory("resources/rpg_items"), 0);
        auto& melee = MeleeAnimMapper::instance();
        if (!melee.isLoaded()) melee.loadConfig("resources/rpg_items/anim/melee_anim_families.json");
        // gameplay items (torch, bow, staff_fire carry no D&D entry)
        std::ifstream in("resources/items.json");
        ASSERT_TRUE(in.is_open());
        nlohmann::json j; in >> j;
        ItemRegistry::instance().loadFromJson(j.contains("items") ? j["items"] : j);
    }
    static ItemDefinition weapon(const char* id) {
        ItemDefinition d; d.id = id; d.type = ItemType::Weapon; d.holdable = true; return d;
    }
    static const ItemDefinition* gameplay(const char* id) { return ItemRegistry::instance().getItem(id); }
    const MeleeAnimMapper& melee() const { return MeleeAnimMapper::instance(); }
};

} // namespace

TEST_F(GripClass, EmptyHandIsEmptyWithNoLoad) {
    const auto g = melee().resolveGripFactors(nullptr, nullptr);
    EXPECT_EQ(g.grip, "empty");
    EXPECT_EQ(g.load, "none");
}

TEST_F(GripClass, TwoHandedHeavyWeaponsAreTwoHandHeavyWithTheirWeightBand) {
    const auto gs = melee().resolveGripFactors(&weapon("greatsword"));   // 6 lb, Heavy+TwoHanded
    EXPECT_EQ(gs.grip, "2h_heavy");
    EXPECT_EQ(gs.load, "heavy");
    const auto maul = melee().resolveGripFactors(&weapon("maul"));       // 10 lb, Heavy+TwoHanded
    EXPECT_EQ(maul.grip, "2h_heavy");
    EXPECT_EQ(maul.load, "heavy");
}

TEST_F(GripClass, OneHandedWeaponsAreOneHandWithLightOrNoLoad) {
    const auto ls = melee().resolveGripFactors(&weapon("longsword"));    // 3 lb, Versatile
    EXPECT_EQ(ls.grip, "1h");
    EXPECT_EQ(ls.load, "light");
    const auto dg = melee().resolveGripFactors(&weapon("dagger"));       // 1 lb
    EXPECT_EQ(dg.grip, "1h");
    EXPECT_EQ(dg.load, "none");
}

TEST_F(GripClass, AShieldInTheOffHandMakesOneHandIntoOneHandShield) {
    ItemDefinition shield; shield.id = "shield"; shield.holdable = true;
    const auto g = melee().resolveGripFactors(&weapon("longsword"), &shield);
    EXPECT_EQ(g.grip, "1h_shield");
    // a shield cannot make a two-hander one-handed
    EXPECT_EQ(melee().resolveGripFactors(&weapon("greatsword"), &shield).grip, "2h_heavy");
}

TEST_F(GripClass, AmmunitionWeaponsAreBowsAndStavesAreStaves) {
    EXPECT_EQ(melee().resolveGripFactors(&weapon("longbow")).grip, "bow");
    EXPECT_EQ(melee().resolveGripFactors(&weapon("quarterstaff")).grip, "staff");
    ASSERT_NE(gameplay("staff_fire"), nullptr);
    EXPECT_EQ(melee().resolveGripFactors(gameplay("staff_fire")).grip, "staff");
    ASSERT_NE(gameplay("bow"), nullptr);
    EXPECT_EQ(melee().resolveGripFactors(gameplay("bow")).grip, "bow");
}

TEST_F(GripClass, TheTorchIsATorchGrip) {
    // items.json declares the torch's light under "effects", so the rule keys on the id too.
    ASSERT_NE(gameplay("torch"), nullptr);
    EXPECT_EQ(melee().resolveGripFactors(gameplay("torch")).grip, "torch");
    ItemDefinition lantern; lantern.id = "brass_lantern"; lantern.holdable = true;
    lantern.held.lightIntensity = 1.0f; lantern.held.lightRadius = 6.0f;
    EXPECT_EQ(melee().resolveGripFactors(&lantern).grip, "torch") << "any held light is a torch grip";
}

TEST_F(GripClass, GameplayWeaponsWithoutRpgDataFallBackToOneHandNoLoad) {
    ASSERT_NE(gameplay("frying_pan"), nullptr);
    const auto g = melee().resolveGripFactors(gameplay("frying_pan"));
    EXPECT_EQ(g.grip, "1h");
    EXPECT_EQ(g.load, "none");
}
