// DebrisRuntimeTest - DebrisInteractionPlan Phase 5: the shared GPU-debris runtime that the editor
// and shipped games both use.
//   * the shipped default is ON (user decision 2026-10-07) - pinned here; changing it is a
//     deliberate decision that updates this test in the same commit;
//   * a runtime that cannot run GPU debris says so (disabled reason) and has no solver - never a
//     silent half-state;
//   * the scaffold's spell hook: which spells blast, how big, how hard.

#include <gtest/gtest.h>

#include "core/DebrisRuntime.h"
#include "core/SpellDefinition.h"

using Phyxel::DebrisRuntime;
using Phyxel::Core::SpellDefinition;

TEST(DebrisRuntime, ShippedDefaultIsOn) {
    DebrisRuntime::Config cfg;
    EXPECT_TRUE(cfg.enabled) << "game.json debris.enabled defaults to true (user decision 2026-10-07)";
}

TEST(DebrisRuntime, DisabledOrDevicelessRuntimeIsLoudNotHalfAlive) {
    DebrisRuntime off;
    DebrisRuntime::Config cfg;
    cfg.enabled = false;
    EXPECT_FALSE(off.initialize(nullptr, nullptr, nullptr, nullptr, cfg));
    EXPECT_EQ(off.gpu(), nullptr);
    EXPECT_FALSE(off.enabled());
    EXPECT_FALSE(off.disabledReason().empty()) << "a disabled runtime must say why";

    DebrisRuntime noDevice;
    EXPECT_FALSE(noDevice.initialize(nullptr, nullptr, nullptr, nullptr));
    EXPECT_EQ(noDevice.gpu(), nullptr);
    EXPECT_FALSE(noDevice.disabledReason().empty());

    // Every per-frame entry is a safe no-op without a solver.
    noDevice.beginFrame(1.0f / 60.0f);
    noDevice.feedCharacters({nullptr}, glm::vec3(0.0f));
    noDevice.feedKinematicObjects(nullptr, 1.0f / 60.0f, glm::vec3(0.0f));
    noDevice.feedRigidBodies(glm::vec3(0.0f));
    noDevice.shutdown();
    const auto r = noDevice.applyDamage(glm::vec3(0.0f), 2.0f, 300.0f);
    EXPECT_EQ(r.voxelsBroken, 0) << "no world: nothing to break";
}

TEST(DebrisRuntime, SpellBlastSizesFromTheSpellDefinition) {
    SpellDefinition heal;                       // no base damage
    heal.healDice = {1, Phyxel::Core::DieType::D8, 0};
    EXPECT_FALSE(DebrisRuntime::spellBlast(heal).blast) << "heals and buffs do not blast";

    SpellDefinition bolt;                       // fire bolt: 1d10, single target
    bolt.baseDamage = {1, Phyxel::Core::DieType::D10, 0};
    const auto b = DebrisRuntime::spellBlast(bolt);
    EXPECT_TRUE(b.blast);
    EXPECT_FLOAT_EQ(b.radius, DebrisRuntime::SPELL_SINGLE_TARGET_RADIUS);
    EXPECT_FLOAT_EQ(b.energy, DebrisRuntime::SPELL_ENERGY_PER_DAMAGE * 5.5f);

    SpellDefinition fireball;                   // 8d6, 20 ft sphere
    fireball.baseDamage   = {8, Phyxel::Core::DieType::D6, 0};
    fireball.areaShape    = Phyxel::Core::AreaShape::Sphere;
    fireball.areaSizeFeet = 20.0f;
    const auto f = DebrisRuntime::spellBlast(fireball);
    EXPECT_TRUE(f.blast);
    EXPECT_NEAR(f.radius, 6.096f, 1e-3f) << "area size in feet -> metres";
    EXPECT_FLOAT_EQ(f.energy, DebrisRuntime::SPELL_ENERGY_PER_DAMAGE * 28.0f);
    EXPECT_NEAR(f.energy, 350.0f, 20.0f) << "a fireball sits at the editor's test-spell blast scale";
}
