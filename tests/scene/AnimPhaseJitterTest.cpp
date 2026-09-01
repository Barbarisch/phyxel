#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"

#include <cstdio>
#include <set>
#include <string>

using namespace Phyxel;
using Anim = Scene::AnimatedVoxelCharacter;

// ============================================================================
// Looping locomotion must NOT start at phase 0 for every character.
//
// The report was that a 200-body battle "moves in sync as if they are a single
// robot controlled entity". There is no shared controller: entering a clip set
// animTime = 0 unconditionally, so a crowd that switched to Walk on the same
// frame advanced by the same dt forever and never drifted apart.
//
// These pin the SEEDING CONTRACT via the static helpers, so no physics world or
// loaded rig is needed. The visual half — that an army no longer marches in
// lockstep — is a runtime observation a unit test cannot make.
// ============================================================================

TEST(AnimPhaseJitterTest, SeedIsStableForTheSameId) {
    // Stability is why this is hashed rather than random: a character that
    // re-rolled its phase every time it re-entered Walk would visibly twitch.
    EXPECT_FLOAT_EQ(Anim::phaseSeedForId("npc_crimson_M017"),
                    Anim::phaseSeedForId("npc_crimson_M017"));
}

TEST(AnimPhaseJitterTest, AdjacentIdsGetDifferentPhases) {
    EXPECT_NE(Anim::phaseSeedForId("npc_crimson_M000"),
              Anim::phaseSeedForId("npc_crimson_M001"))
        << "adjacent ids collided — neighbours in the line would step together";
}

TEST(AnimPhaseJitterTest, SeedsAreAlreadyInUnitRange) {
    for (int i = 0; i < 200; ++i) {
        char id[64];
        std::snprintf(id, sizeof(id), "npc_azure_C%03d", i);
        const float p = Anim::phaseSeedForId(id);
        ASSERT_GE(p, 0.0f) << id;
        ASSERT_LT(p, 1.0f) << id;
    }
}

TEST(AnimPhaseJitterTest, WrapHandlesOutOfRangeAndNegativeSeeds) {
    // Callers may hand in any float. A phase outside [0,1) multiplies by clip
    // duration into a seek past the end of the clip.
    for (float s : {3.75f, -0.25f, 1.0f, 12345.5f, -7.5f}) {
        const float w = Anim::wrapPhase(s);
        EXPECT_GE(w, 0.0f) << "seed " << s;
        EXPECT_LT(w, 1.0f) << "seed " << s;
    }
    EXPECT_FLOAT_EQ(Anim::wrapPhase(0.25f), 0.25f);
    EXPECT_FLOAT_EQ(Anim::wrapPhase(3.25f), 0.25f);
    EXPECT_FLOAT_EQ(Anim::wrapPhase(1.0f),  0.0f);
}

TEST(AnimPhaseJitterTest, AnArmyIsSpreadAcrossTheWholeCycle) {
    // THE test that matters. 120 combatants named the way the battle generator
    // names them must not clump: bucket their phases into tenths of the cycle
    // and require most buckets occupied. A formula mapping every id onto a
    // handful of values would pass "adjacent ids differ" above and still leave
    // the army visibly marching in step.
    std::set<int> buckets;
    for (int i = 0; i < 120; ++i) {
        char id[64];
        std::snprintf(id, sizeof(id), "npc_crimson_M%03d", i);
        buckets.insert(static_cast<int>(Anim::phaseSeedForId(id) * 10.0f));
    }
    EXPECT_GE(buckets.size(), 8u)
        << "120 fighters occupy only " << buckets.size()
        << "/10 phase buckets — they would still visibly step together";
}

TEST(AnimPhaseJitterTest, TwoArmiesDoNotSyncWithEachOther) {
    // Both sides spawn from the same generator with only the faction prefix
    // differing. If the hash were dominated by the numeric suffix, crimson_M004
    // and azure_M004 would share a phase and the two lines would mirror each
    // other — a subtler version of the same defect.
    int same = 0;
    for (int i = 0; i < 60; ++i) {
        char a[64], b[64];
        std::snprintf(a, sizeof(a), "npc_crimson_M%03d", i);
        std::snprintf(b, sizeof(b), "npc_azure_M%03d", i);
        if (Anim::phaseSeedForId(a) == Anim::phaseSeedForId(b)) ++same;
    }
    EXPECT_LE(same, 1) << same << "/60 opposing pairs share a phase";
}
