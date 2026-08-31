#pragma once

#include <nlohmann/json.hpp>

#include <deque>
#include <fstream>
#include <mutex>
#include <string>

namespace Phyxel {
namespace Core {

/// A structured, human-readable record of WHY combat unfolded the way it did —
/// deliberately separate from the engine log.
///
/// The engine log answers "what happened" ("NPC 'npc_Archer' hits 'player'").
/// This answers "why did it do that": which targets it weighed, which tactic
/// fired, what it rejected and on what grounds. Debugging an AI from
/// after-the-fact damage lines is guesswork; this is the reasoning itself.
///
/// Entries carry a machine-readable `detail` blob AND a one-line `reason`
/// written for a human reading a wall of turns. Cheap enough to leave on: a
/// bounded ring in memory, plus an optional JSONL file.
class CombatLog {
public:
    struct Entry {
        double      time    = 0.0;   ///< seconds since the log was created
        int         round   = 0;
        std::string actor;           ///< entity id whose decision this is
        std::string category;        ///< "target" | "action" | "reject" | "outcome" | "turn"
        std::string decision;        ///< short verb: "cast", "kite", "flee", "heal", ...
        std::string reason;          ///< the WHY, in a sentence
        nlohmann::json detail;       ///< structured supporting numbers

        nlohmann::json toJson() const {
            return {{"t", time}, {"round", round}, {"actor", actor},
                    {"category", category}, {"decision", decision},
                    {"reason", reason}, {"detail", detail}};
        }
    };

    static CombatLog& instance() {
        static CombatLog s;
        return s;
    }

    void setEnabled(bool on) { m_enabled = on; }
    bool enabled() const     { return m_enabled; }

    /// Mirror entries to a JSONL file (one JSON object per line). Empty path
    /// disables file output. Kept separate from the engine log on purpose.
    void setFile(const std::string& path) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_file.close();
        if (!path.empty()) m_file.open(path, std::ios::out | std::ios::trunc);
    }

    void setRound(int round) { m_round = round; }
    int  round() const       { return m_round; }

    void add(const std::string& actor, const std::string& category,
             const std::string& decision, const std::string& reason,
             nlohmann::json detail = nlohmann::json::object()) {
        if (!m_enabled) return;
        Entry e;
        e.time     = m_clock;
        e.round    = m_round;
        e.actor    = actor;
        e.category = category;
        e.decision = decision;
        e.reason   = reason;
        e.detail   = std::move(detail);

        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_file.is_open()) { m_file << e.toJson().dump() << "\n"; m_file.flush(); }
        m_entries.push_back(std::move(e));
        while (m_entries.size() > kMaxEntries) m_entries.pop_front();
        ++m_seq;
    }

    /// Advance the log's clock (call once per frame from the host).
    void tick(float dt) { m_clock += dt; }

    /// Entries newest-last. `since` skips the first N (use the previous
    /// `next_index` for polling); 0 = from the start of the ring.
    nlohmann::json toJson(size_t since = 0, size_t limit = 200) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        const size_t dropped = (m_seq > m_entries.size()) ? m_seq - m_entries.size() : 0;
        size_t start = (since > dropped) ? since - dropped : 0;
        nlohmann::json arr = nlohmann::json::array();
        for (size_t i = start; i < m_entries.size() && arr.size() < limit; ++i)
            arr.push_back(m_entries[i].toJson());
        return {{"entries", arr},
                {"next_index", dropped + m_entries.size()},
                {"dropped", dropped},
                {"round", m_round}};
    }

    void clear() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_entries.clear();
        m_seq = 0;
    }

private:
    CombatLog() = default;
    static constexpr size_t kMaxEntries = 2000;

    mutable std::mutex  m_mutex;
    std::deque<Entry>   m_entries;
    std::ofstream       m_file;
    size_t              m_seq     = 0;   ///< total entries ever added
    double              m_clock   = 0.0;
    int                 m_round   = 0;
    bool                m_enabled = true;
};

} // namespace Core
} // namespace Phyxel
