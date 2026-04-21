#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// AttunementSystem
//
// D&D 5e: a creature can be attuned to at most 3 magic items at a time.
// Attuning takes a short rest. Some items grant bonuses only while attuned.
// ============================================================================
class AttunementSystem {
public:
    static constexpr int MAX_ATTUNED = 3;

    // -----------------------------------------------------------------------
    // Attune / unattune
    // -----------------------------------------------------------------------

    /// Attempt to attune entityId to itemId.
    /// Returns false if already attuned to MAX_ATTUNED items or already attuned to this item.
    bool attune(const std::string& entityId, const std::string& itemId);

    /// Remove attunement. Returns false if not attuned.
    bool unattune(const std::string& entityId, const std::string& itemId);

    /// Remove all attunements for an entity (death, etc.).
    void unattuneAll(const std::string& entityId);

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    bool isAttuned(const std::string& entityId, const std::string& itemId) const;
    int  attunedCount(const std::string& entityId) const;
    bool canAttune(const std::string& entityId) const;

    /// All items currently attuned by entityId (empty vector if none).
    const std::vector<std::string>& attunedItems(const std::string& entityId) const;

    /// All entities attuned to a specific item (normally just one).
    std::vector<std::string> entitiesAttunedTo(const std::string& itemId) const;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Remove entity and all their attunements (entity died or was removed).
    void removeEntity(const std::string& entityId);

    void clear();

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    // entityId → ordered list of attuned item IDs
    std::unordered_map<std::string, std::vector<std::string>> m_attuned;

    static const std::vector<std::string> s_empty;
};

} // namespace Core
} // namespace Phyxel
