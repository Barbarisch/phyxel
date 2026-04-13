#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// JournalEntryType
// ---------------------------------------------------------------------------

enum class JournalEntryType {
    SessionNote,     ///< GM or player session recap
    WorldEvent,      ///< Something that happened in the world
    CharacterEvent,  ///< Character-specific milestone or event
    QuestUpdate,     ///< Quest / objective state change
    Discovery        ///< New location, item, or lore discovered
};

const char*        journalEntryTypeName(JournalEntryType type);
JournalEntryType   journalEntryTypeFromString(const char* name);

// ---------------------------------------------------------------------------
// JournalEntry
// ---------------------------------------------------------------------------

struct JournalEntry {
    int                      id        = 0;
    JournalEntryType         type      = JournalEntryType::WorldEvent;
    int                      dayNumber = 0;   ///< In-world day (from WorldClock)
    std::string              title;
    std::string              content;
    std::vector<std::string> tags;

    nlohmann::json toJson() const;
    static JournalEntry fromJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// CampaignJournal
// ---------------------------------------------------------------------------

/// Append-only campaign event log. IDs are monotonically increasing integers.
class CampaignJournal {
public:
    // -----------------------------------------------------------------------
    // Write
    // -----------------------------------------------------------------------

    /// Add an entry. Returns the new entry's ID.
    int addEntry(JournalEntryType type,
                 const std::string& title,
                 const std::string& content,
                 int dayNumber = 0,
                 std::vector<std::string> tags = {});

    /// Remove an entry by ID. No-op if not found.
    void removeEntry(int id);

    // -----------------------------------------------------------------------
    // Read
    // -----------------------------------------------------------------------

    /// Get a single entry by ID. Returns nullptr if not found.
    const JournalEntry* getEntry(int id) const;

    /// All entries in insertion order.
    const std::vector<JournalEntry>& getEntries() const { return m_entries; }

    /// Filter by type.
    std::vector<const JournalEntry*> getEntriesByType(JournalEntryType type) const;

    /// Filter by in-world day.
    std::vector<const JournalEntry*> getEntriesByDay(int dayNumber) const;

    /// Filter by tag (exact match).
    std::vector<const JournalEntry*> getEntriesByTag(const std::string& tag) const;

    /// Case-insensitive substring search across title and content.
    std::vector<const JournalEntry*> searchEntries(const std::string& query) const;

    int  entryCount() const { return static_cast<int>(m_entries.size()); }
    void clear();

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::vector<JournalEntry> m_entries;
    int                       m_nextId = 1;
};

} // namespace Phyxel::Core
