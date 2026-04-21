#include "core/interactions/SeatInteractionHandler.h"
#include "core/PlacedObjectManager.h"
#include "core/InteractionProfileManager.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

/// Rotate a template-local offset by object rotation (0/90/180/270 degrees around Y).
static glm::vec3 rotateOffsetByDegrees(const glm::vec3& offset, int rotationDegrees) {
    switch (rotationDegrees % 360) {
        case 90:  return { -offset.z, offset.y,  offset.x };
        case 180: return { -offset.x, offset.y, -offset.z };
        case 270: return {  offset.z, offset.y, -offset.x };
        default:  return offset;
    }
}

void SeatInteractionHandler::execute(const InteractionContext& ctx) {
    if (!m_callback) return;

    // Claim the seat so nobody else (NPC) can grab it
    if (m_placedObjects) {
        m_placedObjects->claimInteractionPoint(ctx.objectId, ctx.pointId, "player");
    }

    // Read seat-specific data from typeData (populated by InteractionManager from the InteractionPoint)
    glm::vec3 sitDown{0.0f}, sittingIdle{0.0f}, sitStandUp{0.0f};
    float blendDur = 0.0f, heightOff = 0.0f;

    if (ctx.typeData.contains("sitDownOffset")) {
        auto& a = ctx.typeData["sitDownOffset"];
        sitDown = glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
    }
    if (ctx.typeData.contains("sittingIdleOffset")) {
        auto& a = ctx.typeData["sittingIdleOffset"];
        sittingIdle = glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
    }
    if (ctx.typeData.contains("sitStandUpOffset")) {
        auto& a = ctx.typeData["sitStandUpOffset"];
        sitStandUp = glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
    }
    if (ctx.typeData.contains("sitBlendDuration")) {
        blendDur = ctx.typeData["sitBlendDuration"].get<float>();
    }
    if (ctx.typeData.contains("seatHeightOffset")) {
        heightOff = ctx.typeData["seatHeightOffset"].get<float>();
    }

    // Resolve per-archetype profile offsets (override asset defaults if profile exists)
    if (m_profileManager) {
        const auto* profile = m_profileManager->getProfile(
            ctx.playerArchetype, ctx.templateName, ctx.pointId);
        if (profile) {
            sitDown     = rotateOffsetByDegrees(profile->sitDownOffset,    ctx.objectRotation);
            sittingIdle = rotateOffsetByDegrees(profile->sittingIdleOffset, ctx.objectRotation);
            sitStandUp  = rotateOffsetByDegrees(profile->sitStandUpOffset,  ctx.objectRotation);
            blendDur    = profile->sitBlendDuration;
            heightOff   = profile->seatHeightOffset;
            LOG_INFO("SeatInteraction", "Using profile for archetype '{}' template '{}' point '{}'",
                     ctx.playerArchetype, ctx.templateName, ctx.pointId);
        }
    }

    LOG_INFO("SeatInteraction", "Player sitting at '{}':'{}'", ctx.objectId, ctx.pointId);
    m_callback(ctx.objectId, ctx.pointId, ctx.worldPos, ctx.facingYaw,
               sitDown, sittingIdle, sitStandUp, blendDur, heightOff);
}

} // namespace Core
} // namespace Phyxel
