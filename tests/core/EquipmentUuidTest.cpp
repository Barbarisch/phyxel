#include <gtest/gtest.h>

#include "core/EquipmentSystem.h"
#include "core/ItemDefinition.h"
#include "core/Uuid.h"

using namespace Phyxel::Core;

namespace {

ItemDefinition makeWeapon(const std::string& id) {
    ItemDefinition d;
    d.id = id;
    d.type = ItemType::Weapon;
    d.equipSlot = EquipSlot::MainHand;
    return d;
}

// EquipmentSlots stores a COPY of the ItemDefinition, so without an explicit instance-uuid channel
// a unique item's identity would be lost the moment it is equipped. These tests are the tripwire.

TEST(EquipmentUuidTest, EquipCarriesAndUnequipClearsInstanceUuid) {
    EquipmentSlots eq;
    const ItemDefinition sword = makeWeapon("iron_sword");
    const std::string uuid = Uuid::generate();

    ASSERT_TRUE(eq.equip(sword, uuid));
    EXPECT_EQ(eq.getInstanceUuid(EquipSlot::MainHand), uuid) << "instance uuid lost on equip";

    // The caller reads the uuid before unequipping to restore the same instance to the inventory.
    const std::string recovered = eq.getInstanceUuid(EquipSlot::MainHand);
    auto removed = eq.unequip(EquipSlot::MainHand);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(*removed, "iron_sword");
    EXPECT_EQ(recovered, uuid);
    EXPECT_EQ(eq.getInstanceUuid(EquipSlot::MainHand), "") << "uuid not cleared after unequip";
}

TEST(EquipmentUuidTest, PlainEquipHasNoInstanceUuid) {
    EquipmentSlots eq;
    ASSERT_TRUE(eq.equip(makeWeapon("iron_sword")));  // no uuid overload
    EXPECT_EQ(eq.getInstanceUuid(EquipSlot::MainHand), "");
}

TEST(EquipmentUuidTest, ReEquipReplacesInstanceUuidAndEmptyClears) {
    EquipmentSlots eq;
    const ItemDefinition sword = makeWeapon("iron_sword");
    const std::string u1 = Uuid::generate(), u2 = Uuid::generate();

    eq.equip(sword, u1);
    eq.equip(sword, u2);  // a different instance into the same slot
    EXPECT_EQ(eq.getInstanceUuid(EquipSlot::MainHand), u2) << "re-equip did not replace the instance uuid";

    eq.equip(sword);  // equipping without a uuid must clear any stale instance id
    EXPECT_EQ(eq.getInstanceUuid(EquipSlot::MainHand), "") << "stale instance uuid left after plain re-equip";
}

}  // namespace
