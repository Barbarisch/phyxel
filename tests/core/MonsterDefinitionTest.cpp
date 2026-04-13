#include <gtest/gtest.h>
#include "core/MonsterDefinition.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// CreatureSize helpers
// ---------------------------------------------------------------------------

TEST(MonsterDefinitionTest, SizeRoundTrip) {
    const CreatureSize sizes[] = {
        CreatureSize::Tiny, CreatureSize::Small, CreatureSize::Medium,
        CreatureSize::Large, CreatureSize::Huge, CreatureSize::Gargantuan
    };
    for (auto s : sizes) {
        const char* n = creatureSizeName(s);
        EXPECT_EQ(creatureSizeFromString(n), s) << "Failed for: " << n;
    }
}

TEST(MonsterDefinitionTest, SizeFromStringCaseInsensitive) {
    EXPECT_EQ(creatureSizeFromString("tiny"),       CreatureSize::Tiny);
    EXPECT_EQ(creatureSizeFromString("LARGE"),      CreatureSize::Large);
    EXPECT_EQ(creatureSizeFromString("gargantuan"), CreatureSize::Gargantuan);
    EXPECT_EQ(creatureSizeFromString("unknown"),    CreatureSize::Medium); // default
}

// ---------------------------------------------------------------------------
// MonsterAttack serialization
// ---------------------------------------------------------------------------

TEST(MonsterDefinitionTest, AttackJsonRoundTrip) {
    MonsterAttack a;
    a.name          = "Shortsword";
    a.isWeaponAttack= true;
    a.toHitBonus    = 4;
    a.damageDice    = "1d6+2";
    a.damageType    = "piercing";
    a.reach         = 5;
    a.isRanged      = false;

    auto j = a.toJson();
    auto r = MonsterAttack::fromJson(j);

    EXPECT_EQ(r.name,         "Shortsword");
    EXPECT_EQ(r.toHitBonus,   4);
    EXPECT_EQ(r.damageDice,   "1d6+2");
    EXPECT_EQ(r.damageType,   "piercing");
    EXPECT_EQ(r.reach,        5);
    EXPECT_FALSE(r.isRanged);
}

TEST(MonsterDefinitionTest, RangedAttackRoundTrip) {
    MonsterAttack a;
    a.name        = "Shortbow";
    a.isRanged    = true;
    a.rangeNormal = 80;
    a.rangeLong   = 320;
    a.toHitBonus  = 4;
    a.damageDice  = "1d6+2";
    a.damageType  = "piercing";

    auto r = MonsterAttack::fromJson(a.toJson());
    EXPECT_TRUE(r.isRanged);
    EXPECT_EQ(r.rangeNormal, 80);
    EXPECT_EQ(r.rangeLong,   320);
}

TEST(MonsterDefinitionTest, SavingThrowAttackRoundTrip) {
    MonsterAttack a;
    a.name          = "Web";
    a.isWeaponAttack= false;
    a.requiresSave  = true;
    a.saveAbility   = "DEX";
    a.saveDC        = 11;
    a.effectOnFail  = "Restrained";
    a.effectDuration= -1.0f;

    auto r = MonsterAttack::fromJson(a.toJson());
    EXPECT_TRUE(r.requiresSave);
    EXPECT_EQ(r.saveAbility,  "DEX");
    EXPECT_EQ(r.saveDC,       11);
    EXPECT_EQ(r.effectOnFail, "Restrained");
    EXPECT_FLOAT_EQ(r.effectDuration, -1.0f);
}

// ---------------------------------------------------------------------------
// MonsterDefinition — construction and queries
// ---------------------------------------------------------------------------

static MonsterDefinition makeGoblin() {
    MonsterDefinition m;
    m.id             = "goblin";
    m.name           = "Goblin";
    m.type           = "humanoid";
    m.subtype        = "(goblinoid)";
    m.size           = CreatureSize::Small;
    m.alignment      = "neutral evil";
    m.armorClass     = 15;
    m.hitPointDice   = "2d6";
    m.averageHP      = 7;
    m.speed          = 30;
    m.challengeRating= 0.25f;
    m.xpValue        = 50;
    m.darkvisionRange= 60;
    m.passivePerception = 9;
    m.tags           = {"humanoid", "goblinoid"};
    m.conditionImmunities = {};

    m.attributes.strength.base  = 8;
    m.attributes.dexterity.base = 14;

    MonsterAttack scimitar;
    scimitar.name       = "Scimitar";
    scimitar.toHitBonus = 4;
    scimitar.damageDice = "1d6+2";
    scimitar.damageType = "slashing";
    m.attacks.push_back(scimitar);

    return m;
}

