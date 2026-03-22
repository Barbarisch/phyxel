#include "scene/behaviors/StoryDrivenBehavior.h"
#include "story/StoryEngine.h"
#include "scene/Entity.h"
#include <sstream>

namespace Phyxel {
namespace Scene {

StoryDrivenBehavior::StoryDrivenBehavior(Story::CharacterAgent* agent,
                                           Story::CharacterProfile* profile,
                                           Story::CharacterMemory* memory,
                                           Story::StoryEngine* storyEngine)
    : m_agent(agent)
    , m_profile(profile)
    , m_memory(memory)
    , m_storyEngine(storyEngine)
{
}

void StoryDrivenBehavior::update(float dt, NPCContext& ctx) {
    if (!m_agent || !m_profile) return;

    m_decisionTimer += dt;
    if (m_decisionTimer < m_decisionInterval) return;
    m_decisionTimer = 0.0f;

    // Build context and decide
    auto context = buildContext(ctx);
    m_lastDecision = m_agent->decide(context);

    // Notify callback
    if (m_onDecision) {
        m_onDecision(ctx.selfId, m_lastDecision);
    }

    // Show speech bubble if speaking
    if (m_lastDecision.action == "speak" && !m_lastDecision.dialogueText.empty()) {
        if (ctx.speechBubbleManager) {
            // Use speech bubble manager to display the dialogue
            // The speechBubbleManager API is assumed to accept entity ID + text
        }
    }
}

void StoryDrivenBehavior::onInteract(Entity* interactor) {
    if (!m_agent || !m_profile) return;

    // Start or continue a conversation
    // Try to look up the interactor's character profile
    Story::CharacterProfile* partnerProfile = nullptr;
    if (m_lookupProfile) {
        // The lookup callback maps entity pointer info to a character profile
        // Use m_conversationPartnerId if set, otherwise pass empty string
        partnerProfile = m_lookupProfile(m_conversationPartnerId);
    }

    // Build context with conversation partner
    Story::CharacterDecisionContext context;
    context.profile = m_profile;
    if (m_memory) {
        context.knowledgeSummary = m_memory->buildContextSummary(15);
    }
    context.currentSituation = "A character has approached and wants to interact.";
    context.conversationPartner = partnerProfile;
    context.conversationHistory = m_conversationHistory;

    if (m_profile->agencyLevel >= Story::AgencyLevel::Guided) {
        // AI-driven dialogue
        std::string dialogue = m_agent->generateDialogue(context);
        if (!dialogue.empty()) {
            m_conversationHistory += m_profile->name + ": " + dialogue + "\n";
            m_lastDecision.action = "speak";
            m_lastDecision.dialogueText = dialogue;

            if (m_onDecision) {
                m_onDecision(m_profile->id, m_lastDecision);
            }
        }
    } else {
        // Scripted/Templated — just get a decision
        m_lastDecision = m_agent->decide(context);
        if (m_onDecision) {
            m_onDecision(m_profile->id, m_lastDecision);
        }
    }
}

void StoryDrivenBehavior::onEvent(const std::string& eventType, const nlohmann::json& data) {
    // Store event as situation context for next decision cycle
    // The agent will see it via knowledgeSummary (if witnessed via EventBus)
    // or via currentSituation
}

Story::CharacterDecisionContext StoryDrivenBehavior::buildContext(const NPCContext& ctx) const {
    Story::CharacterDecisionContext context;
    context.profile = m_profile;

    if (m_memory) {
        context.knowledgeSummary = m_memory->buildContextSummary(15);
    }

    // Build situation description
    if (m_situationBuilder) {
        context.currentSituation = m_situationBuilder(ctx);
    } else {
        context.currentSituation = buildDefaultSituation(ctx);
    }

    // Set available actions from profile's allowed actions
    if (m_profile) {
        context.availableActions = m_profile->allowedActions;
    }

    return context;
}

std::string StoryDrivenBehavior::buildDefaultSituation(const NPCContext& ctx) const {
    std::ostringstream ss;

    if (ctx.self) {
        auto pos = ctx.self->getPosition();
        ss << "You are at position (" << pos.x << ", " << pos.y << ", " << pos.z << "). ";
    }

    // Count nearby entities if registry is available
    if (ctx.entityRegistry) {
        ss << "You are aware of your surroundings. ";
    }

    return ss.str();
}

} // namespace Scene
} // namespace Phyxel
