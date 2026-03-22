#pragma once

#include "story/CharacterAgent.h"
#include <unordered_map>
#include <functional>

namespace Phyxel {
namespace Story {

// ============================================================================
// RuleBasedCharacterAgent — offline fallback with no AI backend required.
//
// Uses personality traits, emotional state, goals, and relationships to make
// deterministic decisions via priority-weighted rules.
// ============================================================================

class RuleBasedCharacterAgent : public CharacterAgent {
public:
    /// A rule maps a condition (evaluated against context) to an action.
    struct Rule {
        std::string name;
        /// Returns a priority score (0 = don't fire, higher = more urgent).
        std::function<float(const CharacterDecisionContext&)> condition;
        /// Produces the decision if this rule wins.
        std::function<CharacterDecision(const CharacterDecisionContext&)> action;
    };

    RuleBasedCharacterAgent() = default;

    /// Add a custom rule. Higher-priority rules take precedence.
    void addRule(Rule rule);

    /// Clear all custom rules (keeps built-in rules).
    void clearRules();

    /// Set response templates for ambient dialogue keyed by emotion.
    void setDialogueTemplates(const std::unordered_map<std::string, std::vector<std::string>>& templates);

    // --- CharacterAgent interface ---
    CharacterDecision decide(const CharacterDecisionContext& context) override;
    std::string generateDialogue(const CharacterDecisionContext& context) override;
    std::string getAgentName() const override { return "RuleBased"; }

private:
    std::vector<Rule> m_rules;
    std::unordered_map<std::string, std::vector<std::string>> m_dialogueTemplates;

    // Built-in decision logic using personality + goals + relationships
    CharacterDecision decideFromPersonality(const CharacterDecisionContext& context) const;
    std::string pickDialogueTemplate(const std::string& emotion) const;

    // Scoring helpers
    static float scoreFlee(const CharacterDecisionContext& ctx);
    static float scoreAttack(const CharacterDecisionContext& ctx);
    static float scoreTrade(const CharacterDecisionContext& ctx);
    static float scoreIdle(const CharacterDecisionContext& ctx);
};

} // namespace Story
} // namespace Phyxel
