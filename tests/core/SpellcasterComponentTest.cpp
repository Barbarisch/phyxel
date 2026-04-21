#include <gtest/gtest.h>
#include "core/SpellcasterComponent.h"

using namespace Phyxel::Core;

class SpellcasterTest : public ::testing::Test {
protected:
    SpellcasterComponent caster{"wizard_entity"};
    CharacterAttributes  attrs;

    void SetUp() override {
        attrs.setAll(10, 10, 10, 18, 10, 10);  // INT=18, mod=+4
        caster.initialize("wizard", AbilityType::Intelligence, 5, "full");
    }
};

// ---------------------------------------------------------------------------
// SpellSlots
// ---------------------------------------------------------------------------

TEST(SpellSlotsTest, DefaultZero) {
    SpellSlots s;
    EXPECT_EQ(s.totalMaximum(),   0);
    EXPECT_EQ(s.totalRemaining(), 0);
}

TEST(SpellSlotsTest, SpendAndRestore) {
    SpellSlots s;
    s.maximum[0] = 4; s.remaining[0] = 4;
    EXPECT_TRUE(s.canSpend(1));
    EXPECT_TRUE(s.spend(1));
    EXPECT_EQ(s.remaining[0], 3);
    s.restore(1);
    EXPECT_EQ(s.remaining[0], 4);
}

TEST(SpellSlotsTest, CannotOverRestore) {
    SpellSlots s;
    s.maximum[0] = 2; s.remaining[0] = 2;
    s.restore(1, 5);
    EXPECT_EQ(s.remaining[0], 2);  // capped at maximum
}

TEST(SpellSlotsTest, SpendWhenEmpty) {
    SpellSlots s;
    s.maximum[0] = 2; s.remaining[0] = 0;
    EXPECT_FALSE(s.canSpend(1));
    EXPECT_FALSE(s.spend(1));
}

TEST(SpellSlotsTest, RestoreAll) {
    SpellSlots s;
    s.maximum[0] = 4; s.remaining[0] = 1;
    s.maximum[1] = 3; s.remaining[1] = 0;
    s.restoreAll();
    EXPECT_EQ(s.remaining[0], 4);
    EXPECT_EQ(s.remaining[1], 3);
}

TEST(SpellSlotsTest, TotalRemaining) {
    SpellSlots s;
    s.maximum[0] = 4; s.remaining[0] = 2;
    s.maximum[1] = 3; s.remaining[1] = 1;
    EXPECT_EQ(s.totalRemaining(), 3);
    EXPECT_EQ(s.totalMaximum(), 7);
}

TEST(SpellSlotsTest, JsonRoundTrip) {
    SpellSlots s;
    s.maximum[0] = 4; s.remaining[0] = 3;
    s.maximum[2] = 2; s.remaining[2] = 1;
    auto j = s.toJson();
    SpellSlots s2;
    s2.fromJson(j);
    EXPECT_EQ(s2.maximum[0], 4);
    EXPECT_EQ(s2.remaining[0], 3);
    EXPECT_EQ(s2.maximum[2], 2);
    EXPECT_EQ(s2.remaining[2], 1);
}

// ---------------------------------------------------------------------------
// SpellSlotTable — full caster
// ---------------------------------------------------------------------------

TEST(SpellSlotTableTest, FullCasterLevel1) {
    auto slots = SpellSlotTable::forLevel(1, "full");
    EXPECT_EQ(slots.maximum[0], 2);  // 2 first-level slots
    EXPECT_EQ(slots.maximum[1], 0);
}

TEST(SpellSlotTableTest, FullCasterLevel5) {
    auto slots = SpellSlotTable::forLevel(5, "full");
    EXPECT_EQ(slots.maximum[0], 4);  // 4 1st-level
    EXPECT_EQ(slots.maximum[1], 3);  // 3 2nd-level
    EXPECT_EQ(slots.maximum[2], 2);  // 2 3rd-level
    EXPECT_EQ(slots.maximum[3], 0);
}

TEST(SpellSlotTableTest, FullCasterLevel20) {
    auto slots = SpellSlotTable::forLevel(20, "full");
    EXPECT_EQ(slots.maximum[0], 4);
    EXPECT_EQ(slots.maximum[8], 1);  // 1 ninth-level slot
}

TEST(SpellSlotTableTest, HalfCasterLevel1NoSlots) {
    auto slots = SpellSlotTable::forLevel(1, "half");
    EXPECT_EQ(slots.totalMaximum(), 0);
}

TEST(SpellSlotTableTest, HalfCasterLevel5) {
    auto slots = SpellSlotTable::forLevel(5, "half");
    EXPECT_EQ(slots.maximum[0], 4);  // same as full caster level 3
    EXPECT_EQ(slots.maximum[1], 2);
    EXPECT_EQ(slots.maximum[2], 0);
}

