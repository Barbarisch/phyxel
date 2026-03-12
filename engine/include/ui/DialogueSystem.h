#pragma once

#include "ui/DialogueData.h"
#include <string>
#include <vector>
#include <functional>

namespace Phyxel {

namespace Scene { class NPCEntity; }
namespace Core { class GameEventLog; }

namespace UI {

/// States of the dialogue state machine.
enum class DialogueState {
    Inactive,        ///< No conversation active
    Typing,          ///< Text being revealed character by character
    WaitingForInput, ///< Full text shown, waiting for Enter to advance
    ChoiceSelection  ///< Choices displayed, waiting for 1-4 key
};

/// Manages RPG-style dialogue conversations with NPCs.
/// Handles typewriter reveal, choice selection, and tree traversal.
class DialogueSystem {
public:
    using ConversationEndCallback = std::function<void(const std::string& npcName,
                                                       const std::string& lastNodeId)>;

    DialogueSystem() = default;

    /// Start a conversation with an NPC using a dialogue tree.
    /// Returns false if a conversation is already active.
    bool startConversation(Scene::NPCEntity* npc, const DialogueTree* tree);

    /// Start a conversation with just a name and tree (no NPC entity needed).
    bool startConversation(const std::string& speakerName, const DialogueTree* tree);

    /// Advance dialogue: skip typewriter or go to next node (Enter key).
    void advanceDialogue();

    /// Select a choice by index (0-based). Only valid in ChoiceSelection state.
    void selectChoice(int index);

    /// End the current conversation (Esc key).
    void endConversation();

    /// Update typewriter effect.
    void update(float dt);

    // --- State queries ---
    bool isActive() const { return m_state != DialogueState::Inactive; }
    DialogueState getState() const { return m_state; }

    /// Speaker name for the current node.
    const std::string& getCurrentSpeaker() const { return m_currentSpeaker; }
    /// Full text of the current node.
    const std::string& getCurrentText() const { return m_currentFullText; }
    /// Revealed portion of text (typewriter effect).
    const std::string& getRevealedText() const { return m_revealedText; }
    /// Current emotion tag.
    const std::string& getCurrentEmotion() const { return m_currentEmotion; }
    /// Available choices (empty if not in ChoiceSelection state or linear node).
    const std::vector<DialogueChoice>& getAvailableChoices() const { return m_availableChoices; }
    /// The NPC entity in conversation (may be nullptr for dynamic dialogues).
    Scene::NPCEntity* getConversationNPC() const { return m_npc; }

    // --- Configuration ---
    /// Characters revealed per second (default 30).
    void setTypingSpeed(float charsPerSecond) { m_typingSpeed = charsPerSecond; }
    float getTypingSpeed() const { return m_typingSpeed; }

    /// Set callback for when conversation ends.
    void setConversationEndCallback(ConversationEndCallback callback) {
        m_endCallback = std::move(callback);
    }

    /// Set the game event log for emitting conversation events.
    void setGameEventLog(Core::GameEventLog* log) { m_gameEventLog = log; }

private:
    void loadNode(const std::string& nodeId);
    void finishTyping();

    DialogueState m_state = DialogueState::Inactive;
    const DialogueTree* m_tree = nullptr;
    Scene::NPCEntity* m_npc = nullptr;
    std::string m_npcName;

    // Current node state
    std::string m_currentNodeId;
    std::string m_currentSpeaker;
    std::string m_currentFullText;
    std::string m_currentEmotion;
    std::string m_revealedText;
    std::vector<DialogueChoice> m_availableChoices;

    // Typewriter
    float m_typingSpeed = 30.0f; // chars per second
    float m_typingProgress = 0.0f;
    int m_revealedCharCount = 0;

    // Callbacks
    ConversationEndCallback m_endCallback;
    Core::GameEventLog* m_gameEventLog = nullptr;
};

} // namespace UI
} // namespace Phyxel