TEST(MonsterDefinitionTest, ProficiencyBonusByCR) {
    MonsterDefinition m;
    m.challengeRating = 0.25f; EXPECT_EQ(m.proficiencyBonus(), 2);
    m.challengeRating = 4.0f;  EXPECT_EQ(m.proficiencyBonus(), 2);
    m.challengeRating = 5.0f;  EXPECT_EQ(m.proficiencyBonus(), 3);
    m.challengeRating = 8.0f;  EXPECT_EQ(m.proficiencyBonus(), 3);
    m.challengeRating = 9.0f;  EXPECT_EQ(m.proficiencyBonus(), 4);
    m.challengeRating = 17.0f; EXPECT_EQ(m.proficiencyBonus(), 6);
    m.challengeRating = 30.0f; EXPECT_EQ(m.proficiencyBonus(), 9);
}

TEST(MonsterDefinitionTest, RollHPIsPositive) {
    DiceSystem::setSeed(42);
    auto goblin = makeGoblin();
    for (int i = 0; i < 20; ++i)
        EXPECT_GE(goblin.rollHP(), 1);
}

TEST(MonsterDefinitionTest, IsImmuneToCondition) {
    MonsterDefinition m;
    m.conditionImmunities = {"poisoned", "exhausted"};
    EXPECT_TRUE(m.isImmuneTo("poisoned"));
    EXPECT_TRUE(m.isImmuneTo("Poisoned"));    // case-insensitive
    EXPECT_TRUE(m.isImmuneTo("EXHAUSTED"));
    EXPECT_FALSE(m.isImmuneTo("charmed"));
}

TEST(MonsterDefinitionTest, AttributeAccess) {
    auto goblin = makeGoblin();
    EXPECT_EQ(goblin.attributes.score(AbilityType::Strength),  8);
    EXPECT_EQ(goblin.attributes.score(AbilityType::Dexterity), 14);
    EXPECT_EQ(goblin.attributes.modifier(AbilityType::Dexterity), 2);
}

// ---------------------------------------------------------------------------
// MonsterDefinition JSON round-trip
// ---------------------------------------------------------------------------

TEST(MonsterDefinitionTest, JsonRoundTrip) {
    auto original = makeGoblin();
    original.damageResistances  = {"piercing"};
    original.conditionImmunities= {"frightened"};
    original.languages          = {"Common", "Goblin"};
    original.traits.push_back({"Nimble Escape", "Can Disengage/Hide as bonus action."});

    auto j       = original.toJson();
    auto restored = MonsterDefinition::fromJson(j);

    EXPECT_EQ(restored.id,             "goblin");
    EXPECT_EQ(restored.name,           "Goblin");
    EXPECT_EQ(restored.type,           "humanoid");
    EXPECT_EQ(restored.size,           CreatureSize::Small);
    EXPECT_EQ(restored.armorClass,     15);
    EXPECT_FLOAT_EQ(restored.challengeRating, 0.25f);
    EXPECT_EQ(restored.xpValue,        50);
    EXPECT_EQ(restored.darkvisionRange,60);
    ASSERT_EQ(restored.attacks.size(), 1u);
    EXPECT_EQ(restored.attacks[0].name, "Scimitar");
    EXPECT_EQ(restored.attacks[0].toHitBonus, 4);
    ASSERT_EQ(restored.damageResistances.size(), 1u);
    EXPECT_EQ(restored.damageResistances[0], "piercing");
    ASSERT_EQ(restored.conditionImmunities.size(), 1u);
    EXPECT_EQ(restored.conditionImmunities[0], "frightened");
    ASSERT_EQ(restored.traits.size(), 1u);
    EXPECT_EQ(restored.traits[0].first, "Nimble Escape");
    ASSERT_EQ(restored.languages.size(), 2u);
    EXPECT_EQ(restored.languages[0], "Common");
}

// ---------------------------------------------------------------------------
// MonsterRegistry
// ---------------------------------------------------------------------------

class RegistryTest : public ::testing::Test {
protected:
    void SetUp() override { MonsterRegistry::instance().clear(); }
    void TearDown() override { MonsterRegistry::instance().clear(); }
};

TEST_F(RegistryTest, RegisterAndRetrieve) {
    auto goblin = makeGoblin();
    MonsterRegistry::instance().registerMonster(goblin);
    EXPECT_EQ(MonsterRegistry::instance().count(), 1u);
    const auto* g = MonsterRegistry::instance().getMonster("goblin");
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->name, "Goblin");
}

TEST_F(RegistryTest, GetNonExistentReturnsNull) {
    EXPECT_EQ(MonsterRegistry::instance().getMonster("dragon"), nullptr);
}

TEST_F(RegistryTest, HasMonster) {
    MonsterRegistry::instance().registerMonster(makeGoblin());
    EXPECT_TRUE(MonsterRegistry::instance().hasMonster("goblin"));
    EXPECT_FALSE(MonsterRegistry::instance().hasMonster("orc"));
}

