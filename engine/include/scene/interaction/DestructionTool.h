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

private:
    VoxelManipulationSystem* m_manipulator;
};

}
