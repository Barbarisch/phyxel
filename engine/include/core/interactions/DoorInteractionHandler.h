#pragma once

#include "core/InteractionHandler.h"
#include <functional>

namespace Phyxel {
namespace Core {

class DoorManager;

/// Handles door interactions (open/close toggle).
class DoorInteractionHandler : public InteractionHandler {
public:
    using DoorCallback = std::function<void(const std::string& objectId,
                                            const std::string& pointId)>;

    explicit DoorInteractionHandler(DoorManager* doorManager)
        : m_doorManager(doorManager) {}

    const char* getType() const override { return "door_handle"; }
    float getDefaultRadius() const override { return 2.0f; }
    const char* getDefaultPrompt() const override { return "Open / Close"; }
    std::vector<std::string> getRequiredAnimations() const override {
        return {"reach_forward", "push_door"};
    }
    int getPriority() const override { return 20; }

    void setDoorCallback(DoorCallback cb) { m_callback = std::move(cb); }

    void execute(const InteractionContext& ctx) override;

private:
    DoorManager* m_doorManager = nullptr;
    DoorCallback m_callback;
};

} // namespace Core
} // namespace Phyxel
