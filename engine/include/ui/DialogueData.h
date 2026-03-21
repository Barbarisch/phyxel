#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace UI {

/// A single choice the player can make during dialogue.
struct DialogueChoice {
    std::string text;          ///< Display text shown to player
    std::string targetNodeId;  ///< Node to jump to when selected
    /// Optional condition — if set and returns false, choice is hidden.
    std::function<bool()> condition;
};

/// A single node in a dialogue tree — one "page" of conversation.
struct DialogueNode {
    std::string id;
    std::string speaker;       ///< Name shown above the text (e.g. "Guard")
    std::string text;          ///< The dialogue text content
    std::string emotion;       ///< Optional emotion tag (e.g. "happy", "angry")
    std::vector<DialogueChoice> choices;  ///< Player choices (empty = linear advance)
    std::string nextNodeId;    ///< Next node if no choices (empty = end of conversation)
};

/// A complete dialogue tree loaded from JSON or constructed programmatically.
struct DialogueTree {
    std::string id;            ///< Unique tree identifier
    std::string startNodeId;   ///< First node to display
    std::unordered_map<std::string, DialogueNode> nodes;

    /// Get a node by ID. Returns nullptr if not found.
    const DialogueNode* getNode(const std::string& nodeId) const {
        auto it = nodes.find(nodeId);
        return (it != nodes.end()) ? &it->second : nullptr;
    }

    /// Check whether this tree has a specific node.
    bool hasNode(const std::string& nodeId) const {
        return nodes.count(nodeId) > 0;
    }

    /// Deserialize from JSON.
    static DialogueTree fromJson(const nlohmann::json& j) {
        DialogueTree tree;
        tree.id = j.value("id", "");
        tree.startNodeId = j.value("startNodeId", "");

        if (j.contains("nodes") && j["nodes"].is_array()) {
            for (const auto& nodeJson : j["nodes"]) {
                DialogueNode node;
                node.id = nodeJson.value("id", "");
                node.speaker = nodeJson.value("speaker", "");
                node.text = nodeJson.value("text", "");
                node.emotion = nodeJson.value("emotion", "");
                node.nextNodeId = nodeJson.value("nextNodeId", "");

                if (nodeJson.contains("choices") && nodeJson["choices"].is_array()) {
                    for (const auto& choiceJson : nodeJson["choices"]) {
                        DialogueChoice choice;
                        choice.text = choiceJson.value("text", "");
                        choice.targetNodeId = choiceJson.value("targetNodeId", "");
                        // condition is not serializable — set programmatically
                        node.choices.push_back(std::move(choice));
                    }
                }
                tree.nodes[node.id] = std::move(node);
            }
        }
        return tree;
    }

    /// Serialize to JSON.
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["id"] = id;
        j["startNodeId"] = startNodeId;
        nlohmann::json nodesArr = nlohmann::json::array();
        for (const auto& [nodeId, node] : nodes) {
            nlohmann::json nodeJson;
            nodeJson["id"] = node.id;
            nodeJson["speaker"] = node.speaker;
            nodeJson["text"] = node.text;
            nodeJson["emotion"] = node.emotion;
            nodeJson["nextNodeId"] = node.nextNodeId;
            nlohmann::json choicesArr = nlohmann::json::array();
            for (const auto& choice : node.choices) {
                choicesArr.push_back({{"text", choice.text}, {"targetNodeId", choice.targetNodeId}});
            }
            nodeJson["choices"] = choicesArr;
            nodesArr.push_back(nodeJson);
        }
        j["nodes"] = nodesArr;
        return j;
    }
};

/// Interface for providing dialogue trees to NPCs.
/// Concrete implementations can load from JSON or generate dynamically via AI.
class DialogueProvider {
public:
    virtual ~DialogueProvider() = default;
    /// Get the dialogue tree to use for the current context.
    virtual const DialogueTree* getDialogueTree() const = 0;
};

/// Simple provider that holds a single loaded tree.
class StaticDialogueProvider : public DialogueProvider {
public:
    explicit StaticDialogueProvider(DialogueTree tree) : m_tree(std::move(tree)) {}
    const DialogueTree* getDialogueTree() const override { return &m_tree; }
    void setTree(DialogueTree tree) { m_tree = std::move(tree); }
private:
    DialogueTree m_tree;
};

/// Load a dialogue tree from a JSON file on disk.
/// Returns std::nullopt on failure (file not found or parse error).
std::optional<DialogueTree> loadDialogueFile(const std::string& filePath);

/// List all .json dialogue files in a directory.
std::vector<std::string> listDialogueFiles(const std::string& dirPath);

} // namespace UI
} // namespace Phyxel
