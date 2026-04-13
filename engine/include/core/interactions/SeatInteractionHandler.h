#pragma once

#include "core/InteractionHandler.h"
#include <functional>

namespace Phyxel {
namespace Core {

class PlacedObjectManager;
class InteractionProfileManager;

/// Handles seat interactions (chairs, benches, stools).
///
/// Extracts seat-specific logic: claims the seat, resolves per-archetype profile offsets,
/// and fires the sit callback with all required parameters.
class SeatInteractionHandler : public InteractionHandler {
public:
    using SeatCallback = std::function<void(const std::string& objectId,
                                            const std::string& pointId,
                                            const glm::vec3& seatAnchorPos,
                                            float facingYaw,
                                            const glm::vec3& sitDownOffset,
                                            const glm::vec3& sittingIdleOffset,
                                            const glm::vec3& sitStandUpOffset,
                                            float sitBlendDuration,
                                            float seatHeightOffset)>;

    SeatInteractionHandler(PlacedObjectManager* placedObjects,
                           InteractionProfileManager* profileManager)
        : m_placedObjects(placedObjects), m_profileManager(profileManager) {}

    const char* getType() const override { return "seat"; }
    float getDefaultRadius() const override { return 1.5f; }
    const char* getDefaultPrompt() const override { return "Sit down"; }
    std::vector<std::string> getRequiredAnimations() const override {
        return {"stand_to_sit", "sitting_idle", "sit_to_stand"};
    }
    int getPriority() const override { return 10; }

    void setSeatCallback(SeatCallback cb) { m_callback = std::move(cb); }

    void execute(const InteractionContext& ctx) override;

private:
    PlacedObjectManager* m_placedObjects = nullptr;
    InteractionProfileManager* m_profileManager = nullptr;
    SeatCallback m_callback;
};

} // namespace Core
} // namespace Phyxel
