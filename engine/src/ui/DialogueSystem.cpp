#include "ui/DialogueSystem.h"
#include "scene/NPCEntity.h"
#include "core/GameEventLog.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace UI {

bool DialogueSystem::startConversation(Scene::NPCEntity* npc, const DialogueTree* tree) {
    if (!tree || m_state != DialogueState::Inactive) return false;

    m_npc = npc;
    m_npcName = npc ? npc->getName() : "";
    m_tree = tree;

    loadNode(tree->startNodeId);

    LOG_INFO("DialogueSystem", "Started conversation with '{}'", m_npcName);

    if (m_gameEventLog) {
        m_gameEventLog->emit("conversation_started", {
            {"npc", m_npcName}, {"tree", tree->id}
        });
    }
    return true;
}

bool DialogueSystem::startConversation(const std::string& speakerName, const DialogueTree* tree) {
    if (!tree || m_state != DialogueState::Inactive) return false;

    m_npc = nullptr;
    m_npcName = speakerName;
    m_tree = tree;

    loadNode(tree->startNodeId);

    LOG_INFO("DialogueSystem", "Started conversation with '{}'", speakerName);
    return true;
}

void DialogueSystem::advanceDialogue() {
    if (m_state == DialogueState::Typing) {
        // Skip typewriter — show full text
        finishTyping();
        return;
    }

    if (m_state == DialogueState::WaitingForInput) {
        // Advance to next node
        const auto* node = m_tree->getNode(m_currentNodeId);
        if (!node || node->nextNodeId.empty()) {
            endConversation();
            return;
        }
        loadNode(node->nextNodeId);
    }
    // ChoiceSelection: player must pick a choice, not advance
}

void DialogueSystem::selectChoice(int index) {
    if (m_state != DialogueState::ChoiceSelection) return;
    if (index < 0 || index >= static_cast<int>(m_availableChoices.size())) return;

    const auto& choice = m_availableChoices[index];

    if (m_gameEventLog) {
        m_gameEventLog->emit("dialogue_choice", {
            {"npc", m_npcName}, {"node", m_currentNodeId},
            {"choice_index", index}, {"choice_text", choice.text}
        });
    }

    if (choice.targetNodeId.empty()) {
        endConversation();
        return;
    }

    loadNode(choice.targetNodeId);
}

void DialogueSystem::endConversation() {
    if (m_state == DialogueState::Inactive) return;

    LOG_INFO("DialogueSystem", "Ended conversation with '{}'", m_npcName);

    if (m_gameEventLog) {
        m_gameEventLog->emit("conversation_ended", {
            {"npc", m_npcName}, {"lastNode", m_currentNodeId}
        });
    }

    if (m_endCallback) {
        m_endCallback(m_npcName, m_currentNodeId);
    }

    m_state = DialogueState::Inactive;
    m_tree = nullptr;
    m_npc = nullptr;
    m_npcName.clear();
    m_currentNodeId.clear();
    m_currentSpeaker.clear();
    m_currentFullText.clear();
    m_currentEmotion.clear();
    m_revealedText.clear();
    m_availableChoices.clear();
    m_typingProgress = 0.0f;
    m_revealedCharCount = 0;
}

void DialogueSystem::update(float dt) {
    if (m_state != DialogueState::Typing) return;

    m_typingProgress += dt * m_typingSpeed;
    int newCharCount = static_cast<int>(m_typingProgress);

    if (newCharCount >= static_cast<int>(m_currentFullText.size())) {
        finishTyping();
    } else if (newCharCount > m_revealedCharCount) {
        m_revealedCharCount = newCharCount;
        m_revealedText = m_currentFullText.substr(0, m_revealedCharCount);
    }
}

void DialogueSystem::loadNode(const std::string& nodeId) {
    const auto* node = m_tree->getNode(nodeId);
    if (!node) {
        LOG_WARN("DialogueSystem", "Node '{}' not found in tree '{}'", nodeId, m_tree->id);
        endConversation();
        return;
    }

    m_currentNodeId = node->id;
    m_currentSpeaker = node->speaker;
    m_currentFullText = node->text;
    m_currentEmotion = node->emotion;
    m_revealedText.clear();
    m_typingProgress = 0.0f;
    m_revealedCharCount = 0;

    // Filter available choices by condition
    m_availableChoices.clear();
    for (const auto& choice : node->choices) {
        if (!choice.condition || choice.condition()) {
            m_availableChoices.push_back(choice);
        }
    }

    m_state = DialogueState::Typing;
}

void DialogueSystem::finishTyping() {
    m_revealedText = m_currentFullText;
    m_revealedCharCount = static_cast<int>(m_currentFullText.size());

    if (!m_availableChoices.empty()) {
        m_state = DialogueState::ChoiceSelection;
    } else {
        m_state = DialogueState::WaitingForInput;
    }
}

} // namespace UI
} // namespace Phyxel
