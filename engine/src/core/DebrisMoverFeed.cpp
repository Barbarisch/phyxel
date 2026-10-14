#include "core/DebrisMoverFeed.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Phyxel {
namespace DebrisMoverFeed {

namespace {
bool finite3(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}  // namespace

BodyFeedResult appendRigidBodies(const Physics::VoxelDynamicsWorld& world, const glm::vec3& eye,
                                 uint32_t budget, std::vector<GpuParticlePhysics::MoverBox>& out) {
    BodyFeedResult r;
    std::vector<std::pair<float, const Physics::VoxelRigidBody*>> order;
    order.reserve(world.getBodyCount());
    for (size_t i = 0; i < world.getBodyCount(); ++i) {
        const auto* b = world.getBodyByIndex(i);
        if (!b || b->isDead || b->getLocalBoxes().empty()) continue;
        if (!finite3(b->position) || !finite3(b->linearVelocity) || !finite3(b->angularVelocity)) {
            ++r.bodiesSkipped;   // a NaN body must not poison the solver
            continue;
        }
        const glm::vec3 d = b->position - eye;
        order.push_back({glm::dot(d, d), b});
    }
    std::sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return x.first < y.first; });

    uint32_t used = 0;
    for (const auto& [d2, b] : order) {
        const auto n = static_cast<uint32_t>(b->getLocalBoxes().size());
        if (used + n > budget) { ++r.bodiesSkipped; continue; }   // whole or nothing; a smaller one may still fit
        const glm::quat q = glm::normalize(b->orientation);
        for (size_t k = 0; k < n; ++k) {
            const auto wb = b->getWorldBox(k);
            GpuParticlePhysics::MoverBox m;
            m.center      = wb.center;
            m.halfExtents = wb.halfExtents;
            m.rotation    = q;
            // A sleeper is a support: its residual drift under the sleep threshold must not shove.
            m.velocity    = b->isAsleep ? glm::vec3(0.0f)
                                        : b->linearVelocity + glm::cross(b->angularVelocity, wb.center - b->position);
            out.push_back(m);
        }
        used += n;
        ++r.bodiesFed;
        r.boxesFed += n;
    }
    return r;
}

}  // namespace DebrisMoverFeed
}  // namespace Phyxel
