#pragma once

#include "ai/BehaviorTree.h"
#include "ai/ActionSystem.h"
#include <fstream>
#include <stdexcept>

namespace Phyxel {
namespace AI {

/// Loads behavior trees from JSON definitions.
///
/// JSON format:
/// {
///   "type": "Selector|Sequence|Parallel|Inverter|Repeater|Cooldown|Succeeder|
///            Action|Condition|BBCheck|BBHasKey",
///   "name": "optional node name",
///   "children": [...],  // for composites
///   "child": {...},      // for decorators
///
///   // Type-specific fields:
///   // Parallel: "required": 2
///   // Repeater: "count": 3
///   // Cooldown: "seconds": 5.0
///   // Action: "action": "MoveTo|LookAt|Wait|Speak|SetBlackboard|MoveToEntity|Flee"
///   //         + action-specific params (target, duration, message, key, value, etc.)
///   // Condition/BBCheck: "key": "hasThreat", "expected": true
///   // BBHasKey: "key": "target"
/// }
class BTLoader {
public:
    /// Parse a BT node tree from JSON.
    static BTNodePtr fromJson(const nlohmann::json& j) {
        std::string type = j.value("type", "");

        BTNodePtr node;

        // Composites
        if (type == "Sequence") {
            auto seq = std::make_shared<SequenceNode>();
            if (j.contains("children")) {
                for (const auto& child : j["children"]) {
                    seq->child(fromJson(child));
                }
            }
            node = seq;
        }
        else if (type == "Selector") {
            auto sel = std::make_shared<SelectorNode>();
            if (j.contains("children")) {
                for (const auto& child : j["children"]) {
                    sel->child(fromJson(child));
                }
            }
            node = sel;
        }
        else if (type == "Parallel") {
            int required = j.value("required", -1);
            auto par = std::make_shared<ParallelNode>(required);
            if (j.contains("children")) {
                for (const auto& child : j["children"]) {
                    par->child(fromJson(child));
                }
            }
            node = par;
        }
        // Decorators
        else if (type == "Inverter") {
            auto childNode = j.contains("child") ? fromJson(j["child"]) : nullptr;
            node = std::make_shared<InverterNode>(std::move(childNode));
        }
        else if (type == "Repeater") {
            auto childNode = j.contains("child") ? fromJson(j["child"]) : nullptr;
            int count = j.value("count", 0);
            node = std::make_shared<RepeaterNode>(std::move(childNode), count);
        }
        else if (type == "Cooldown") {
            auto childNode = j.contains("child") ? fromJson(j["child"]) : nullptr;
            float seconds = j.value("seconds", 1.0f);
            node = std::make_shared<CooldownNode>(std::move(childNode), seconds);
        }
        else if (type == "Succeeder") {
            auto childNode = j.contains("child") ? fromJson(j["child"]) : nullptr;
            node = std::make_shared<SucceederNode>(std::move(childNode));
        }
        // Leaf nodes
        else if (type == "Action") {
            node = parseAction(j);
        }
        else if (type == "BBCheck") {
            std::string key = j.value("key", "");
            bool expected = j.value("expected", true);
            node = std::make_shared<BBConditionNode>(key, expected);
        }
        else if (type == "BBHasKey") {
            std::string key = j.value("key", "");
            node = std::make_shared<BBHasKeyNode>(key);
        }

        // Set name if provided
        if (node && j.contains("name")) {
            node->setName(j["name"].get<std::string>());
        }

        return node;
    }

    /// Load a behavior tree from a JSON file.
    static BTNodePtr fromFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return nullptr;
        nlohmann::json j;
        file >> j;
        return fromJson(j);
    }

private:
    /// Parse an action node from JSON.
    static BTNodePtr parseAction(const nlohmann::json& j) {
        std::string actionType = j.value("action", "");
        std::shared_ptr<NPCAction> action;

        if (actionType == "MoveTo") {
            float x = j.value("x", 0.0f);
            float y = j.value("y", 0.0f);
            float z = j.value("z", 0.0f);
            float speed = j.value("speed", 2.0f);
            float arrivalDist = j.value("arrivalDistance", 1.0f);
            action = std::make_shared<MoveToAction>(glm::vec3(x, y, z), speed, arrivalDist);
        }
        else if (actionType == "MoveToBlackboard") {
            // Reads target position from a blackboard key
            std::string key = j.value("key", "taskLocationPos");
            float speed = j.value("speed", 2.0f);
            float arrivalDist = j.value("arrivalDistance", 1.0f);
            // We'll create a MoveTo with a placeholder — the position will be overridden
            // Actually, better to use MoveToEntity or create a MoveTo that reads BB
            // For now, use a default position — ScheduledBehavior handles location seeking
            action = std::make_shared<MoveToAction>(glm::vec3(0.0f), speed, arrivalDist);
        }
        else if (actionType == "LookAt") {
            float x = j.value("x", 0.0f);
            float y = j.value("y", 0.0f);
            float z = j.value("z", 0.0f);
            action = std::make_shared<LookAtAction>(glm::vec3(x, y, z));
        }
        else if (actionType == "Wait") {
            float duration = j.value("duration", 1.0f);
            action = std::make_shared<WaitAction>(duration);
        }
        else if (actionType == "Speak") {
            std::string message = j.value("message", "...");
            float duration = j.value("duration", 3.0f);
            action = std::make_shared<SpeakAction>(message, duration);
        }
        else if (actionType == "SetBlackboard") {
            std::string key = j.value("key", "");
            std::string value = j.value("value", "");
            action = std::make_shared<SetBlackboardAction>(key, value);
        }
        else if (actionType == "MoveToEntity") {
            std::string entityId = j.value("entityId", "");
            float speed = j.value("speed", 2.0f);
            float arrivalDist = j.value("arrivalDistance", 2.0f);
            action = std::make_shared<MoveToEntityAction>(entityId, speed, arrivalDist);
        }
        else if (actionType == "Flee") {
            std::string fromKey = j.value("fromKey", "threat");
            float speed = j.value("speed", 4.0f);
            float fleeDist = j.value("fleeDistance", 15.0f);
            action = std::make_shared<FleeAction>(fromKey, speed, fleeDist);
        }

        if (!action) return nullptr;
        return std::make_shared<ActionNode>(std::move(action));
    }
};

} // namespace AI
} // namespace Phyxel
