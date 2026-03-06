#pragma once

#include "InteractionContext.h"
#include <memory>

namespace Phyxel {

class VoxelManipulationSystem;

class DestructionTool {
public:
    DestructionTool(VoxelManipulationSystem* manipulator);
    ~DestructionTool() = default;

    bool breakCube(const InteractionContext& context);
    bool breakSubcube(const InteractionContext& context);
    bool breakMicrocube(const InteractionContext& context);
    
    // For subdivision
    bool subdivideCube(const InteractionContext& context);
    bool subdivideSubcube(const InteractionContext& context);

private:
    VoxelManipulationSystem* m_manipulator;
};

}
