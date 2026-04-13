#include "core/AttunementSystem.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

const std::vector<std::string> AttunementSystem::s_empty;

// ---------------------------------------------------------------------------
// Attune / unattune
// ---------------------------------------------------------------------------

bool AttunementSystem::attune(const std::string& entityId, const std::string& itemId) {
    if (entityId.empty() || itemId.empty()) return false;
    auto& list = m_attuned[entityId];
    // Already attuned to this item?
    if (std::find(list.begin(), list.end(), itemId) != list.end()) return false;
    // At max attunement?
    if (static_cast<int>(list.size()) >= MAX_ATTUNED) return false;
    list.push_back(itemId);
    return true;
}

bool AttunementSystem::unattune(const std::string& entityId, const std::string& itemId) {
    auto it = m_attuned.find(entityId);
    if (it == m_attuned.end()) return false;
    auto& list = it->second;
    auto pos = std::find(list.begin(), list.end(), itemId);
    if (pos == list.end()) return false;
    list.erase(pos);
    if (list.empty()) m_attuned.erase(it);
    return true;
}

void AttunementSystem::unattuneAll(const std::string& entityId) {
    m_attuned.erase(entityId);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool AttunementSystem::isAttuned(const std::string& entityId,
                                  const std::string& itemId) const {
    auto it = m_attuned.find(entityId);
    if (it == m_attuned.end()) return false;
    return std::find(it->second.begin(), it->second.end(), itemId) != it->second.end();
}

int AttunementSystem::attunedCount(const std::string& entityId) const {
    auto it = m_attuned.find(entityId);
    return (it != m_attuned.end()) ? static_cast<int>(it->second.size()) : 0;
}

bool AttunementSystem::canAttune(const std::string& entityId) const {
    return attunedCount(entityId) < MAX_ATTUNED;
}

const std::vector<std::string>& AttunementSystem::attunedItems(
    const std::string& entityId) const
{
    auto it = m_attuned.find(entityId);
    return (it != m_attuned.end()) ? it->second : s_empty;
}

std::vector<std::string> AttunementSystem::entitiesAttunedTo(
    const std::string& itemId) const
{
    std::vector<std::string> result;
    for (const auto& [entity, items] : m_attuned) {
        if (std::find(items.begin(), items.end(), itemId) != items.end())
            result.push_back(entity);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void AttunementSystem::removeEntity(const std::string& entityId) {
    m_attuned.erase(entityId);
}

void AttunementSystem::clear() {
    m_attuned.clear();
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json AttunementSystem::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [entityId, items] : m_attuned) {
        j[entityId] = items;
    }
    return j;
}

void AttunementSystem::fromJson(const nlohmann::json& j) {
    clear();
    if (!j.is_object()) return;
    for (const auto& [entityId, items] : j.items()) {
        if (items.is_array()) {
            auto& list = m_attuned[entityId];
            for (const auto& item : items) {
                if (item.is_string() && list.size() < static_cast<size_t>(MAX_ATTUNED))
                    list.push_back(item.get<std::string>());
            }
        }
    }
}

} // namespace Core
} // namespace Phyxel
