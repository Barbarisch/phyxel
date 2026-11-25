#include "scene/VoxelManipulationSystem.h"

namespace Phyxel {

void VoxelManipulationSystem::setCallbacks(
    GetChunkManagerFunc chunkManagerFunc,
    GetPhysicsWorldFunc physicsWorldFunc) {

    getChunkManager = chunkManagerFunc;
    getPhysicsWorld = physicsWorldFunc;
}

// (Today's cube break / place on the Chunk store went with D4: the switched world's break and place are the
// world editor's brushes, VoxelInteractionSystem::editorAction.)

} // namespace Phyxel
