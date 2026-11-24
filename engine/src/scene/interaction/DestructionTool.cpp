#include "scene/interaction/DestructionTool.h"
#include "scene/VoxelManipulationSystem.h"
#include "utils/Logger.h"

namespace Phyxel {

DestructionTool::DestructionTool(VoxelManipulationSystem* manipulator)
    : m_manipulator(manipulator) {}

bool DestructionTool::breakCube(const InteractionContext& context) {
    if (!context.isValid()) return false;
    
    // Break regular cube into dynamic cube with physics
    return m_manipulator->breakCube(context.hoveredLocation, context.cameraPosition);
}

}
