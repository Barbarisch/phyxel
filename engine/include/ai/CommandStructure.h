#pragma once

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel {
namespace AI {

/// Squads, officers and orders — the difference between an army and a mob.
///
/// Without this, 200 combatants are 200 independent agents that happen to
/// share a faction: every one of them charges the nearest enemy, and the
/// "battle" is a uniform grind. A chain of command lets a side act with
/// intent — this squad holds the ridge, that one flanks, the mauled one falls
/// back — and, more interestingly, lets that intent DEGRADE: kill the officer
/// and his squad reverts to individual instinct.
///
/// The engine owns the structure and the propagation. WHICH order an officer
/// picks is deliberately a policy callback, so a game can supply its own
/// doctrine (or an LLM, or a scripted commander) without touching the engine.
class CommandStructure {
public:
    enum class Order {
        Advance,    ///< close with the enemy
        Hold,       ///< stand and fight where you are (use cover)
        Flank,      ///< move wide, then engage from the side
        FallBack,   ///< withdraw toward the rally point
        Regroup,    ///< reform on the leader
    };

    static const char* orderName(Order o) {
        switch (o) {
            case Order::Advance:  return "advance";
            case Order::Hold:     return "hold";
            case Order::Flank:    return "flank";
            case Order::FallBack: return "fall_back";
            case Order::Regroup:  return "regroup";
        }
        return "?";
    }

    struct Squad {
        std::string id;
        std::string faction;
        std::string leaderId;             ///< "" once the officer is dead
        std::vector<std::string> members; ///< includes the leader
        Order order = Order::Advance;
        glm::vec3 objective{0.0f};        ///< where the order points
        glm::vec3 rally{0.0f};            ///< fall-back / regroup point
        float     lastDecision = 0.0f;    ///< seconds since the order changed
        bool      leaderless = false;     ///< officer down: orders stop updating
    };

    /// Snapshot handed to the doctrine callback so it can decide.
    struct SquadSituation {
        const Squad* squad = nullptr;
        int   alive = 0;
        int   strength = 0;          ///< starting member count
        float healthFraction = 1.0f; ///< squad-average hp fraction
        int   enemiesNear = 0;       ///< hostiles within engagement range
        float nearestEnemyDist = 1e9f;
        glm::vec3 centre{0.0f};
        glm::vec3 enemyCentre{0.0f};
    };

    /// Doctrine: given the situation, what does this officer order? Supplied by
    /// the host so the engine has no opinion about tactics.
    using Doctrine = std::function<Order(const SquadSituation&)>;
    void setDoctrine(Doctrine d) { m_doctrine = std::move(d); }

    /// How often an officer re-evaluates (seconds). Real officers do not
    /// re-plan every frame, and neither should 20 of them in a battle.
    void setDecisionInterval(float s) { m_decisionInterval = s; }

    // ── Structure ───────────────────────────────────────────────
    Squad& createSquad(const std::string& id, const std::string& faction) {
        Squad& s = m_squads[id];
        s.id = id;
        s.faction = faction;
        return s;
    }
    void addMember(const std::string& squadId, const std::string& entityId,
                   bool isLeader = false) {
        auto it = m_squads.find(squadId);
        if (it == m_squads.end()) return;
        it->second.members.push_back(entityId);
        if (isLeader) it->second.leaderId = entityId;
        m_entitySquad[entityId] = squadId;
    }

    Squad* squadOf(const std::string& entityId) {
        auto it = m_entitySquad.find(entityId);
        if (it == m_entitySquad.end()) return nullptr;
        auto sq = m_squads.find(it->second);
        return sq == m_squads.end() ? nullptr : &sq->second;
    }
    const Squad* squadOf(const std::string& entityId) const {
        auto it = m_entitySquad.find(entityId);
        if (it == m_entitySquad.end()) return nullptr;
        auto sq = m_squads.find(it->second);
        return sq == m_squads.end() ? nullptr : &sq->second;
    }
    Squad* squad(const std::string& id) {
        auto it = m_squads.find(id);
        return it == m_squads.end() ? nullptr : &it->second;
    }
    const std::unordered_map<std::string, Squad>& squads() const { return m_squads; }

    /// The order an entity is currently under (Advance when unsquadded).
    Order orderFor(const std::string& entityId) const {
        const Squad* s = squadOf(entityId);
        return s ? s->order : Order::Advance;
    }

    /// Report the officer's death: the squad keeps its last order but stops
    /// receiving new ones. A headless squad fights on, badly.
    void notifyDeath(const std::string& entityId) {
        Squad* s = squadOf(entityId);
        if (!s) return;
        if (s->leaderId == entityId) {
            s->leaderId.clear();
            s->leaderless = true;
        }
    }

    /// Tick every squad's officer on the decision cadence. `situationOf` is
    /// supplied by the host (it owns the entities); the doctrine turns that
    /// into an order.
    void update(float dt, const std::function<SquadSituation(const Squad&)>& situationOf,
                const std::function<void(const Squad&, Order)>& onOrderChanged = {}) {
        if (!m_doctrine) return;
        for (auto& [id, s] : m_squads) {
            s.lastDecision += dt;
            if (s.leaderless) continue;              // nobody left to give orders
            if (s.lastDecision < m_decisionInterval) continue;
            s.lastDecision = 0.0f;
            SquadSituation sit = situationOf(s);
            sit.squad = &s;
            if (sit.alive <= 0) continue;
            // Refresh the objective every tick, not only when the order flips:
            // a squad holding "Flank" still needs to know where the enemy has
            // moved to since the order was given.
            s.objective = sit.enemyCentre;
            const Order next = m_doctrine(sit);
            if (next != s.order) {
                s.order = next;
                if (onOrderChanged) onOrderChanged(s, next);
            }
        }
    }

private:
    std::unordered_map<std::string, Squad> m_squads;
    std::unordered_map<std::string, std::string> m_entitySquad;
    Doctrine m_doctrine;
    float m_decisionInterval = 3.0f;
};

} // namespace AI
} // namespace Phyxel
