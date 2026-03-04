#include "scene/interaction/DestructionTool.h"
#include "scene/VoxelManipulationSystem.h"
#include "utils/Logger.h"

namespace VulkanCube {

DestructionTool::DestructionTool(VoxelManipulationSystem* manipulator)
    : m_manipulator(manipulator) {}

bool DestructionTool::breakCube(const InteractionContext& context) {
    if (!context.isValid()) return false;
    
    // Break regular cube into dynamic cube with physics
    return m_manipulator->breakCube(context.hoveredLocation, context.cameraPosition);
}

bool DestructionTool::breakSubcube(const InteractionContext& context) {
    if (!context.isValid()) return false;
    
    // Break subcube with physics
    return m_manipulator->breakSubcube(context.hoveredLocation);
}

bool DestructionTool::breakMicrocube(const InteractionContext& context) {
    if (!context.isValid()) return false;
    
    // Break microcube
    return m_manipulator->breakMicrocube(context.hoveredLocation);
}

bool DestructionTool::subdivideCube(const InteractionContext& context) {
    if (!context.isValid()) return false;
    
    return m_manipulator->subdivideCube(context.hoveredLocation);
}

bool DestructionTool::subdivideSubcube(const InteractionContext& context) {
    if (!context.isValid()) return false;
    
    return m_manipulator->subdivideSubcube(context.hoveredLocation);
}

}
