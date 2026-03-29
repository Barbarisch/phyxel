#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace Phyxel {
namespace Core {

struct Objective {
    std::string id;
    std::string title;
    std::string description;
    enum class Status { Active, Completed, Failed } status = Status::Active;
    bool hidden = false;         // Hidden objectives don't show in HUD
    int priority = 0;            // Higher = more important (sort order)
    std::string category;        // e.g. "main", "side", "exploration"

    nlohmann::json toJson() const {
        return {
            {"id", id}, {"title", title}, {"description", description},
            {"status", status == Status::Active ? "active" : (status == Status::Completed ? "completed" : "failed")},
            {"hidden", hidden}, {"priority", priority}, {"category", category}
        };
    }
};

class ObjectiveTracker {
public:
    bool addObjective(const std::string& id, const std::string& title,
                      const std::string& description = "", const std::string& category = "main",
                      int priority = 0, bool hidden = false) {
        if (m_objectives.count(id)) return false;
        Objective obj;
        obj.id = id;
        obj.title = title;
        obj.description = description;
        obj.category = category;
        obj.priority = priority;
        obj.hidden = hidden;
        m_objectives[id] = obj;
        m_order.push_back(id);
        return true;
    }

    bool completeObjective(const std::string& id) {
        auto it = m_objectives.find(id);
        if (it == m_objectives.end()) return false;
        it->second.status = Objective::Status::Completed;
        m_completedCount++;
        return true;
    }

    bool failObjective(const std::string& id) {
        auto it = m_objectives.find(id);
        if (it == m_objectives.end()) return false;
        it->second.status = Objective::Status::Failed;
        return true;
    }

    bool removeObjective(const std::string& id) {
        auto it = m_objectives.find(id);
        if (it == m_objectives.end()) return false;
        if (it->second.status == Objective::Status::Completed) m_completedCount--;
        m_objectives.erase(it);
        m_order.erase(std::remove(m_order.begin(), m_order.end(), id), m_order.end());
        return true;
    }

    const Objective* getObjective(const std::string& id) const {
        auto it = m_objectives.find(id);
        return (it != m_objectives.end()) ? &it->second : nullptr;
    }

    std::vector<const Objective*> getActiveObjectives() const {
        std::vector<const Objective*> result;
        for (auto& id : m_order) {
            auto& obj = m_objectives.at(id);
            if (obj.status == Objective::Status::Active && !obj.hidden) {
                result.push_back(&obj);
            }
        }
        std::sort(result.begin(), result.end(), [](const Objective* a, const Objective* b) {
            return a->priority > b->priority;
        });
        return result;
    }

    std::vector<const Objective*> getAllObjectives() const {
        std::vector<const Objective*> result;
        for (auto& id : m_order) {
            result.push_back(&m_objectives.at(id));
        }
        return result;
    }

    int getCompletedCount() const { return m_completedCount; }
    int getTotalCount() const { return static_cast<int>(m_objectives.size()); }

    void clear() {
        m_objectives.clear();
        m_order.clear();
        m_completedCount = 0;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["totalCount"] = getTotalCount();
        j["completedCount"] = m_completedCount;
        nlohmann::json arr = nlohmann::json::array();
        for (auto& id : m_order) {
            arr.push_back(m_objectives.at(id).toJson());
        }
        j["objectives"] = arr;
        return j;
    }

    void fromJson(const nlohmann::json& j) {
        clear();
        if (!j.contains("objectives")) return;
        for (auto& obj : j["objectives"]) {
            std::string id = obj.value("id", "");
            if (id.empty()) continue;
            addObjective(id, obj.value("title", ""), obj.value("description", ""),
                         obj.value("category", "main"), obj.value("priority", 0),
                         obj.value("hidden", false));
            std::string status = obj.value("status", "active");
            if (status == "completed") completeObjective(id);
            else if (status == "failed") failObjective(id);
        }
    }

private:
    std::unordered_map<std::string, Objective> m_objectives;
    std::vector<std::string> m_order;  // Insertion order
    int m_completedCount = 0;
};

} // namespace Core
} // namespace Phyxel
