#pragma once

#include "InteractionContext.h"
#include <memory>

namespace VulkanCube {

class VoxelManipulationSystem;

class PlacementTool {
public:
    PlacementTool(VoxelManipulationSystem* manipulator);
    ~PlacementTool() = default;

    bool placeVoxel(const InteractionContext& context);
    bool placeSubcube(const InteractionContext& context);
    bool placeMicrocube(const InteractionContext& context);

private:
    VoxelManipulationSystem* m_manipulator;
};

}
