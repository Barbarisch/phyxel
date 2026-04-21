#include "core/CampaignJournal.h"
#include <algorithm>
#include <cctype>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

const char* journalEntryTypeName(JournalEntryType type) {
    switch (type) {
        case JournalEntryType::SessionNote:    return "SessionNote";
        case JournalEntryType::WorldEvent:     return "WorldEvent";
        case JournalEntryType::CharacterEvent: return "CharacterEvent";
        case JournalEntryType::QuestUpdate:    return "QuestUpdate";
        case JournalEntryType::Discovery:      return "Discovery";
    }
    return "WorldEvent";
}

JournalEntryType journalEntryTypeFromString(const char* name) {
    if (!name) return JournalEntryType::WorldEvent;
    auto eq = [&](const char* s) { return _stricmp(name, s) == 0; };
    if (eq("SessionNote"))    return JournalEntryType::SessionNote;
    if (eq("WorldEvent"))     return JournalEntryType::WorldEvent;
    if (eq("CharacterEvent")) return JournalEntryType::CharacterEvent;
    if (eq("QuestUpdate"))    return JournalEntryType::QuestUpdate;
    if (eq("Discovery"))      return JournalEntryType::Discovery;
    return JournalEntryType::WorldEvent;
}

// ---------------------------------------------------------------------------
// JournalEntry JSON
// ---------------------------------------------------------------------------

nlohmann::json JournalEntry::toJson() const {
    nlohmann::json j;
    j["id"]        = id;
    j["type"]      = journalEntryTypeName(type);
    j["dayNumber"] = dayNumber;
    j["title"]     = title;
    j["content"]   = content;
    j["tags"]      = tags;
    return j;
}

JournalEntry JournalEntry::fromJson(const nlohmann::json& j) {
    JournalEntry e;
    e.id        = j.value("id",        0);
    e.type      = journalEntryTypeFromString(j.value("type", "WorldEvent").c_str());
    e.dayNumber = j.value("dayNumber", 0);
    e.title     = j.value("title",     "");
    e.content   = j.value("content",   "");
    if (j.contains("tags") && j["tags"].is_array()) {
        for (const auto& t : j["tags"])
            e.tags.push_back(t.get<std::string>());
    }
    return e;
}

// ---------------------------------------------------------------------------
// CampaignJournal
// ---------------------------------------------------------------------------

int CampaignJournal::addEntry(JournalEntryType type,
                               const std::string& title,
                               const std::string& content,
                               int dayNumber,
                               std::vector<std::string> tags) {
    JournalEntry entry;
    entry.id        = m_nextId++;
    entry.type      = type;
    entry.dayNumber = dayNumber;
    entry.title     = title;
    entry.content   = content;
    entry.tags      = std::move(tags);
    m_entries.push_back(std::move(entry));
    return m_entries.back().id;
}

void CampaignJournal::removeEntry(int id) {
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [id](const JournalEntry& e) { return e.id == id; });
    if (it != m_entries.end()) m_entries.erase(it);
}

const JournalEntry* CampaignJournal::getEntry(int id) const {
    for (const auto& e : m_entries)
        if (e.id == id) return &e;
    return nullptr;
}

std::vector<const JournalEntry*> CampaignJournal::getEntriesByType(
    JournalEntryType type) const
{
    std::vector<const JournalEntry*> out;
    for (const auto& e : m_entries)
        if (e.type == type) out.push_back(&e);
    return out;
}

std::vector<const JournalEntry*> CampaignJournal::getEntriesByDay(
    int dayNumber) const
{
    std::vector<const JournalEntry*> out;
    for (const auto& e : m_entries)
        if (e.dayNumber == dayNumber) out.push_back(&e);
    return out;
}

std::vector<const JournalEntry*> CampaignJournal::getEntriesByTag(
    const std::string& tag) const
{
    std::vector<const JournalEntry*> out;
    for (const auto& e : m_entries) {
        for (const auto& t : e.tags) {
            if (t == tag) { out.push_back(&e); break; }
        }
    }
    return out;
}

std::vector<const JournalEntry*> CampaignJournal::searchEntries(
    const std::string& query) const
{
    // Case-insensitive substring search in title + content
    auto toLower = [](const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return r;
    };

    std::string q = toLower(query);
    std::vector<const JournalEntry*> out;
    for (const auto& e : m_entries) {
        if (toLower(e.title).find(q) != std::string::npos ||
            toLower(e.content).find(q) != std::string::npos) {
            out.push_back(&e);
        }
    }
    return out;
}

void CampaignJournal::clear() {
    m_entries.clear();
    m_nextId = 1;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json CampaignJournal::toJson() const {
    nlohmann::json j;
    j["nextId"] = m_nextId;
    auto arr = nlohmann::json::array();
    for (const auto& e : m_entries) arr.push_back(e.toJson());
    j["entries"] = arr;
    return j;
}

void CampaignJournal::fromJson(const nlohmann::json& j) {
    m_entries.clear();
    m_nextId = j.value("nextId", 1);
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& ej : j["entries"])
            m_entries.push_back(JournalEntry::fromJson(ej));
    }
}

} // namespace Phyxel::Core
