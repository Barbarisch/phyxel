#include <gtest/gtest.h>
#include "core/HealthComponent.h"

using namespace Phyxel::Core;

TEST(HealthComponentTest, DefaultConstruction) {
    HealthComponent h;
    EXPECT_FLOAT_EQ(h.getHealth(), 100.0f);
    EXPECT_FLOAT_EQ(h.getMaxHealth(), 100.0f);
    EXPECT_TRUE(h.isAlive());
    EXPECT_FALSE(h.isInvulnerable());
    EXPECT_FLOAT_EQ(h.getHealthPercent(), 1.0f);
}

TEST(HealthComponentTest, CustomMaxHealth) {
    HealthComponent h(50.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 50.0f);
    EXPECT_FLOAT_EQ(h.getMaxHealth(), 50.0f);
}

TEST(HealthComponentTest, TakeDamage) {
    HealthComponent h(100.0f);
    float actual = h.takeDamage(30.0f);
    EXPECT_FLOAT_EQ(actual, 30.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 70.0f);
    EXPECT_TRUE(h.isAlive());
}

TEST(HealthComponentTest, TakeFatalDamage) {
    HealthComponent h(100.0f);
    float actual = h.takeDamage(150.0f);
    EXPECT_FLOAT_EQ(actual, 100.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 0.0f);
    EXPECT_FALSE(h.isAlive());
}

TEST(HealthComponentTest, TakeDamageExactlyKills) {
    HealthComponent h(50.0f);
    h.takeDamage(50.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 0.0f);
    EXPECT_FALSE(h.isAlive());
}

TEST(HealthComponentTest, TakeDamageWhileDead) {
    HealthComponent h(100.0f);
    h.kill();
    float actual = h.takeDamage(50.0f);
    EXPECT_FLOAT_EQ(actual, 0.0f);
}

TEST(HealthComponentTest, TakeDamageInvulnerable) {
    HealthComponent h(100.0f);
    h.setInvulnerable(true);
    float actual = h.takeDamage(50.0f);
    EXPECT_FLOAT_EQ(actual, 0.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 100.0f);
}

TEST(HealthComponentTest, TakeNegativeDamage) {
    HealthComponent h(100.0f);
    float actual = h.takeDamage(-10.0f);
    EXPECT_FLOAT_EQ(actual, 0.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 100.0f);
}

TEST(HealthComponentTest, Heal) {
    HealthComponent h(100.0f);
    h.takeDamage(60.0f);
    float actual = h.heal(30.0f);
    EXPECT_FLOAT_EQ(actual, 30.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 70.0f);
}

TEST(HealthComponentTest, HealOverMax) {
    HealthComponent h(100.0f);
    h.takeDamage(20.0f);
    float actual = h.heal(50.0f);
    EXPECT_FLOAT_EQ(actual, 20.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 100.0f);
}

TEST(HealthComponentTest, HealWhileDead) {
    HealthComponent h(100.0f);
    h.kill();
    float actual = h.heal(50.0f);
    EXPECT_FLOAT_EQ(actual, 0.0f);
    EXPECT_FALSE(h.isAlive());
}

TEST(HealthComponentTest, HealNegative) {
    HealthComponent h(100.0f);
    h.takeDamage(50.0f);
    float actual = h.heal(-10.0f);
    EXPECT_FLOAT_EQ(actual, 0.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 50.0f);
}

TEST(HealthComponentTest, Kill) {
    HealthComponent h(100.0f);
    h.kill();
    EXPECT_FLOAT_EQ(h.getHealth(), 0.0f);
    EXPECT_FALSE(h.isAlive());
}

TEST(HealthComponentTest, Revive) {
    HealthComponent h(100.0f);
    h.kill();
    h.revive(0.5f);
    EXPECT_FLOAT_EQ(h.getHealth(), 50.0f);
    EXPECT_TRUE(h.isAlive());
}

TEST(HealthComponentTest, ReviveFullHealth) {
    HealthComponent h(100.0f);
    h.kill();
    h.revive();
    EXPECT_FLOAT_EQ(h.getHealth(), 100.0f);
    EXPECT_TRUE(h.isAlive());
}

TEST(HealthComponentTest, SetHealth) {
    HealthComponent h(100.0f);
    h.setHealth(42.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 42.0f);
    EXPECT_TRUE(h.isAlive());
}

TEST(HealthComponentTest, SetHealthToZero) {
    HealthComponent h(100.0f);
    h.setHealth(0.0f);
    EXPECT_FALSE(h.isAlive());
}

TEST(HealthComponentTest, SetHealthClamps) {
    HealthComponent h(100.0f);
    h.setHealth(200.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 100.0f);
    h.setHealth(-50.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 0.0f);
}

TEST(HealthComponentTest, SetMaxHealth) {
    HealthComponent h(100.0f);
    h.setMaxHealth(50.0f);
    EXPECT_FLOAT_EQ(h.getMaxHealth(), 50.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 50.0f); // clamped to new max
}

TEST(HealthComponentTest, SetMaxHealthLowerKills) {
    HealthComponent h(100.0f);
    h.setHealth(5.0f);
    h.setMaxHealth(0.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 0.0f);
    EXPECT_FALSE(h.isAlive());
}

TEST(HealthComponentTest, HealthPercent) {
    HealthComponent h(200.0f);
    h.takeDamage(50.0f);
    EXPECT_FLOAT_EQ(h.getHealthPercent(), 0.75f);
}

TEST(HealthComponentTest, ToJsonAndBack) {
    HealthComponent h(200.0f);
    h.takeDamage(60.0f);
    h.setInvulnerable(true);

    auto j = h.toJson();

    HealthComponent h2;
    h2.fromJson(j);
    EXPECT_FLOAT_EQ(h2.getHealth(), 140.0f);
    EXPECT_FLOAT_EQ(h2.getMaxHealth(), 200.0f);
    EXPECT_TRUE(h2.isAlive());
    EXPECT_TRUE(h2.isInvulnerable());
}

TEST(HealthComponentTest, ToJsonContainsFields) {
    HealthComponent h(100.0f);
    h.takeDamage(25.0f);
    auto j = h.toJson();

    EXPECT_FLOAT_EQ(j["health"].get<float>(), 75.0f);
    EXPECT_FLOAT_EQ(j["maxHealth"].get<float>(), 100.0f);
    EXPECT_TRUE(j["alive"].get<bool>());
    EXPECT_FALSE(j["invulnerable"].get<bool>());
    EXPECT_FLOAT_EQ(j["healthPercent"].get<float>(), 0.75f);
}

TEST(HealthComponentTest, MultipleDamageThenHeal) {
    HealthComponent h(100.0f);
    h.takeDamage(20.0f);
    h.takeDamage(30.0f);
    h.heal(10.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 60.0f);
}

TEST(HealthComponentTest, DamageWithSource) {
    HealthComponent h(100.0f);
    float actual = h.takeDamage(25.0f, "enemy_01");
    EXPECT_FLOAT_EQ(actual, 25.0f);
    EXPECT_FLOAT_EQ(h.getHealth(), 75.0f);
}
