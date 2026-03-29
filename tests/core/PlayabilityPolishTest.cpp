#include <gtest/gtest.h>
#include "core/HealthComponent.h"
#include "core/RespawnSystem.h"
#include "core/MusicPlaylist.h"
#include "core/ObjectiveTracker.h"
#include "core/PlayerProfile.h"

using namespace Phyxel::Core;

// ================================================================
// HealthComponent - Death Callback
// ================================================================

TEST(HealthComponentCallbackTest, OnDeathCallbackFiresOnKill) {
    HealthComponent health(100.0f);
    bool called = false;
    health.setOnDeathCallback([&]() { called = true; });
    health.kill();
    EXPECT_TRUE(called);
    EXPECT_FALSE(health.isAlive());
}

TEST(HealthComponentCallbackTest, OnDeathCallbackFiresOnFatalDamage) {
    HealthComponent health(50.0f);
    bool called = false;
    health.setOnDeathCallback([&]() { called = true; });
    health.takeDamage(100.0f);
    EXPECT_TRUE(called);
    EXPECT_FALSE(health.isAlive());
}

TEST(HealthComponentCallbackTest, OnDeathCallbackNotFiredOnNonFatalDamage) {
    HealthComponent health(100.0f);
    bool called = false;
    health.setOnDeathCallback([&]() { called = true; });
    health.takeDamage(50.0f);
    EXPECT_FALSE(called);
    EXPECT_TRUE(health.isAlive());
}

TEST(HealthComponentCallbackTest, NoCallbackDoesNotCrash) {
    HealthComponent health(100.0f);
    // No callback set — should not crash
    health.kill();
    EXPECT_FALSE(health.isAlive());
}

// ================================================================
// RespawnSystem
// ================================================================

TEST(RespawnSystemTest, DefaultState) {
    RespawnSystem rs;
    EXPECT_FALSE(rs.isPlayerDead());
    EXPECT_EQ(rs.getDeathCount(), 0);
    EXPECT_FLOAT_EQ(rs.getRespawnDelay(), 3.0f);
}

TEST(RespawnSystemTest, StartDeathSequence) {
    RespawnSystem rs;
    rs.startDeathSequence();
    EXPECT_TRUE(rs.isPlayerDead());
    EXPECT_EQ(rs.getDeathCount(), 1);
    EXPECT_FLOAT_EQ(rs.getDeathTimer(), 0.0f);
}

TEST(RespawnSystemTest, MultipleDeathsIncrementCount) {
    RespawnSystem rs;
    rs.startDeathSequence();
    rs.respawn();
    rs.startDeathSequence();
    rs.respawn();
    rs.startDeathSequence();
    EXPECT_EQ(rs.getDeathCount(), 3);
}

TEST(RespawnSystemTest, UpdateAdvancesTimer) {
    RespawnSystem rs;
    rs.startDeathSequence();
    rs.update(1.0f);
    EXPECT_FLOAT_EQ(rs.getDeathTimer(), 1.0f);
    EXPECT_TRUE(rs.isPlayerDead());  // Not yet at 3s delay
}

TEST(RespawnSystemTest, AutoRespawnWhenTimerExpires) {
    RespawnSystem rs;
    bool respawned = false;
    rs.setOnRespawnCallback([&](const glm::vec3&) { respawned = true; });
    rs.startDeathSequence();
    rs.update(4.0f);  // Exceeds 3s delay
    EXPECT_FALSE(rs.isPlayerDead());
    EXPECT_TRUE(respawned);
}

TEST(RespawnSystemTest, RespawnCallbackReceivesSpawnPoint) {
    RespawnSystem rs;
    glm::vec3 receivedPos(0.0f);
    rs.setSpawnPoint(glm::vec3(100.0f, 200.0f, 300.0f));
    rs.setOnRespawnCallback([&](const glm::vec3& pos) { receivedPos = pos; });
    rs.startDeathSequence();
    rs.respawn();
    EXPECT_FLOAT_EQ(receivedPos.x, 100.0f);
    EXPECT_FLOAT_EQ(receivedPos.y, 200.0f);
    EXPECT_FLOAT_EQ(receivedPos.z, 300.0f);
}

TEST(RespawnSystemTest, SetRespawnDelay) {
    RespawnSystem rs;
    rs.setRespawnDelay(5.0f);
    EXPECT_FLOAT_EQ(rs.getRespawnDelay(), 5.0f);
}

