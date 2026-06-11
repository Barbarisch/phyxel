#pragma once

#include "core/InteractionHandler.h"
#include <functional>

namespace Phyxel {
namespace Core {

/// Handles picking up world item props ([E] on a category="item" placed object).
///
/// The handler itself is thin: it fires a callback with the placed-object id.
/// The host (Application) resolves the prop via ItemPropManager, credits the
/// player's inventory, and removes the prop.
class PickupInteractionHandler : public InteractionHandler {
public:
    /// objectId = the PlacedObject id of the item prop.
    using PickupCallback = std::function<void(const std::string& objectId)>;

    const char* getType() const override { return "pickup"; }
    float getDefaultRadius() const override { return 2.0f; }
    const char* getDefaultPrompt() const override { return "Take"; }
    int getPriority() const override { return 30; }  // beats seats(10)/doors(20) when overlapping

    void setPickupCallback(PickupCallback cb) { m_callback = std::move(cb); }

    void execute(const InteractionContext& ctx) override {
        if (m_callback && !ctx.objectId.empty()) m_callback(ctx.objectId);
    }

private:
    PickupCallback m_callback;
};

} // namespace Core
} // namespace Phyxel
