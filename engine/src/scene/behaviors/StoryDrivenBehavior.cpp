#include "scene/behaviors/StoryDrivenBehavior.h"
#include "story/StoryEngine.h"
#include "scene/Entity.h"
#include "ui/SpeechBubbleManager.h"
#include "core/LocationRegistry.h"
#include "core/NavGraph.h"
#include "core/PathService.h"
#include "graphics/DayNightCycle.h"
#include <glm/gtc/quaternion.hpp>
#include <sstream>
#include <random>
#include <cmath>

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
    if (!m_agent || !m_profile || !ctx.self) return;

    if (!m_anchorSet) {
        m_anchor = ctx.self->getPosition();
        m_anchorSet = true;
    }

    // Decision tick — re-evaluate what to do every decisionInterval seconds.
    m_decisionTimer += dt;
    if (m_decisionTimer >= m_decisionInterval) {
        m_decisionTimer = 0.0f;

        // A daily routine, when present, decides WHERE the character goes; the agent
        // still runs for emotion/reasoning and flavor. Without a schedule (or with no
        // entry for this hour) we fall back to the agent's own action (wander/idle).
        bool scheduleHandled = m_hasSchedule && applySchedule(ctx);

        auto context = buildContext(ctx);
        m_lastDecision = m_agent->decide(context);
        if (m_onDecision) m_onDecision(ctx.selfId, m_lastDecision);

        if (!scheduleHandled) applyDecision(ctx);
        maybeAmbientChatter(ctx);
    }

    // Steering runs every frame so movement is smooth between decision ticks.
    updateMovement(dt, ctx);
}

// Route the character according to its daily schedule. Returns true when the schedule
// dictated an intent (heading to / staying at a location); false to let the agent decide.
bool StoryDrivenBehavior::applySchedule(NPCContext& ctx) {
    if (!ctx.dayNightCycle || !ctx.self) return false;

    float hour = ctx.dayNightCycle->getTimeOfDay();
    const AI::ScheduleEntry* entry = m_schedule.getCurrentActivity(hour);
    if (!entry) { m_currentActivity.clear(); return false; }

    m_currentActivity = AI::ScheduleEntry::activityToString(entry->activity);

    // Resolve the destination for this block, if any.
    if (!entry->locationId.empty() && ctx.locationRegistry) {
        const auto* loc = ctx.locationRegistry->getLocation(entry->locationId);
        if (loc) {
            glm::vec3 pos = ctx.self->getPosition();
            float distXZ = glm::length(glm::vec2(loc->position.x - pos.x,
                                                 loc->position.z - pos.z));
            if (distXZ > loc->radius) {
                m_roamTarget = loc->position;   // travel to the scheduled place
                m_moving = true;
            } else {
                m_moving = false;               // arrived — perform the activity here
            }
            return true;
        }
    }

    // No location for this block: "Wander" lets the agent roam; everything else (Sleep,
    // Eat, Guard, ...) holds position where they are.
    if (entry->activity == AI::ActivityType::Wander) return false;
    m_moving = false;
    return true;
}

// Translate the latest decision into an embodied intent.
void StoryDrivenBehavior::applyDecision(NPCContext& ctx) {
    const std::string& a = m_lastDecision.action;
    if (a == "speak" && !m_lastDecision.dialogueText.empty()) {
        sayBubble(ctx, m_lastDecision.dialogueText);
        m_moving = false;
    } else if (a == "move_to") {
        pickRoamTarget(ctx);   // no goal->coordinate mapping yet: wander near the anchor
        m_moving = true;
    } else {
        // idle / wait / (flee/attack/trade not embodied yet) → hold position
        m_moving = false;
    }
}

