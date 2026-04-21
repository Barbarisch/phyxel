#include <gtest/gtest.h>
#include "core/CampaignJournal.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

TEST(CampaignJournalTest, EntryTypeRoundTrip) {
    const JournalEntryType types[] = {
        JournalEntryType::SessionNote, JournalEntryType::WorldEvent,
        JournalEntryType::CharacterEvent, JournalEntryType::QuestUpdate,
        JournalEntryType::Discovery
    };
    for (auto t : types) {
        const char* n = journalEntryTypeName(t);
        EXPECT_EQ(journalEntryTypeFromString(n), t) << "Failed for: " << n;
    }
}

TEST(CampaignJournalTest, EntryTypeFromStringCaseInsensitive) {
    EXPECT_EQ(journalEntryTypeFromString("sessionnote"),    JournalEntryType::SessionNote);
    EXPECT_EQ(journalEntryTypeFromString("WORLDEVENT"),     JournalEntryType::WorldEvent);
    EXPECT_EQ(journalEntryTypeFromString("Discovery"),      JournalEntryType::Discovery);
    EXPECT_EQ(journalEntryTypeFromString("unknown"),        JournalEntryType::WorldEvent);
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class JournalTest : public ::testing::Test {
protected:
    void SetUp() override {
        id1 = journal.addEntry(JournalEntryType::SessionNote,
            "Session 1", "Party arrived at Bridgewater.", 1, {"bridgewater", "travel"});
        id2 = journal.addEntry(JournalEntryType::WorldEvent,
            "Dragon Sighting", "A red dragon was spotted near the mountains.", 3, {"dragon", "danger"});
        id3 = journal.addEntry(JournalEntryType::QuestUpdate,
            "Quest: The Lost Sword", "Found a clue in the tavern.", 3, {"quest", "sword"});
    }
    CampaignJournal journal;
    int id1, id2, id3;
};

// ---------------------------------------------------------------------------
// addEntry / getEntry
// ---------------------------------------------------------------------------

TEST_F(JournalTest, AddEntryAssignsSequentialIds) {
    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);
    EXPECT_EQ(id3, 3);
}

TEST_F(JournalTest, GetEntryByIdExists) {
    const auto* e = journal.getEntry(id1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->title,     "Session 1");
    EXPECT_EQ(e->type,      JournalEntryType::SessionNote);
    EXPECT_EQ(e->dayNumber, 1);
}

TEST_F(JournalTest, GetEntryByIdNotFound) {
    EXPECT_EQ(journal.getEntry(999), nullptr);
}

TEST_F(JournalTest, EntryCount) {
    EXPECT_EQ(journal.entryCount(), 3);
}

// ---------------------------------------------------------------------------
// removeEntry
// ---------------------------------------------------------------------------

TEST_F(JournalTest, RemoveEntry) {
    journal.removeEntry(id2);
    EXPECT_EQ(journal.entryCount(), 2);
    EXPECT_EQ(journal.getEntry(id2), nullptr);
}

TEST_F(JournalTest, RemoveNonExistentNoOp) {
    journal.removeEntry(999);
    EXPECT_EQ(journal.entryCount(), 3);
}

// ---------------------------------------------------------------------------
// getEntriesByType
// ---------------------------------------------------------------------------

TEST_F(JournalTest, GetByTypeSessionNote) {
    auto results = journal.getEntriesByType(JournalEntryType::SessionNote);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->id, id1);
}

TEST_F(JournalTest, GetByTypeNotPresent) {
    auto results = journal.getEntriesByType(JournalEntryType::Discovery);
    EXPECT_TRUE(results.empty());
}

TEST_F(JournalTest, GetByTypeMultiple) {
    journal.addEntry(JournalEntryType::WorldEvent, "Another event", "content", 5, {});
    auto results = journal.getEntriesByType(JournalEntryType::WorldEvent);
    EXPECT_EQ(results.size(), 2u);
}

// ---------------------------------------------------------------------------
// getEntriesByDay
// ---------------------------------------------------------------------------

