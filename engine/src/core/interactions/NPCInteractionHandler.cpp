#include "core/interactions/NPCInteractionHandler.h"
#include "core/EntityRegistry.h"
#include "scene/NPCEntity.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

void NPCInteractionHandler::execute(const InteractionContext& ctx) {
    if (!ctx.npc) return;

    LOG_INFO("NPCInteraction", "Player interacting with NPC '{}'", ctx.npc->getName());

    if (auto* behavior = ctx.npc->getBehavior()) {
        behavior->onInteract(ctx.playerEntity);
    }

    if (m_callback) {
        m_callback(ctx.npc);
    }
}

} // namespace Core
} // namespace Phyxel
