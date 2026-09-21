#pragma once

#include "graphics/Animation.h"
#include "scene/motion/MotionTypes.h"

#include <string>
#include <vector>

namespace Phyxel::Scene::Motion {

struct RetargetJoint {
    std::string sourceJoint;
    std::string targetBone;
    glm::quat sourceBindRotation{1.0f, 0.0f, 0.0f, 0.0f};
    /// Optional ordered hinge chain collapsed onto one target ball joint.
    /// Used by G1 hips/shoulders/wrists; sourceJoint remains the simple case.
    std::vector<std::string> sourceChain;
};

/// Explicit G1-34 to Mixamo humanoid mapping. Fingers and head are deliberately
/// absent and therefore preserve the authored base pose.
std::vector<RetargetJoint> g1ToMixamoRetargetMap();

class HumanoidRetargeter {
public:
    HumanoidRetargeter(const Skeleton& targetSkeleton,
                       std::vector<RetargetJoint> joints);

    bool valid() const { return m_valid; }
    const std::string& error() const { return m_error; }

    /// Starts from baseTargetPose so unmapped target bones are preserved.
    bool retarget(const LocalPoseFrame& source,
                  const std::vector<glm::quat>& baseTargetPose,
                  std::vector<glm::quat>& targetPose) const;

private:
    struct ResolvedJoint {
        std::vector<std::string> sourceJoints;
        int targetBoneId = -1;
        glm::quat sourceBindRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::quat targetBindRotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    std::size_t m_targetBoneCount = 0;
    std::vector<ResolvedJoint> m_joints;
    bool m_valid = false;
    std::string m_error;
};

bool isFiniteQuaternion(const glm::quat& rotation);
bool normalizeAndMatchHemisphere(glm::quat& rotation, const glm::quat& previous);

} // namespace Phyxel::Scene::Motion