TEST_F(JournalTest, GetByDay) {
    auto day1 = journal.getEntriesByDay(1);
    EXPECT_EQ(day1.size(), 1u);
    EXPECT_EQ(day1[0]->id, id1);

    auto day3 = journal.getEntriesByDay(3);
    EXPECT_EQ(day3.size(), 2u);  // dragon + quest
}

TEST_F(JournalTest, GetByDayNoEntries) {
    EXPECT_TRUE(journal.getEntriesByDay(99).empty());
}

// ---------------------------------------------------------------------------
// getEntriesByTag
// ---------------------------------------------------------------------------

TEST_F(JournalTest, GetByTagExactMatch) {
    auto results = journal.getEntriesByTag("dragon");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->id, id2);
}

TEST_F(JournalTest, GetByTagMultipleEntries) {
    auto results = journal.getEntriesByTag("quest");
    EXPECT_EQ(results.size(), 1u);
}

TEST_F(JournalTest, GetByTagNotFound) {
    EXPECT_TRUE(journal.getEntriesByTag("nonexistent").empty());
}

// ---------------------------------------------------------------------------
// searchEntries
// ---------------------------------------------------------------------------

TEST_F(JournalTest, SearchInTitle) {
    auto results = journal.searchEntries("Dragon");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->id, id2);
}

TEST_F(JournalTest, SearchCaseInsensitive) {
    auto results = journal.searchEntries("dragon");
    EXPECT_EQ(results.size(), 1u);
}

TEST_F(JournalTest, SearchInContent) {
    auto results = journal.searchEntries("tavern");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->id, id3);
}

TEST_F(JournalTest, SearchNoMatch) {
    EXPECT_TRUE(journal.searchEntries("xyzzy").empty());
}

TEST_F(JournalTest, SearchMatchesMultiple) {
    // "the" appears in multiple entries
    auto results = journal.searchEntries("the");
    EXPECT_GE(results.size(), 1u);
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

TEST_F(JournalTest, ClearResetsAll) {
    journal.clear();
    EXPECT_EQ(journal.entryCount(), 0);
    // After clear, IDs restart from 1
    int newId = journal.addEntry(JournalEntryType::WorldEvent, "New", "content", 0, {});
    EXPECT_EQ(newId, 1);
}

// ---------------------------------------------------------------------------
// JournalEntry JSON
// ---------------------------------------------------------------------------

TEST(CampaignJournalTest, EntryJsonRoundTrip) {
    JournalEntry e;
    e.id        = 42;
    e.type      = JournalEntryType::Discovery;
    e.dayNumber = 15;
    e.title     = "Ancient Ruins";
    e.content   = "Discovered ruins of the old empire.";
    e.tags      = { "ruins", "history", "empire" };

    auto j = e.toJson();
    auto r = JournalEntry::fromJson(j);

    EXPECT_EQ(r.id,        42);
    EXPECT_EQ(r.type,      JournalEntryType::Discovery);
    EXPECT_EQ(r.dayNumber, 15);
    EXPECT_EQ(r.title,     "Ancient Ruins");
    EXPECT_EQ(r.content,   "Discovered ruins of the old empire.");
    ASSERT_EQ(r.tags.size(), 3u);
    EXPECT_EQ(r.tags[1],   "history");
}

// ---------------------------------------------------------------------------
// CampaignJournal JSON
// ---------------------------------------------------------------------------

TEST_F(JournalTest, JournalJsonRoundTrip) {
    auto j = journal.toJson();
    CampaignJournal j2;
    j2.fromJson(j);

    EXPECT_EQ(j2.entryCount(), 3);

    const auto* e = j2.getEntry(id2);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->title,     "Dragon Sighting");
    EXPECT_EQ(e->dayNumber, 3);
    EXPECT_EQ(e->tags.size(), 2u);

    // After loading, next ID should continue from where we left off
    int nextId = j2.addEntry(JournalEntryType::WorldEvent, "New", "content", 0, {});
    EXPECT_EQ(nextId, 4);
}
