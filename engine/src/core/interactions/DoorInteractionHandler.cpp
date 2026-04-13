#include "core/interactions/DoorInteractionHandler.h"
#include "core/DoorManager.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

void DoorInteractionHandler::execute(const InteractionContext& ctx) {
    LOG_INFO("DoorInteraction", "Player toggling door '{}'", ctx.objectId);
    if (m_callback) {
        m_callback(ctx.objectId, ctx.pointId);
    } else if (m_doorManager) {
        m_doorManager->toggle(ctx.objectId);
    }
}

} // namespace Core
} // namespace Phyxel