TEST(RespawnSystemTest, CustomDelayAutoRespawn) {
    RespawnSystem rs;
    bool respawned = false;
    rs.setRespawnDelay(1.0f);
    rs.setOnRespawnCallback([&](const glm::vec3&) { respawned = true; });
    rs.startDeathSequence();
    rs.update(0.5f);
    EXPECT_TRUE(rs.isPlayerDead());
    EXPECT_FALSE(respawned);
    rs.update(0.6f);
    EXPECT_FALSE(rs.isPlayerDead());
    EXPECT_TRUE(respawned);
}

TEST(RespawnSystemTest, NoUpdateWhenAlive) {
    RespawnSystem rs;
    bool respawned = false;
    rs.setOnRespawnCallback([&](const glm::vec3&) { respawned = true; });
    rs.update(10.0f);  // Not dead, should do nothing
    EXPECT_FALSE(respawned);
    EXPECT_FALSE(rs.isPlayerDead());
}

TEST(RespawnSystemTest, ToJson) {
    RespawnSystem rs;
    rs.setSpawnPoint(glm::vec3(5.0f, 10.0f, 15.0f));
    rs.startDeathSequence();
    auto j = rs.toJson();
    EXPECT_TRUE(j["playerDead"].get<bool>());
    EXPECT_EQ(j["deathCount"].get<int>(), 1);
    EXPECT_FLOAT_EQ(j["spawnPoint"]["x"].get<float>(), 5.0f);
}

// ================================================================
// MusicPlaylist
// ================================================================

TEST(MusicPlaylistTest, DefaultState) {
    MusicPlaylist pl;
    EXPECT_FALSE(pl.isPlaying());
    EXPECT_EQ(pl.getCurrentIndex(), 0);
    EXPECT_TRUE(pl.getTracks().empty());
    EXPECT_FLOAT_EQ(pl.getVolume(), 0.5f);
}

TEST(MusicPlaylistTest, AddAndRemoveTracks) {
    MusicPlaylist pl;
    pl.addTrack("track1.wav");
    pl.addTrack("track2.wav");
    EXPECT_EQ(pl.getTracks().size(), 2u);
    pl.removeTrack("track1.wav");
    EXPECT_EQ(pl.getTracks().size(), 1u);
    EXPECT_EQ(pl.getTracks()[0], "track2.wav");
}

TEST(MusicPlaylistTest, Clear) {
    MusicPlaylist pl;
    pl.addTrack("a.wav");
    pl.addTrack("b.wav");
    pl.clear();
    EXPECT_TRUE(pl.getTracks().empty());
    EXPECT_FALSE(pl.isPlaying());
}

TEST(MusicPlaylistTest, SetVolume) {
    MusicPlaylist pl;
    pl.setVolume(0.8f);
    EXPECT_FLOAT_EQ(pl.getVolume(), 0.8f);
    pl.setVolume(2.0f);  // Clamped
    EXPECT_FLOAT_EQ(pl.getVolume(), 1.0f);
    pl.setVolume(-1.0f);  // Clamped
    EXPECT_FLOAT_EQ(pl.getVolume(), 0.0f);
}

TEST(MusicPlaylistTest, SetMode) {
    MusicPlaylist pl;
    EXPECT_EQ(pl.getMode(), MusicPlaylist::Mode::Sequential);
    pl.setMode(MusicPlaylist::Mode::Shuffle);
    EXPECT_EQ(pl.getMode(), MusicPlaylist::Mode::Shuffle);
}

TEST(MusicPlaylistTest, GetCurrentTrack) {
    MusicPlaylist pl;
    EXPECT_EQ(pl.getCurrentTrack(), "");
    pl.addTrack("music.wav");
    EXPECT_EQ(pl.getCurrentTrack(), "music.wav");
}

TEST(MusicPlaylistTest, PlayWithNoAudioDoesNotCrash) {
    MusicPlaylist pl;
    pl.addTrack("test.wav");
    pl.play(nullptr);  // No audio system — should not crash
    EXPECT_TRUE(pl.isPlaying());  // State is set even without audio
}

TEST(MusicPlaylistTest, StopWithNoAudio) {
    MusicPlaylist pl;
    pl.addTrack("test.wav");
    pl.play(nullptr);
    pl.stop(nullptr);
    EXPECT_FALSE(pl.isPlaying());
}

TEST(MusicPlaylistTest, ToJson) {
    MusicPlaylist pl;
    pl.addTrack("track1.wav");
    pl.addTrack("track2.wav");
    pl.setVolume(0.7f);
    auto j = pl.toJson();
    EXPECT_EQ(j["trackCount"].get<int>(), 2);
    EXPECT_FLOAT_EQ(j["volume"].get<float>(), 0.7f);
    EXPECT_FALSE(j["playing"].get<bool>());
    EXPECT_EQ(j["mode"].get<std::string>(), "sequential");
}