TEST(SpellSlotTableTest, ThirdCasterLevel3) {
    auto slots = SpellSlotTable::forLevel(3, "third");
    EXPECT_EQ(slots.maximum[0], 2);
    EXPECT_EQ(slots.maximum[1], 0);
}

TEST(SpellSlotTableTest, WarlockPactLevel5) {
    auto slots = SpellSlotTable::forLevel(5, "pact");
    // Warlock level 5: 2 slots at 3rd level
    EXPECT_EQ(slots.maximum[2], 2);  // index 2 = 3rd-level
    EXPECT_EQ(slots.maximum[0], 0);
    EXPECT_EQ(slots.maximum[1], 0);
}

TEST(SpellSlotTableTest, WarlockPactLevelLookup) {
    EXPECT_EQ(SpellSlotTable::pactSlotLevel(1),  1);
    EXPECT_EQ(SpellSlotTable::pactSlotLevel(3),  2);
    EXPECT_EQ(SpellSlotTable::pactSlotLevel(5),  3);
    EXPECT_EQ(SpellSlotTable::pactSlotLevel(9),  5);
    EXPECT_EQ(SpellSlotTable::pactSlotLevel(11), 5);
}

TEST(SpellSlotTableTest, SlotsStartAtMaximum) {
    auto slots = SpellSlotTable::forLevel(5, "full");
    EXPECT_EQ(slots.remaining[0], slots.maximum[0]);
    EXPECT_EQ(slots.remaining[1], slots.maximum[1]);
}

// ---------------------------------------------------------------------------
// SpellcasterComponent — stats
// ---------------------------------------------------------------------------

TEST_F(SpellcasterTest, SpellSaveDC) {
    // 8 + profBonus(3 at level 5) + INT mod(+4) = 15
    EXPECT_EQ(caster.spellSaveDC(3, attrs), 15);
}

TEST_F(SpellcasterTest, SpellAttackBonus) {
    // profBonus(3) + INT mod(+4) = +7
    EXPECT_EQ(caster.spellAttackBonus(3, attrs), 7);
}

// ---------------------------------------------------------------------------
// Cantrips
// ---------------------------------------------------------------------------

TEST_F(SpellcasterTest, LearnCantrip) {
    EXPECT_TRUE(caster.learnCantrip("fire_bolt"));
    EXPECT_TRUE(caster.knowsCantrip("fire_bolt"));
    EXPECT_FALSE(caster.learnCantrip("fire_bolt"));  // already known
}

TEST_F(SpellcasterTest, ForgetCantrip) {
    caster.learnCantrip("fire_bolt");
    EXPECT_TRUE(caster.forgetCantrip("fire_bolt"));
    EXPECT_FALSE(caster.knowsCantrip("fire_bolt"));
    EXPECT_FALSE(caster.forgetCantrip("fire_bolt"));  // not known
}

// ---------------------------------------------------------------------------
// Known / prepared spells
// ---------------------------------------------------------------------------

TEST_F(SpellcasterTest, LearnAndPrepare) {
    EXPECT_TRUE(caster.learnSpell("fireball"));
    EXPECT_TRUE(caster.knowsSpell("fireball"));
    EXPECT_FALSE(caster.hasPrepared("fireball"));

    EXPECT_TRUE(caster.prepareSpell("fireball"));
    EXPECT_TRUE(caster.hasPrepared("fireball"));
}

TEST_F(SpellcasterTest, LearnAsPrepared) {
    caster.learnSpell("magic_missile", true);
    EXPECT_TRUE(caster.knowsSpell("magic_missile"));
    EXPECT_TRUE(caster.hasPrepared("magic_missile"));
}

TEST_F(SpellcasterTest, Unprepare) {
    caster.learnSpell("fireball", true);
    EXPECT_TRUE(caster.unprepareSpell("fireball"));
    EXPECT_FALSE(caster.hasPrepared("fireball"));
    EXPECT_TRUE(caster.knowsSpell("fireball"));
}

TEST_F(SpellcasterTest, ForgetSpell) {
    caster.learnSpell("fireball");
    EXPECT_TRUE(caster.forgetSpell("fireball"));
    EXPECT_FALSE(caster.knowsSpell("fireball"));
}

TEST_F(SpellcasterTest, CantPrepareUnknown) {
    EXPECT_FALSE(caster.prepareSpell("fireball"));
}

// ---------------------------------------------------------------------------
// Slot management
// ---------------------------------------------------------------------------

TEST_F(SpellcasterTest, CanCastCantripWithoutSlot) {
    caster.learnCantrip("fire_bolt");
    EXPECT_TRUE(caster.canCast("fire_bolt", 0));
}

TEST_F(SpellcasterTest, CanCastPreparedSpell) {
    caster.learnSpell("fireball", true);
    EXPECT_TRUE(caster.canCast("fireball", 3));
}

TEST_F(SpellcasterTest, CannotCastUnprepared) {
    caster.learnSpell("fireball");  // not prepared
    EXPECT_FALSE(caster.canCast("fireball", 3));
}

