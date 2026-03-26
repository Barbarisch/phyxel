#include "ai/ConversationMemory.h"
#include "ai/LLMClient.h"
#include "utils/Logger.h"

#include <sstream>
#include <algorithm>

#ifdef ENABLE_WORLD_STORAGE
#include <sqlite3.h>
#endif

namespace Phyxel {
namespace AI {

// ============================================================================
// Lifecycle
// ============================================================================

ConversationMemory::~ConversationMemory() {
    finalizeStatements();
    // Note: we don't close m_db — it's owned by WorldStorage
}

bool ConversationMemory::initialize(sqlite3* db) {
    if (!db) {
        LOG_ERROR("AI", "ConversationMemory::initialize called with null db");
        return false;
    }
    m_db = db;

#ifdef ENABLE_WORLD_STORAGE
    if (!createTables()) return false;
    if (!prepareStatements()) return false;
    LOG_INFO("AI", "ConversationMemory initialized");
    return true;
#else
    LOG_WARN("AI", "ConversationMemory: SQLite not available");
    return false;
#endif
}

// ============================================================================
// Table Creation
// ============================================================================

bool ConversationMemory::createTables() {
#ifdef ENABLE_WORLD_STORAGE
    const char* turnTableSQL =
        "CREATE TABLE IF NOT EXISTS conversation_turns ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  npc_id TEXT NOT NULL,"
        "  speaker TEXT NOT NULL,"
        "  text TEXT NOT NULL,"
        "  emotion TEXT DEFAULT '',"
        "  importance REAL DEFAULT 1.0,"
        "  game_time REAL NOT NULL,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_turns_npc ON conversation_turns(npc_id, game_time DESC);";

    const char* summaryTableSQL =
        "CREATE TABLE IF NOT EXISTS conversation_summaries ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  npc_id TEXT NOT NULL,"
        "  summary TEXT NOT NULL,"
        "  turn_range_start INTEGER,"
        "  turn_range_end INTEGER,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_summaries_npc ON conversation_summaries(npc_id);";

    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, turnTableSQL, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOG_ERROR("AI", "Failed to create conversation_turns table: {}", errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
        return false;
    }
    if (sqlite3_exec(m_db, summaryTableSQL, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOG_ERROR("AI", "Failed to create conversation_summaries table: {}", errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
        return false;
    }
    return true;
#else
    return false;
#endif
}

// ============================================================================
// Prepared Statements
// ============================================================================

bool ConversationMemory::prepareStatements() {
#ifdef ENABLE_WORLD_STORAGE
    const char* insertTurnSQL =
        "INSERT INTO conversation_turns (npc_id, speaker, text, emotion, importance, game_time) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    const char* selectTurnsSQL =
        "SELECT speaker, text, emotion, importance, game_time "
        "FROM conversation_turns WHERE npc_id = ? "
        "ORDER BY game_time DESC LIMIT ?";

    const char* countTurnsSQL =
        "SELECT COUNT(*) FROM conversation_turns WHERE npc_id = ?";

    const char* selectSummarySQL =
        "SELECT summary FROM conversation_summaries "
        "WHERE npc_id = ? ORDER BY id DESC LIMIT 1";

    const char* insertSummarySQL =
        "INSERT INTO conversation_summaries (npc_id, summary, turn_range_start, turn_range_end) "
        "VALUES (?, ?, ?, ?)";

    bool ok = true;
    ok &= (sqlite3_prepare_v2(m_db, insertTurnSQL, -1, &m_insertTurnStmt, nullptr) == SQLITE_OK);
    ok &= (sqlite3_prepare_v2(m_db, selectTurnsSQL, -1, &m_selectTurnsStmt, nullptr) == SQLITE_OK);
    ok &= (sqlite3_prepare_v2(m_db, countTurnsSQL, -1, &m_countTurnsStmt, nullptr) == SQLITE_OK);
    ok &= (sqlite3_prepare_v2(m_db, selectSummarySQL, -1, &m_selectSummaryStmt, nullptr) == SQLITE_OK);
    ok &= (sqlite3_prepare_v2(m_db, insertSummarySQL, -1, &m_insertSummaryStmt, nullptr) == SQLITE_OK);

    if (!ok) {
        LOG_ERROR("AI", "ConversationMemory: failed to prepare statements: {}",
                  sqlite3_errmsg(m_db));
    }
    return ok;
#else
    return false;
#endif
}

void ConversationMemory::finalizeStatements() {
#ifdef ENABLE_WORLD_STORAGE
    if (m_insertTurnStmt)   { sqlite3_finalize(m_insertTurnStmt);   m_insertTurnStmt = nullptr; }
    if (m_selectTurnsStmt)  { sqlite3_finalize(m_selectTurnsStmt);  m_selectTurnsStmt = nullptr; }
    if (m_countTurnsStmt)   { sqlite3_finalize(m_countTurnsStmt);   m_countTurnsStmt = nullptr; }
    if (m_selectSummaryStmt){ sqlite3_finalize(m_selectSummaryStmt); m_selectSummaryStmt = nullptr; }
    if (m_insertSummaryStmt){ sqlite3_finalize(m_insertSummaryStmt); m_insertSummaryStmt = nullptr; }
#endif
}

// ============================================================================
// Recording
// ============================================================================

void ConversationMemory::recordTurn(const std::string& npcId,
                                     const std::string& speaker,
                                     const std::string& text,
                                     const std::string& emotion,
                                     float importance,
                                     float gameTime) {
#ifdef ENABLE_WORLD_STORAGE
    if (!m_insertTurnStmt) return;

    sqlite3_reset(m_insertTurnStmt);
    sqlite3_bind_text(m_insertTurnStmt, 1, npcId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_insertTurnStmt, 2, speaker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_insertTurnStmt, 3, text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_insertTurnStmt, 4, emotion.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(m_insertTurnStmt, 5, importance);
    sqlite3_bind_double(m_insertTurnStmt, 6, gameTime);

    if (sqlite3_step(m_insertTurnStmt) != SQLITE_DONE) {
        LOG_ERROR("AI", "Failed to record conversation turn: {}", sqlite3_errmsg(m_db));
    }
#endif
}

// ============================================================================
// Retrieval
// ============================================================================

std::vector<ConversationMemory::Turn> ConversationMemory::getRecentTurns(
    const std::string& npcId, int limit) const
{
    std::vector<Turn> turns;
#ifdef ENABLE_WORLD_STORAGE
    if (!m_selectTurnsStmt) return turns;

    sqlite3_reset(m_selectTurnsStmt);
    sqlite3_bind_text(m_selectTurnsStmt, 1, npcId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(m_selectTurnsStmt, 2, limit);

    while (sqlite3_step(m_selectTurnsStmt) == SQLITE_ROW) {
        Turn t;
        t.speaker    = reinterpret_cast<const char*>(sqlite3_column_text(m_selectTurnsStmt, 0));
        t.text       = reinterpret_cast<const char*>(sqlite3_column_text(m_selectTurnsStmt, 1));
        const char* em = reinterpret_cast<const char*>(sqlite3_column_text(m_selectTurnsStmt, 2));
        t.emotion    = em ? em : "";
        t.importance = static_cast<float>(sqlite3_column_double(m_selectTurnsStmt, 3));
        t.gameTime   = static_cast<float>(sqlite3_column_double(m_selectTurnsStmt, 4));
        turns.push_back(std::move(t));
    }

    // Reverse so oldest is first (query returns newest first)
    std::reverse(turns.begin(), turns.end());
#endif
    return turns;
}

int ConversationMemory::getTotalTurns(const std::string& npcId) const {
#ifdef ENABLE_WORLD_STORAGE
    if (!m_countTurnsStmt) return 0;

    sqlite3_reset(m_countTurnsStmt);
    sqlite3_bind_text(m_countTurnsStmt, 1, npcId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(m_countTurnsStmt) == SQLITE_ROW) {
        return sqlite3_column_int(m_countTurnsStmt, 0);
    }
#endif
    return 0;
}

// ============================================================================
// Summarization
// ============================================================================

std::string ConversationMemory::getSummary(const std::string& npcId) const {
#ifdef ENABLE_WORLD_STORAGE
    if (!m_selectSummaryStmt) return "";

    sqlite3_reset(m_selectSummaryStmt);
    sqlite3_bind_text(m_selectSummaryStmt, 1, npcId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(m_selectSummaryStmt) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(m_selectSummaryStmt, 0));
        return text ? text : "";
    }
#endif
    return "";
}

void ConversationMemory::generateSummary(const std::string& npcId,
                                          LLMClient* llm,
                                          int keepRecent) {
#ifdef ENABLE_WORLD_STORAGE
    if (!llm || !llm->isConfigured() || !m_db) return;

    // Get all turns for this NPC
    auto allTurns = getRecentTurns(npcId, 100); // Get a large batch
    int totalTurns = static_cast<int>(allTurns.size());

    if (totalTurns <= keepRecent) return; // Not enough to summarize

    // Build the conversation text to summarize (everything except the most recent)
    int summarizeCount = totalTurns - keepRecent;
    std::ostringstream convo;
    for (int i = 0; i < summarizeCount; i++) {
        convo << allTurns[i].speaker << ": " << allTurns[i].text;
        // Include emotion/importance metadata when present
        bool hasMeta = false;
        if (!allTurns[i].emotion.empty()) {
            convo << " [emotion: " << allTurns[i].emotion << "]";
            hasMeta = true;
        }
        if (allTurns[i].importance > 1.5f) {
            convo << " [important]";
        }
        convo << "\n";
    }

    // Get existing summary to include in the prompt
    std::string existingSummary = getSummary(npcId);

    // Ask LLM to summarize with structured guidance
    std::vector<LLMMessage> messages;
    std::string sysPrompt =
        "Summarize this conversation history concisely in 2-4 sentences. Include:\n"
        "- Key topics discussed and decisions made\n"
        "- Any promises, agreements, or commitments\n"
        "- The overall relationship tone and emotional arc\n"
        "- Important facts the NPC learned about the player\n"
        "Write from the NPC's perspective as a memory of past interactions.";
    if (!existingSummary.empty()) {
        sysPrompt += "\n\nPrevious summary to incorporate and update:\n" + existingSummary;
    }
    messages.push_back({"system", sysPrompt});
    messages.push_back({"user", convo.str()});

    auto response = llm->complete(messages);
    if (!response.ok()) {
        LOG_WARN("AI", "Failed to generate conversation summary: {}", response.error);
        return;
    }

    // Store the summary
    if (m_insertSummaryStmt) {
        // Get turn ID range (approximate using count)
        int rangeStart = 1;
        int rangeEnd = summarizeCount;

        sqlite3_reset(m_insertSummaryStmt);
        sqlite3_bind_text(m_insertSummaryStmt, 1, npcId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(m_insertSummaryStmt, 2, response.content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(m_insertSummaryStmt, 3, rangeStart);
        sqlite3_bind_int(m_insertSummaryStmt, 4, rangeEnd);

        if (sqlite3_step(m_insertSummaryStmt) != SQLITE_DONE) {
            LOG_ERROR("AI", "Failed to store conversation summary: {}", sqlite3_errmsg(m_db));
        }
    }

    // Prune the old turns that were summarized
    std::string pruneSQL =
        "DELETE FROM conversation_turns WHERE npc_id = ? AND id NOT IN "
        "(SELECT id FROM conversation_turns WHERE npc_id = ? ORDER BY game_time DESC LIMIT ?)";

    sqlite3_stmt* pruneStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, pruneSQL.c_str(), -1, &pruneStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(pruneStmt, 1, npcId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(pruneStmt, 2, npcId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pruneStmt, 3, keepRecent);
        sqlite3_step(pruneStmt);
        sqlite3_finalize(pruneStmt);
    }

    LOG_INFO("AI", "Generated conversation summary for '{}', pruned {} old turns",
             npcId, summarizeCount);
#endif
}

bool ConversationMemory::shouldSummarize(const std::string& npcId) const {
    int total = getTotalTurns(npcId);
    return total > m_summarizationThreshold;
}

bool ConversationMemory::autoSummarizeIfNeeded(const std::string& npcId, LLMClient* llm) {
    if (!llm || !llm->isConfigured()) return false;
    if (!shouldSummarize(npcId)) return false;

    int total = getTotalTurns(npcId);
    LOG_INFO("AI", "Auto-summarizing conversation with '{}' ({} turns, threshold {})",
             npcId, total, m_summarizationThreshold);

    generateSummary(npcId, llm, m_keepRecentTurns);
    return true;
}

// ============================================================================
// Maintenance
// ============================================================================

void ConversationMemory::pruneOldTurns(int keepRecent) {
#ifdef ENABLE_WORLD_STORAGE
    if (!m_db) return;

    // Get all distinct NPC IDs
    const char* npcSQL = "SELECT DISTINCT npc_id FROM conversation_turns";
    sqlite3_stmt* npcStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, npcSQL, -1, &npcStmt, nullptr) != SQLITE_OK) return;

    std::vector<std::string> npcIds;
    while (sqlite3_step(npcStmt) == SQLITE_ROW) {
        const char* id = reinterpret_cast<const char*>(sqlite3_column_text(npcStmt, 0));
        if (id) npcIds.push_back(id);
    }
    sqlite3_finalize(npcStmt);

    // Prune each NPC
    const char* pruneSQL =
        "DELETE FROM conversation_turns WHERE npc_id = ? AND id NOT IN "
        "(SELECT id FROM conversation_turns WHERE npc_id = ? ORDER BY game_time DESC LIMIT ?)";

    sqlite3_stmt* pruneStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, pruneSQL, -1, &pruneStmt, nullptr) != SQLITE_OK) return;

    for (const auto& npcId : npcIds) {
        sqlite3_reset(pruneStmt);
        sqlite3_bind_text(pruneStmt, 1, npcId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(pruneStmt, 2, npcId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(pruneStmt, 3, keepRecent);
        sqlite3_step(pruneStmt);
    }
    sqlite3_finalize(pruneStmt);
#endif
}

} // namespace AI
} // namespace Phyxel