// ================================================================
// ObjectiveTracker
// ================================================================

TEST(ObjectiveTrackerTest, DefaultEmpty) {
    ObjectiveTracker tracker;
    EXPECT_EQ(tracker.getTotalCount(), 0);
    EXPECT_EQ(tracker.getCompletedCount(), 0);
    EXPECT_TRUE(tracker.getActiveObjectives().empty());
}

TEST(ObjectiveTrackerTest, AddObjective) {
    ObjectiveTracker tracker;
    bool ok = tracker.addObjective("obj1", "Find the key");
    EXPECT_TRUE(ok);
    EXPECT_EQ(tracker.getTotalCount(), 1);
    auto* obj = tracker.getObjective("obj1");
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->title, "Find the key");
    EXPECT_EQ(obj->status, Objective::Status::Active);
}

TEST(ObjectiveTrackerTest, AddDuplicateObjective) {
    ObjectiveTracker tracker;
    tracker.addObjective("obj1", "First");
    bool ok = tracker.addObjective("obj1", "Duplicate");
    EXPECT_FALSE(ok);
    EXPECT_EQ(tracker.getTotalCount(), 1);
}

TEST(ObjectiveTrackerTest, CompleteObjective) {
    ObjectiveTracker tracker;
    tracker.addObjective("obj1", "Find the key");
    bool ok = tracker.completeObjective("obj1");
    EXPECT_TRUE(ok);
    EXPECT_EQ(tracker.getCompletedCount(), 1);
    auto* obj = tracker.getObjective("obj1");
    EXPECT_EQ(obj->status, Objective::Status::Completed);
}

TEST(ObjectiveTrackerTest, CompleteNonExistent) {
    ObjectiveTracker tracker;
    bool ok = tracker.completeObjective("nope");
    EXPECT_FALSE(ok);
}

TEST(ObjectiveTrackerTest, FailObjective) {
    ObjectiveTracker tracker;
    tracker.addObjective("obj1", "Escape");
    bool ok = tracker.failObjective("obj1");
    EXPECT_TRUE(ok);
    auto* obj = tracker.getObjective("obj1");
    EXPECT_EQ(obj->status, Objective::Status::Failed);
}

TEST(ObjectiveTrackerTest, RemoveObjective) {
    ObjectiveTracker tracker;
    tracker.addObjective("obj1", "Test");
    tracker.addObjective("obj2", "Test2");
    bool ok = tracker.removeObjective("obj1");
    EXPECT_TRUE(ok);
    EXPECT_EQ(tracker.getTotalCount(), 1);
    EXPECT_EQ(tracker.getObjective("obj1"), nullptr);
}

TEST(ObjectiveTrackerTest, GetActiveObjectives) {
    ObjectiveTracker tracker;
    tracker.addObjective("obj1", "Active one");
    tracker.addObjective("obj2", "Completed one");
    tracker.addObjective("obj3", "Hidden one", "", "main", 0, true);
    tracker.completeObjective("obj2");
    auto active = tracker.getActiveObjectives();
    EXPECT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0]->id, "obj1");
}

TEST(ObjectiveTrackerTest, PrioritySorting) {
    ObjectiveTracker tracker;
    tracker.addObjective("low", "Low prio", "", "main", 1);
    tracker.addObjective("high", "High prio", "", "main", 10);
    tracker.addObjective("mid", "Mid prio", "", "main", 5);
    auto active = tracker.getActiveObjectives();
    EXPECT_EQ(active[0]->id, "high");
    EXPECT_EQ(active[1]->id, "mid");
    EXPECT_EQ(active[2]->id, "low");
}

TEST(ObjectiveTrackerTest, Clear) {
    ObjectiveTracker tracker;
    tracker.addObjective("obj1", "Test");
    tracker.completeObjective("obj1");
    tracker.clear();
    EXPECT_EQ(tracker.getTotalCount(), 0);
    EXPECT_EQ(tracker.getCompletedCount(), 0);
}