TEST_F(SpellcasterTest, SpendSlotDecrements) {
    int before = caster.slots().remaining[2];  // 3rd-level (index 2)
    EXPECT_GT(before, 0);
    caster.spendSlot(3);
    EXPECT_EQ(caster.slots().remaining[2], before - 1);
}

TEST_F(SpellcasterTest, CannotCastWithNoSlot) {
    caster.learnSpell("fireball", true);
    // Spend all 3rd-level slots
    while (caster.slots().canSpend(3)) caster.spendSlot(3);
    EXPECT_FALSE(caster.canCast("fireball", 3));
}

// ---------------------------------------------------------------------------
// Concentration
// ---------------------------------------------------------------------------

TEST_F(SpellcasterTest, StartConcentration) {
    EXPECT_FALSE(caster.isConcentrating());
    caster.startConcentration("fly", "target_1", 600.0f);
    EXPECT_TRUE(caster.isConcentrating());
    EXPECT_EQ(caster.concentration().spellId, "fly");
}

TEST_F(SpellcasterTest, BreakConcentration) {
    caster.startConcentration("fly", "target_1", 600.0f);
    caster.breakConcentration();
    EXPECT_FALSE(caster.isConcentrating());
}

TEST_F(SpellcasterTest, NewConcentrationBreaksOld) {
    caster.startConcentration("fly",          "t1", 600.0f);
    caster.startConcentration("hold_person",  "t2",  60.0f);
    EXPECT_EQ(caster.concentration().spellId, "hold_person");
}

TEST_F(SpellcasterTest, ConcentrationTimerExpires) {
    caster.startConcentration("hold_person", "t1", 1.0f);
    caster.updateConcentration(0.5f);
    EXPECT_TRUE(caster.isConcentrating());
    caster.updateConcentration(0.6f);
    EXPECT_FALSE(caster.isConcentrating());
}

TEST_F(SpellcasterTest, IndefiniteConcentrationDoesNotExpire) {
    caster.startConcentration("held_spell", "t1", -1.0f);
    caster.updateConcentration(99999.0f);
    EXPECT_TRUE(caster.isConcentrating());
}

// ---------------------------------------------------------------------------
// Rests
// ---------------------------------------------------------------------------

TEST_F(SpellcasterTest, LongRestRestoresSlots) {
    while (caster.slots().canSpend(1)) caster.spendSlot(1);
    EXPECT_EQ(caster.slots().remaining[0], 0);
    caster.onLongRest();
    EXPECT_EQ(caster.slots().remaining[0], caster.slots().maximum[0]);
}

TEST_F(SpellcasterTest, LongRestBreaksConcentration) {
    caster.startConcentration("fly", "t1", 600.0f);
    caster.onLongRest();
    EXPECT_FALSE(caster.isConcentrating());
}

TEST_F(SpellcasterTest, ShortRestDoesNotRestoreFullCasterSlots) {
    while (caster.slots().canSpend(1)) caster.spendSlot(1);
    int before = caster.slots().remaining[0];
    caster.onShortRest("full");
    EXPECT_EQ(caster.slots().remaining[0], before);  // unchanged for full casters
}

TEST_F(SpellcasterTest, ShortRestRestoresPactSlots) {
    SpellcasterComponent warlock{"warlock_entity"};
    warlock.initialize("warlock", AbilityType::Charisma, 5, "pact");
    // Spend all pact slots
    while (warlock.slots().canSpend(3)) warlock.spendSlot(3);
    EXPECT_EQ(warlock.slots().totalRemaining(), 0);
    warlock.onShortRest("pact");
    EXPECT_GT(warlock.slots().totalRemaining(), 0);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_F(SpellcasterTest, JsonRoundTrip) {
    caster.learnCantrip("fire_bolt");
    caster.learnSpell("fireball", true);
    caster.learnSpell("hold_person", false);
    caster.spendSlot(3);
    caster.startConcentration("fly", "target_1", 60.0f);

    auto j = caster.toJson();
    SpellcasterComponent caster2;
    caster2.fromJson(j);

    EXPECT_EQ(caster2.entityId(), "wizard_entity");
    EXPECT_EQ(caster2.spellcastingAbility(), AbilityType::Intelligence);
    EXPECT_EQ(caster2.characterLevel(), 5);
    EXPECT_TRUE(caster2.knowsCantrip("fire_bolt"));
    EXPECT_TRUE(caster2.hasPrepared("fireball"));
    EXPECT_TRUE(caster2.knowsSpell("hold_person"));
    EXPECT_FALSE(caster2.hasPrepared("hold_person"));
    EXPECT_EQ(caster2.slots().remaining[2], caster.slots().remaining[2]);
    EXPECT_TRUE(caster2.isConcentrating());
    EXPECT_EQ(caster2.concentration().spellId, "fly");
}
