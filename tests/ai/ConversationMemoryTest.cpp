#include <gtest/gtest.h>
#include "ai/ConversationMemory.h"

#ifdef ENABLE_WORLD_STORAGE
#include <sqlite3.h>
#endif

using namespace Phyxel::AI;

// ============================================================================
// ConversationMemory Tests (using in-memory SQLite database)
// ============================================================================

class ConversationMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifdef ENABLE_WORLD_STORAGE
        int rc = sqlite3_open(":memory:", &db);
        ASSERT_EQ(rc, SQLITE_OK);
        memory = std::make_unique<ConversationMemory>();
        ASSERT_TRUE(memory->initialize(db));
#endif
    }

    void TearDown() override {
#ifdef ENABLE_WORLD_STORAGE
        memory.reset();
        if (db) sqlite3_close(db);
#endif
    }

    sqlite3* db = nullptr;
    std::unique_ptr<ConversationMemory> memory;
};

TEST_F(ConversationMemoryTest, InitializesSuccessfully) {
#ifdef ENABLE_WORLD_STORAGE
    EXPECT_TRUE(memory->isInitialized());
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(ConversationMemoryTest, NullDbFailsInit) {
    ConversationMemory mem;
    EXPECT_FALSE(mem.initialize(nullptr));
    EXPECT_FALSE(mem.isInitialized());
}

TEST_F(ConversationMemoryTest, RecordAndRetrieveTurns) {
#ifdef ENABLE_WORLD_STORAGE
    memory->recordTurn("npc_01", "Player", "Hello there!", "", 1.0f, 100.0f);
    memory->recordTurn("npc_01", "Guard", "Halt! Who goes there?", "suspicious", 1.0f, 100.5f);

    auto turns = memory->getRecentTurns("npc_01", 10);
    ASSERT_EQ(turns.size(), 2);
    EXPECT_EQ(turns[0].speaker, "Player");
    EXPECT_EQ(turns[0].text, "Hello there!");
    EXPECT_EQ(turns[1].speaker, "Guard");
    EXPECT_EQ(turns[1].text, "Halt! Who goes there?");
    EXPECT_EQ(turns[1].emotion, "suspicious");
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(ConversationMemoryTest, TotalTurns) {
#ifdef ENABLE_WORLD_STORAGE
    EXPECT_EQ(memory->getTotalTurns("npc_01"), 0);

    memory->recordTurn("npc_01", "Player", "Hi", "", 1.0f, 1.0f);
    memory->recordTurn("npc_01", "Guard", "Hey", "", 1.0f, 2.0f);
    memory->recordTurn("npc_02", "Player", "Yo", "", 1.0f, 3.0f);

    EXPECT_EQ(memory->getTotalTurns("npc_01"), 2);
    EXPECT_EQ(memory->getTotalTurns("npc_02"), 1);
    EXPECT_EQ(memory->getTotalTurns("npc_03"), 0);
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(ConversationMemoryTest, RecentTurnsLimit) {
#ifdef ENABLE_WORLD_STORAGE
    for (int i = 0; i < 10; i++) {
        memory->recordTurn("npc_01", "Speaker", "Turn " + std::to_string(i), "", 1.0f,
                           static_cast<float>(i));
    }

    auto turns = memory->getRecentTurns("npc_01", 3);
    ASSERT_EQ(turns.size(), 3);
    // Should be most recent 3, in chronological order (newest last)
    EXPECT_EQ(turns[0].text, "Turn 7");
    EXPECT_EQ(turns[1].text, "Turn 8");
    EXPECT_EQ(turns[2].text, "Turn 9");
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(ConversationMemoryTest, SeparateNPCConversations) {
#ifdef ENABLE_WORLD_STORAGE
    memory->recordTurn("guard", "Player", "Hello guard", "", 1.0f, 1.0f);
    memory->recordTurn("merchant", "Player", "Got any wares?", "", 1.0f, 2.0f);

    auto guardTurns = memory->getRecentTurns("guard", 10);
    auto merchantTurns = memory->getRecentTurns("merchant", 10);

    ASSERT_EQ(guardTurns.size(), 1);
    ASSERT_EQ(merchantTurns.size(), 1);
    EXPECT_EQ(guardTurns[0].text, "Hello guard");
    EXPECT_EQ(merchantTurns[0].text, "Got any wares?");
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(ConversationMemoryTest, EmptySummaryForNewNPC) {
#ifdef ENABLE_WORLD_STORAGE
    std::string summary = memory->getSummary("never_spoken_to");
    EXPECT_TRUE(summary.empty());
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(ConversationMemoryTest, PruneOldTurns) {
#ifdef ENABLE_WORLD_STORAGE
    for (int i = 0; i < 20; i++) {
        memory->recordTurn("npc_01", "Speaker", "Turn " + std::to_string(i), "", 1.0f,
                           static_cast<float>(i));
    }
    EXPECT_EQ(memory->getTotalTurns("npc_01"), 20);

    memory->pruneOldTurns(5);
    EXPECT_EQ(memory->getTotalTurns("npc_01"), 5);

    auto turns = memory->getRecentTurns("npc_01", 10);
    ASSERT_EQ(turns.size(), 5);
    EXPECT_EQ(turns[0].text, "Turn 15");
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}
