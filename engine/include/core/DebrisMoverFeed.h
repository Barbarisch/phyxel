#pragma once

#include "core/GpuParticlePhysics.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Phyxel {
namespace Physics { class VoxelDynamicsWorld; }

// DebrisInteractionPlan Phase 3c: CPU rigid bodies (furniture incl. grabbed/thrown, fragments,
// felled trees, item props) as GPU debris movers. One-way: debris rests on and is pushed by them;
// they do not feel the debris (that coupling is Phase 4).
namespace DebrisMoverFeed {

struct BodyFeedResult {
    uint32_t bodiesFed     = 0;
    uint32_t boxesFed      = 0;
    uint32_t bodiesSkipped = 0;   // whole bodies past the budget (never fed partially)
};

// Appends every live body's compound boxes (world centre, local half extents, body orientation,
// point velocity v + w x r at the box centre), nearest `eye` first. Sleeping bodies are included
// with zero velocity: debris rests on them. A body is fed WHOLE or not at all - a body cut at the
// budget would let debris fall through its missing boxes - so `budget` is in boxes and a body that
// does not fit is skipped and counted. Non-finite poses are skipped (counted).
BodyFeedResult appendRigidBodies(const Physics::VoxelDynamicsWorld& world, const glm::vec3& eye,
                                 uint32_t budget, std::vector<GpuParticlePhysics::MoverBox>& out);

}  // namespace DebrisMoverFeed
}  // namespace Phyxel