TEST(ObjectiveTrackerTest, ToJsonAndFromJson) {
    ObjectiveTracker tracker;
    tracker.addObjective("obj1", "Find key", "Look in cave", "main", 5);
    tracker.addObjective("obj2", "Defeat boss", "", "main", 10);
    tracker.completeObjective("obj2");

    nlohmann::json j = tracker.toJson();
    EXPECT_EQ(j["totalCount"].get<int>(), 2);
    EXPECT_EQ(j["completedCount"].get<int>(), 1);

    // Round-trip
    ObjectiveTracker tracker2;
    tracker2.fromJson(j);
    EXPECT_EQ(tracker2.getTotalCount(), 2);
    EXPECT_EQ(tracker2.getCompletedCount(), 1);
    auto* obj = tracker2.getObjective("obj1");
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->title, "Find key");
    EXPECT_EQ(obj->priority, 5);
}

TEST(ObjectiveTrackerTest, GetAllObjectives) {
    ObjectiveTracker tracker;
    tracker.addObjective("a", "First");
    tracker.addObjective("b", "Second");
    tracker.addObjective("c", "Third");
    auto all = tracker.getAllObjectives();
    EXPECT_EQ(all.size(), 3u);
}

// ================================================================
// PlayerProfile
// ================================================================

TEST(PlayerProfileTest, DefaultState) {
    PlayerProfile profile;
    EXPECT_FLOAT_EQ(profile.health, 100.0f);
    EXPECT_FLOAT_EQ(profile.maxHealth, 100.0f);
    EXPECT_EQ(profile.deathCount, 0);
}

TEST(PlayerProfileTest, ToJsonAndFromJson) {
    PlayerProfile profile;
    profile.cameraPosition = glm::vec3(10.0f, 20.0f, 30.0f);
    profile.cameraYaw = 45.0f;
    profile.cameraPitch = -15.0f;
    profile.health = 75.0f;
    profile.spawnPoint = glm::vec3(5.0f, 10.0f, 15.0f);
    profile.deathCount = 3;

    auto j = profile.toJson();
    
    PlayerProfile profile2;
    profile2.fromJson(j);
    EXPECT_FLOAT_EQ(profile2.cameraPosition.x, 10.0f);
    EXPECT_FLOAT_EQ(profile2.cameraPosition.y, 20.0f);
    EXPECT_FLOAT_EQ(profile2.cameraPosition.z, 30.0f);
    EXPECT_FLOAT_EQ(profile2.cameraYaw, 45.0f);
    EXPECT_FLOAT_EQ(profile2.cameraPitch, -15.0f);
    EXPECT_FLOAT_EQ(profile2.health, 75.0f);
    EXPECT_FLOAT_EQ(profile2.spawnPoint.x, 5.0f);
    EXPECT_EQ(profile2.deathCount, 3);
}

TEST(PlayerProfileTest, FromJsonPartial) {
    PlayerProfile profile;
    nlohmann::json j = {{"health", 50.0f}, {"deathCount", 7}};
    profile.fromJson(j);
    EXPECT_FLOAT_EQ(profile.health, 50.0f);
    EXPECT_EQ(profile.deathCount, 7);
    // Camera should retain defaults
    EXPECT_FLOAT_EQ(profile.cameraPosition.x, 50.0f);
}

TEST(PlayerProfileTest, SaveAndLoadSqlite) {
    // In-memory SQLite database
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    PlayerProfile profile;
    profile.health = 42.0f;
    profile.cameraPosition = glm::vec3(1.0f, 2.0f, 3.0f);
    profile.deathCount = 5;
    EXPECT_TRUE(profile.saveToDb(db));

    PlayerProfile loaded;
    EXPECT_TRUE(loaded.loadFromDb(db));
    EXPECT_FLOAT_EQ(loaded.health, 42.0f);
    EXPECT_FLOAT_EQ(loaded.cameraPosition.x, 1.0f);
    EXPECT_EQ(loaded.deathCount, 5);

    sqlite3_close(db);
}

TEST(PlayerProfileTest, LoadFromEmptyDb) {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    PlayerProfile profile;
    EXPECT_FALSE(profile.loadFromDb(db));

    sqlite3_close(db);
}

TEST(PlayerProfileTest, SaveOverwritesPrevious) {
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    PlayerProfile p1;
    p1.health = 100.0f;
    p1.saveToDb(db);

    PlayerProfile p2;
    p2.health = 25.0f;
    p2.saveToDb(db);

    PlayerProfile loaded;
    loaded.loadFromDb(db);
    EXPECT_FLOAT_EQ(loaded.health, 25.0f);

    sqlite3_close(db);
}

TEST(PlayerProfileTest, NullDbReturnsFailure) {
    PlayerProfile profile;
    EXPECT_FALSE(profile.saveToDb(nullptr));
    EXPECT_FALSE(profile.loadFromDb(nullptr));
}
