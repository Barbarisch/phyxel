#include "ui/DialogueSystem.h"
#include "scene/NPCEntity.h"
#include "core/GameEventLog.h"
#include "utils/Logger.h"
#include <fstream>
#include <filesystem>
#include <algorithm>

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
            {"npc", m_npcName}, {"lastNode", m_currentNodeId},
            {"ai_mode", m_isAIMode}
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

    // Reset AI state
    m_isAIMode = false;
    m_inputBuffer[0] = '\0';
    m_conversationHistory.clear();
    m_aiSendCallback = nullptr;
    m_aiEnhanceCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_hasPendingResponse = false;
        m_pendingResponseText.clear();
        m_pendingResponseEmotion.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_enhanceMutex);
        m_hasPendingEnhancement = false;
        m_pendingEnhanceNodeId.clear();
        m_pendingEnhanceText.clear();
        m_pendingEnhanceEmotion.clear();
    }
}

void DialogueSystem::update(float dt) {
    // Check for pending AI response (thread-safe)
    if (m_isAIMode && m_state == DialogueState::AIWaitingForResponse) {
        applyPendingAIResponse();
    }

    // Check for pending AI enhancement (hybrid dialogue)
    if (m_aiEnhanceCallback) {
        applyPendingEnhancement();
    }

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

    // Request AI enhancement if available (async — static text shows immediately)
    if (m_aiEnhanceCallback) {
        m_aiEnhanceCallback(node->id, node->text, node->speaker, node->emotion);
    }
}

void DialogueSystem::finishTyping() {
    m_revealedText = m_currentFullText;
    m_revealedCharCount = static_cast<int>(m_currentFullText.size());

    if (m_isAIMode) {
        // In AI mode, after NPC finishes typing, go back to text input
        m_state = DialogueState::AITextInput;
        return;
    }

    if (!m_availableChoices.empty()) {
        m_state = DialogueState::ChoiceSelection;
    } else {
        m_state = DialogueState::WaitingForInput;
    }
}

// ============================================================================
// AI Conversation
// ============================================================================

bool DialogueSystem::startAIConversation(Scene::NPCEntity* npc, const std::string& npcName,
                                          AISendCallback sendCallback) {
    if (m_state != DialogueState::Inactive) return false;
    if (!sendCallback) return false;

    m_npc = npc;
    m_npcName = npcName;
    m_isAIMode = true;
    m_aiSendCallback = std::move(sendCallback);
    m_inputBuffer[0] = '\0';
    m_conversationHistory.clear();

    m_currentSpeaker = npcName;
    m_currentFullText = "...";
    m_currentEmotion.clear();
    m_revealedText = "...";

    // Start in waiting state — NPC will greet the player first
    m_state = DialogueState::AIWaitingForResponse;

    LOG_INFO("DialogueSystem", "Started AI conversation with '{}' (awaiting greeting)", npcName);

    if (m_gameEventLog) {
        m_gameEventLog->emit("ai_conversation_started", {{"npc", npcName}});
    }
    return true;
}

void DialogueSystem::submitPlayerMessage() {
    if (m_state != DialogueState::AITextInput) return;
    submitAIInput(std::string(m_inputBuffer));
    m_inputBuffer[0] = '\0';
}

void DialogueSystem::submitAIInput(const std::string& input) {
    if (m_state != DialogueState::AITextInput && m_state != DialogueState::AIWaitingForResponse) return;

    std::string message = input;
    // Trim whitespace
    size_t start = message.find_first_not_of(" \t\n\r");
    size_t end = message.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return; // empty message
    message = message.substr(start, end - start + 1);

    if (message.empty()) return;

    // Add player message to history
    m_conversationHistory.push_back({"Player", message, ""});

    // Show "waiting" state
    m_currentSpeaker = m_npcName;
    m_currentFullText = "...";
    m_revealedText = "...";
    m_state = DialogueState::AIWaitingForResponse;

    LOG_DEBUG("DialogueSystem", "Player said to '{}': {}", m_npcName, message);

    // Send to AI
    if (m_aiSendCallback) {
        m_aiSendCallback(message);
    }
}

void DialogueSystem::receiveAIResponse(const std::string& text, const std::string& emotion) {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingResponseText = text;
    m_pendingResponseEmotion = emotion;
    m_hasPendingResponse = true;
}

void DialogueSystem::applyPendingAIResponse() {
    std::string text, emotion;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (!m_hasPendingResponse) return;
        text = std::move(m_pendingResponseText);
        emotion = std::move(m_pendingResponseEmotion);
        m_hasPendingResponse = false;
    }

    // Add NPC response to history
    m_conversationHistory.push_back({m_npcName, text, emotion});

    // Start typewriter effect for the response
    m_currentSpeaker = m_npcName;
    m_currentFullText = text;
    m_currentEmotion = emotion;
    m_revealedText.clear();
    m_typingProgress = 0.0f;
    m_revealedCharCount = 0;
    m_state = DialogueState::Typing;

    LOG_DEBUG("DialogueSystem", "AI '{}' responded: {}", m_npcName, text);
}

// ============================================================================
// AI Enhancement (hybrid dialogue)
// ============================================================================

void DialogueSystem::receiveAIEnhancement(const std::string& nodeId, const std::string& text, const std::string& emotion) {
    std::lock_guard<std::mutex> lock(m_enhanceMutex);
    m_pendingEnhanceNodeId = nodeId;
    m_pendingEnhanceText = text;
    m_pendingEnhanceEmotion = emotion;
    m_hasPendingEnhancement = true;
}

void DialogueSystem::applyPendingEnhancement() {
    std::string text, emotion, nodeId;
    {
        std::lock_guard<std::mutex> lock(m_enhanceMutex);
        if (!m_hasPendingEnhancement) return;
        text = std::move(m_pendingEnhanceText);
        emotion = std::move(m_pendingEnhanceEmotion);
        nodeId = std::move(m_pendingEnhanceNodeId);
        m_hasPendingEnhancement = false;
    }

    // Only apply if still on the same node
    if (nodeId != m_currentNodeId) return;

    LOG_INFO("DialogueSystem", "AI enhancement applied for node '{}': {}", nodeId, text);

    // Replace text with the AI-enhanced version
    m_currentFullText = text;
    if (!emotion.empty()) m_currentEmotion = emotion;

    if (m_state == DialogueState::Typing) {
        // Still typing — restart typewriter with AI text
        m_revealedText.clear();
        m_typingProgress = 0.0f;
        m_revealedCharCount = 0;
    } else {
        // Already past typing (e.g. ChoiceSelection) — swap in the final text directly
        m_revealedText = text;
        m_revealedCharCount = static_cast<int>(text.size());
    }
}

// ============================================================================
// Dialogue file I/O utilities
// ============================================================================

std::optional<DialogueTree> loadDialogueFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_WARN("DialogueData", "Could not open dialogue file: {}", filePath);
        return std::nullopt;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(file);
        return DialogueTree::fromJson(j);
    } catch (const std::exception& e) {
        LOG_ERROR("DialogueData", "Failed to parse dialogue file '{}': {}", filePath, e.what());
        return std::nullopt;
    }
}

std::vector<std::string> listDialogueFiles(const std::string& dirPath) {
    std::vector<std::string> files;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path().filename().string());
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("DialogueData", "Could not list dialogue directory '{}': {}", dirPath, e.what());
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace UI
} // namespace Phyxel
