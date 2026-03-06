#pragma once

#include "scene/CubeLocation.h"
#include <glm/glm.hpp>

namespace Phyxel {

/**
 * Context data passed to interaction tools during an action.
 * Contains information about the current state of the world/input relevant to interaction.
 */
struct InteractionContext {
    CubeLocation hoveredLocation;
    glm::vec3 cameraPosition;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;
    bool hasHovered;
    
    // Helper to check if we can interact
    bool isValid() const { return hasHovered && hoveredLocation.isValid(); }
};

}