void StoryDrivenBehavior::updateMovement(float /*dt*/, NPCContext& ctx) {
    if (!ctx.self) return;
    if (!m_moving) { ctx.self->setMoveVelocity(glm::vec3(0.0f)); return; }

    const glm::vec3 pos = ctx.self->getPosition();

    // Decide the immediate steering point: a NavGraph path waypoint if we can route
    // (around obstacles/edges), otherwise the destination directly (legacy).
    glm::vec3 steerTo = m_roamTarget;
    if (ctx.navGraph || ctx.pathService) {
        // (Re)plan when the destination changed, or we have neither a path nor a query
        // already in flight for it.
        const bool destChanged = !m_hasPathTarget || glm::distance(m_pathTarget, m_roamTarget) > 0.5f;
        if (destChanged || (m_path.empty() && !m_pathPending)) {
            replanPath(ctx, pos);
        }

        // Async path service: poll the in-flight query; until it lands we hold position
        // (keep m_moving so we keep polling next frame) rather than walk blindly.
        if (m_pathPending && ctx.pathService) {
            Core::NavGraph::PathResult res;
            if (ctx.pathService->tryGetResult(m_pathHandle, res)) {
                m_pathPending = false;
                m_pathHandle = 0;
                if (res.found) { m_path = std::move(res.waypoints); m_pathIndex = 0; }
            }
        }
        if (m_pathPending) {             // query still running — hold, keep polling
            ctx.self->setMoveVelocity(glm::vec3(0.0f));
            return;
        }
        if (m_path.empty()) {            // no route — hold position rather than walk blindly off an edge
            m_moving = false;
            ctx.self->setMoveVelocity(glm::vec3(0.0f));
            return;
        }
        // Advance through reached waypoints.
        while (m_pathIndex < m_path.size()) {
            glm::vec3 d = m_path[m_pathIndex] - pos;
            if (glm::length(glm::vec2(d.x, d.z)) < 0.5f) ++m_pathIndex;
            else break;
        }
        if (m_pathIndex >= m_path.size()) {   // reached the end of the path
            m_moving = false;
            m_path.clear();
            m_hasPathTarget = false;
            ctx.self->setMoveVelocity(glm::vec3(0.0f));
            return;
        }
        steerTo = m_path[m_pathIndex];
    }

    glm::vec3 diff = steerTo - pos;
    float distXZ = glm::length(glm::vec2(diff.x, diff.z));
    if (distXZ < 0.4f) {            // arrived at the (direct) destination
        m_moving = false;
        ctx.self->setMoveVelocity(glm::vec3(0.0f));
        return;
    }
    glm::vec3 dir = glm::normalize(glm::vec3(diff.x, 0.0f, diff.z));
    ctx.self->setMoveVelocity(dir * m_walkSpeed);          // gravity handles Y
    ctx.self->setRotation(glm::angleAxis(std::atan2(dir.x, dir.z), glm::vec3(0, 1, 0)));
}

void StoryDrivenBehavior::replanPath(NPCContext& ctx, const glm::vec3& from) {
    m_path.clear();
    m_pathIndex = 0;
    m_pathTarget = m_roamTarget;
    m_hasPathTarget = true;

    Core::NavAgentProfile agent;   // default humanoid; could derive from the profile later

    if (ctx.pathService) {
        // Async: cancel any superseded in-flight query, then queue a fresh one. Movement
        // holds (handled in updateMovement) until the worker delivers the result.
        if (m_pathPending && m_pathHandle) ctx.pathService->cancel(m_pathHandle);
        m_pathHandle = ctx.pathService->requestPath(agent, from, m_roamTarget);
        m_pathPending = (m_pathHandle != 0);
        return;
    }

    // Synchronous fallback (no path service wired): query the graph on this thread.
    m_pathPending = false;
    if (!ctx.navGraph) return;
    auto result = ctx.navGraph->findPath(from, m_roamTarget, agent);
    if (result.found) {
        // Match the PathService path: smooth so the agent cuts corners instead of
        // stair-stepping the 4-connected grid.
        m_path = result.waypoints.size() > 2
                     ? ctx.navGraph->smoothWaypoints(result.waypoints, agent)
                     : std::move(result.waypoints);
    }
}

void StoryDrivenBehavior::invalidatePath() {
    // Clear the route and force a replan on the next updateMovement. We deliberately leave
    // m_pathPending/m_pathHandle untouched: replanPath() cancels a stale in-flight query
    // when it re-requests, so a pending result can't leak or be followed.
    m_path.clear();
    m_pathIndex = 0;
    m_hasPathTarget = false;
}

void StoryDrivenBehavior::pickRoamTarget(NPCContext& ctx) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> ang(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> rad(2.0f, 6.0f);
    float a = ang(rng), r = rad(rng);
    glm::vec3 base = m_anchorSet ? m_anchor : ctx.self->getPosition();
    m_roamTarget = base + glm::vec3(std::cos(a) * r, 0.0f, std::sin(a) * r);
}

// Personality-driven idle chatter so a character's temperament is visible even when
// it isn't in a conversation. Extraverts speak up; introverts mostly stay quiet.
void StoryDrivenBehavior::maybeAmbientChatter(NPCContext& ctx) {
    if (!ctx.speechBubbleManager || ctx.selfId.empty()) return;
    if (m_currentActivity == "Sleep") return;   // don't chatter in their sleep
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    float chance = 0.05f + m_profile->traits.extraversion * 0.20f;   // ~0.05..0.25 per tick
    if (roll(rng) >= chance) return;

    Story::CharacterDecisionContext c;
    c.profile = m_profile;
    std::string line = m_agent->generateDialogue(c);
    if (!line.empty()) sayBubble(ctx, line);
}

void StoryDrivenBehavior::sayBubble(NPCContext& ctx, const std::string& text) {
    if (ctx.speechBubbleManager && !ctx.selfId.empty()) {
        ctx.speechBubbleManager->say(ctx.selfId, text, 3.0f);
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