TEST_F(RegistryTest, GetAllIds) {
    MonsterRegistry::instance().registerMonster(makeGoblin());

    MonsterDefinition orc;
    orc.id = "orc"; orc.name = "Orc"; orc.type = "humanoid";
    orc.tags = {"humanoid"};
    MonsterRegistry::instance().registerMonster(orc);

    auto ids = MonsterRegistry::instance().getAllIds();
    EXPECT_EQ(ids.size(), 2u);
}

TEST_F(RegistryTest, GetByTag) {
    MonsterRegistry::instance().registerMonster(makeGoblin()); // tags: humanoid, goblinoid

    MonsterDefinition skeleton;
    skeleton.id = "skeleton"; skeleton.name = "Skeleton"; skeleton.type = "undead";
    skeleton.tags = {"undead"};
    MonsterRegistry::instance().registerMonster(skeleton);

    auto humanoids = MonsterRegistry::instance().getByTag("humanoid");
    EXPECT_EQ(humanoids.size(), 1u);
    EXPECT_EQ(humanoids[0]->id, "goblin");

    auto goblins = MonsterRegistry::instance().getByTag("goblinoid");
    EXPECT_EQ(goblins.size(), 1u);

    auto undead = MonsterRegistry::instance().getByTag("undead");
    EXPECT_EQ(undead.size(), 1u);
    EXPECT_EQ(undead[0]->id, "skeleton");
}

TEST_F(RegistryTest, GetByCR) {
    auto goblin = makeGoblin(); // CR 0.25
    MonsterRegistry::instance().registerMonster(goblin);

    MonsterDefinition troll;
    troll.id = "troll"; troll.name = "Troll"; troll.type = "giant";
    troll.challengeRating = 5.0f; troll.tags = {"giant"};
    MonsterRegistry::instance().registerMonster(troll);

    auto lowCR = MonsterRegistry::instance().getByCR(0.0f, 1.0f);
    EXPECT_EQ(lowCR.size(), 1u);
    EXPECT_EQ(lowCR[0]->id, "goblin");

    auto highCR = MonsterRegistry::instance().getByCR(4.0f, 10.0f);
    EXPECT_EQ(highCR.size(), 1u);
    EXPECT_EQ(highCR[0]->id, "troll");

    auto all = MonsterRegistry::instance().getByCR(0.0f, 30.0f);
    EXPECT_EQ(all.size(), 2u);
}

TEST_F(RegistryTest, GetByType) {
    MonsterRegistry::instance().registerMonster(makeGoblin()); // type: humanoid
    MonsterDefinition wolf;
    wolf.id = "wolf"; wolf.name = "Wolf"; wolf.type = "beast"; wolf.tags = {"beast"};
    MonsterRegistry::instance().registerMonster(wolf);

    auto humanoids = MonsterRegistry::instance().getByType("humanoid");
    EXPECT_EQ(humanoids.size(), 1u);

    auto beasts = MonsterRegistry::instance().getByType("beast");
    EXPECT_EQ(beasts.size(), 1u);

    auto none = MonsterRegistry::instance().getByType("dragon");
    EXPECT_TRUE(none.empty());
}

TEST_F(RegistryTest, LoadFromJson_SingleObject) {
    auto j = makeGoblin().toJson();
    int count = MonsterRegistry::instance().loadFromJson(j);
    EXPECT_EQ(count, 1);
    EXPECT_TRUE(MonsterRegistry::instance().hasMonster("goblin"));
}

TEST_F(RegistryTest, LoadFromJson_Array) {
    auto goblin = makeGoblin().toJson();

    MonsterDefinition orc;
    orc.id = "orc"; orc.name = "Orc"; orc.type = "humanoid";
    auto orcj = orc.toJson();

    nlohmann::json arr = nlohmann::json::array();
    arr.push_back(goblin);
    arr.push_back(orcj);

    int count = MonsterRegistry::instance().loadFromJson(arr);
    EXPECT_EQ(count, 2);
    EXPECT_TRUE(MonsterRegistry::instance().hasMonster("goblin"));
    EXPECT_TRUE(MonsterRegistry::instance().hasMonster("orc"));
}

TEST_F(RegistryTest, ClearResetsRegistry) {
    MonsterRegistry::instance().registerMonster(makeGoblin());
    EXPECT_EQ(MonsterRegistry::instance().count(), 1u);
    MonsterRegistry::instance().clear();
    EXPECT_EQ(MonsterRegistry::instance().count(), 0u);
    EXPECT_FALSE(MonsterRegistry::instance().hasMonster("goblin"));
}

TEST_F(RegistryTest, DuplicateRegistrationOverwrites) {
    auto goblin = makeGoblin();
    MonsterRegistry::instance().registerMonster(goblin);
    goblin.armorClass = 999; // modify
    MonsterRegistry::instance().registerMonster(goblin);
    EXPECT_EQ(MonsterRegistry::instance().count(), 1u); // still one
    EXPECT_EQ(MonsterRegistry::instance().getMonster("goblin")->armorClass, 999);
}
