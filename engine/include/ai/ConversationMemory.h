#pragma once

#include <string>
#include <vector>

// Forward declare SQLite types (same pattern as WorldStorage.h)
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

namespace Phyxel {
namespace AI {

class LLMClient;

// ============================================================================
// ConversationMemory — SQLite-backed conversation log for NPC interactions
//
// Shares the same database as WorldStorage (worlds/default.db).
// Records turns, retrieves history, and supports LLM-generated summaries.
// ============================================================================

class ConversationMemory {
public:
    ConversationMemory() = default;
    ~ConversationMemory();

    // Non-copyable
    ConversationMemory(const ConversationMemory&) = delete;
    ConversationMemory& operator=(const ConversationMemory&) = delete;

    /// Initialize tables using an existing DB connection (from WorldStorage)
    bool initialize(sqlite3* db);

    /// Check if initialized
    bool isInitialized() const { return m_db != nullptr; }

    // ---- Recording ----

    /// Record a conversation turn
    void recordTurn(const std::string& npcId,
                    const std::string& speaker,
                    const std::string& text,
                    const std::string& emotion = "",
                    float importance = 1.0f,
                    float gameTime = 0.0f);

    // ---- Retrieval ----

    struct Turn {
        std::string speaker;
        std::string text;
        std::string emotion;
        float importance = 1.0f;
        float gameTime   = 0.0f;
    };

    /// Get most recent turns for an NPC (newest last)
    std::vector<Turn> getRecentTurns(const std::string& npcId, int limit = 20) const;

    /// Total turn count for an NPC
    int getTotalTurns(const std::string& npcId) const;

    // ---- Summarization ----

    /// Get the latest AI-generated summary for an NPC
    std::string getSummary(const std::string& npcId) const;

    /// Generate a summary of old turns using the LLM, store it, prune old turns
    void generateSummary(const std::string& npcId, LLMClient* llm, int keepRecent = 10);

    // ---- Maintenance ----

    /// Prune old turns per NPC, keeping only the most recent N
    void pruneOldTurns(int keepRecent = 50);

private:
    bool createTables();
    bool prepareStatements();
    void finalizeStatements();

    sqlite3* m_db = nullptr;

    // Prepared statements for performance
    sqlite3_stmt* m_insertTurnStmt   = nullptr;
    sqlite3_stmt* m_selectTurnsStmt  = nullptr;
    sqlite3_stmt* m_countTurnsStmt   = nullptr;
    sqlite3_stmt* m_selectSummaryStmt = nullptr;
    sqlite3_stmt* m_insertSummaryStmt = nullptr;
};

} // namespace AI
} // namespace Phyxel
