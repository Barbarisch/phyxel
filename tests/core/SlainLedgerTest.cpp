// SlainLedgerTest.cpp -- cleared enemies stay cleared (G-171).
//
// User, 2026-09-23: "Wolf enemies respawn when coming back to the hallow farm". The farm's
// wolves are authored NPCs in the scene's `npcs` list, so re-entering the scene simply re-runs
// the list; the `wolves_cleared` story variable the victory sets is consumed by nothing. These
// pin the ledger that records a kill and the loader rule that honours it.

#include <gtest/gtest.h>

#include "core/SlainLedger.h"
#include "core/GameDefinitionLoader.h"
#include "core/NPCManager.h"

#include <string>
#include <vector>

using namespace Phyxel::Core;
using json = nlohmann::json;

// ---------------------------------------------------------------- the ledger itself

TEST(SlainLedger, RecordsAndRecallsAKillPerScene) {
    SlainLedger l;
    EXPECT_FALSE(l.contains("farm", "npc_Wolf"));
    l.record("farm", "npc_Wolf");
    EXPECT_TRUE(l.contains("farm", "npc_Wolf"));
    EXPECT_FALSE(l.contains("barrow", "npc_Wolf"))
        << "ids are only unique within a scene: a farm wolf must not shadow a barrow wolf";
    EXPECT_FALSE(l.contains("farm", "npc_Wolf2"));
}

TEST(SlainLedger, IgnoresAnEmptyIdAndClearsOnRequest) {
    SlainLedger l;
    l.record("farm", "");
    EXPECT_EQ(l.size(), 0u) << "an empty id is a bug upstream, not a corpse";
    l.record("farm", "npc_Wolf");
    l.record("farm", "npc_Wolf");   // idempotent
    EXPECT_EQ(l.size(), 1u);
    l.clear();
    EXPECT_EQ(l.size(), 0u);
}

TEST(SlainLedger, RoundTripsThroughJsonSoTheSaveCarriesIt) {
    SlainLedger l;
    l.record("farm", "npc_Wolf");
    l.record("farm", "npc_AlphaWolf");
    l.record("", "npc_Rat");   // single-scene game
    SlainLedger back;
    back.fromJson(l.toJson());
    EXPECT_TRUE(back.contains("farm", "npc_Wolf"));
    EXPECT_TRUE(back.contains("farm", "npc_AlphaWolf"));
    EXPECT_TRUE(back.contains("", "npc_Rat"));
    EXPECT_EQ(back.size(), 3u);
    SlainLedger junk;
    junk.fromJson(json{{"not", "an array"}});
    EXPECT_EQ(junk.size(), 0u) << "a malformed save must not throw or invent kills";
}

// ---------------------------------------------------------------- the loader honours it

namespace {

// A scene with three wolves and a farmer, the shape of Ravenmere's farm.
json farmNpcs() {
    return json::array({
        {{"name", "Dunstan"}, {"position", {{"x", 0}, {"y", 20}, {"z", 0}}}},
        {{"name", "Wolf"},    {"position", {{"x", 5}, {"y", 20}, {"z", 0}}}},
        {{"name", "Wolf2"},   {"position", {{"x", 6}, {"y", 20}, {"z", 0}}}},
        {{"name", "AlphaWolf"}, {"position", {{"x", 7}, {"y", 20}, {"z", 0}}}},
    });
}

struct SpawnLog {
    std::vector<std::string> spawned;   // entity types requested through the spawner
};

// Wire a GameSubsystems that can run loadNPCs without a renderer. A bare NPCManager returns
// null from spawnNPC (there is no scene to spawn into), so `npcsSpawned` is 0 for EVERY NPC in
// these tests regardless of the ledger. The skip decision runs BEFORE the spawn call, so the
// observable that pins the rule is `npcsSkippedSlain`; whether the survivors really spawn is
// what the shipped-build L4 (tools/rv_slain_l4.py) proves.
GameSubsystems subsystemsWith(NPCManager& npcs, SpawnLog& log, const SlainLedger* ledger,
                              const std::string& sceneId) {
    GameSubsystems sub;
    sub.npcManager = &npcs;
    sub.entitySpawner = [&log](const std::string& type, const glm::vec3&, const std::string&)
        -> Phyxel::Scene::Entity* { log.spawned.push_back(type); return nullptr; };
    sub.activeSceneIdForLoad = sceneId;
    if (ledger)
        sub.isNpcSlain = [ledger](const std::string& scene, const std::string& id) {
            return ledger->contains(scene, id);
        };
    return sub;
}

}  // namespace

TEST(SlainLedger, LoaderSpawnsEveryoneWhenNothingIsRecorded) {
    NPCManager npcs; SpawnLog log;
    auto sub = subsystemsWith(npcs, log, nullptr, "farm");
    auto result = GameDefinitionLoader::load(json{{"npcs", farmNpcs()}}, sub);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.npcsSkippedSlain, 0) << "nothing recorded, nothing skipped";
}

TEST(SlainLedger, LoaderSkipsTheSlainAndReportsHowMany) {
    // The bug: three dead wolves came back. With the kills recorded, only the farmer spawns.
    SlainLedger ledger;
    ledger.record("farm", "npc_Wolf");
    ledger.record("farm", "npc_Wolf2");
    ledger.record("farm", "npc_AlphaWolf");
    NPCManager npcs; SpawnLog log;
    auto sub = subsystemsWith(npcs, log, &ledger, "farm");
    auto result = GameDefinitionLoader::load(json{{"npcs", farmNpcs()}}, sub);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.npcsSkippedSlain, 3)
        << "the three wolves must be skipped, and the skip must be COUNTED, not silent "
           "(Dunstan is not recorded, so he is not among them)";
}

TEST(SlainLedger, ASceneThatShouldNotSeeAnotherScenesKillsDoesNot) {
    SlainLedger ledger;
    ledger.record("barrow", "npc_Wolf");   // a different Wolf, elsewhere
    NPCManager npcs; SpawnLog log;
    auto sub = subsystemsWith(npcs, log, &ledger, "farm");
    auto result = GameDefinitionLoader::load(json{{"npcs", farmNpcs()}}, sub);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.npcsSkippedSlain, 0) << "the farm wolf is alive; the barrow wolf is the dead one";
}

TEST(SlainLedger, AnNpcAuthoredToRespawnComesBackEvenWhenRecorded) {
    // Wildlife and guards opt in: "respawns": true. The ledger is consulted and ignored.
    SlainLedger ledger;
    ledger.record("farm", "npc_Wolf");
    json npcsDef = farmNpcs();
    npcsDef[1]["respawns"] = true;   // Wolf
    NPCManager npcs; SpawnLog log;
    auto sub = subsystemsWith(npcs, log, &ledger, "farm");
    auto result = GameDefinitionLoader::load(json{{"npcs", npcsDef}}, sub);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.npcsSkippedSlain, 0) << "recorded, but authored to respawn: not skipped";
}

TEST(SlainLedger, WithoutALedgerHookTheLoaderBehavesAsBefore) {
    // The editor, and any host that never wires isNpcSlain, must see no change at all.
    NPCManager npcs; SpawnLog log;
    auto sub = subsystemsWith(npcs, log, nullptr, "");
    auto result = GameDefinitionLoader::load(json{{"npcs", farmNpcs()}}, sub);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.npcsSkippedSlain, 0) << "no hook, no skipping - the editor sees no change";
}
