#include "scene/motion/HumanoidRetargeter.h"

#include <cmath>
#include <initializer_list>
#include <unordered_map>
#include <utility>

namespace Phyxel::Scene::Motion {

bool isFiniteQuaternion(const glm::quat& q) {
    return std::isfinite(q.w) && std::isfinite(q.x) &&
           std::isfinite(q.y) && std::isfinite(q.z) &&
           std::isfinite(glm::dot(q, q));
}

bool normalizeAndMatchHemisphere(glm::quat& rotation, const glm::quat& previous) {
    const float length2 = glm::dot(rotation, rotation);
    if (!isFiniteQuaternion(rotation) || !std::isfinite(length2) || length2 < 1.0e-12f ||
        !isFiniteQuaternion(previous)) {
        return false;
    }
    rotation *= 1.0f / std::sqrt(length2);
    if (glm::dot(rotation, previous) < 0.0f) rotation = -rotation;
    return true;
}

HumanoidRetargeter::HumanoidRetargeter(const Skeleton& targetSkeleton,
                                       std::vector<RetargetJoint> joints)
    : m_targetBoneCount(targetSkeleton.bones.size()) {
    if (targetSkeleton.bones.empty() || joints.empty()) {
        m_error = "retargeter requires a target skeleton and joint map";
        return;
    }

    for (const RetargetJoint& joint : joints) {
        std::vector<std::string> sourceJoints = joint.sourceChain;
        if (sourceJoints.empty() && !joint.sourceJoint.empty())
            sourceJoints.push_back(joint.sourceJoint);
        const auto targetIt = targetSkeleton.boneMap.find(joint.targetBone);
        if (sourceJoints.empty() || targetIt == targetSkeleton.boneMap.end() ||
            !isFiniteQuaternion(joint.sourceBindRotation) ||
            glm::dot(joint.sourceBindRotation, joint.sourceBindRotation) < 1.0e-12f) {
            m_error = "unresolvable or invalid retarget joint: " + joint.sourceJoint +
                      " -> " + joint.targetBone;
            m_joints.clear();
            return;
        }
        const Bone& target = targetSkeleton.bones[static_cast<std::size_t>(targetIt->second)];
        m_joints.push_back({std::move(sourceJoints), targetIt->second,
                            glm::normalize(joint.sourceBindRotation),
                            glm::normalize(target.localRotation)});
    }
    m_valid = true;
}

bool HumanoidRetargeter::retarget(const LocalPoseFrame& source,
                                  const std::vector<glm::quat>& baseTargetPose,
                                  std::vector<glm::quat>& targetPose) const {
    if (!m_valid || !source.structurallyValid() ||
        baseTargetPose.size() != m_targetBoneCount) {
        return false;
    }

    std::unordered_map<std::string, std::size_t> sourceIds;
    sourceIds.reserve(source.jointNames.size());
    for (std::size_t i = 0; i < source.jointNames.size(); ++i) {
        if (!sourceIds.emplace(source.jointNames[i], i).second) return false;
    }

    targetPose = baseTargetPose;
    for (const ResolvedJoint& joint : m_joints) {
        glm::quat sourceRotation(1.0f, 0.0f, 0.0f, 0.0f);
        for (const std::string& sourceJoint : joint.sourceJoints) {
            const auto sourceIt = sourceIds.find(sourceJoint);
            if (sourceIt == sourceIds.end()) return false;
            sourceRotation *= source.localRotations[sourceIt->second];
        }
        if (!normalizeAndMatchHemisphere(sourceRotation, joint.sourceBindRotation)) return false;

        // Transfer the source's bind-relative local delta onto the target bind.
        const glm::quat sourceDelta = glm::inverse(joint.sourceBindRotation) * sourceRotation;
        glm::quat result = joint.targetBindRotation * sourceDelta;
        const std::size_t targetId = static_cast<std::size_t>(joint.targetBoneId);
        if (!normalizeAndMatchHemisphere(result, baseTargetPose[targetId])) return false;
        targetPose[targetId] = result;
    }
    return true;
}

std::vector<RetargetJoint> g1ToMixamoRetargetMap() {
    auto chain = [](std::initializer_list<const char*> sources, const char* target) {
        RetargetJoint result;
        result.targetBone = target;
        for (const char* source : sources) result.sourceChain.emplace_back(source);
        return result;
    };
    return {
        chain({"pelvis_skel"}, "mixamorig:Hips"),
        chain({"waist_yaw_skel", "waist_roll_skel", "waist_pitch_skel"}, "mixamorig:Spine"),
        chain({"left_hip_pitch_skel", "left_hip_roll_skel", "left_hip_yaw_skel"}, "mixamorig:LeftUpLeg"),
        chain({"left_knee_skel"}, "mixamorig:LeftLeg"),
        chain({"left_ankle_pitch_skel", "left_ankle_roll_skel"}, "mixamorig:LeftFoot"),
        chain({"left_toe_base"}, "mixamorig:LeftToeBase"),
        chain({"right_hip_pitch_skel", "right_hip_roll_skel", "right_hip_yaw_skel"}, "mixamorig:RightUpLeg"),
        chain({"right_knee_skel"}, "mixamorig:RightLeg"),
        chain({"right_ankle_pitch_skel", "right_ankle_roll_skel"}, "mixamorig:RightFoot"),
        chain({"right_toe_base"}, "mixamorig:RightToeBase"),
        chain({"left_shoulder_pitch_skel", "left_shoulder_roll_skel", "left_shoulder_yaw_skel"}, "mixamorig:LeftArm"),
        chain({"left_elbow_skel"}, "mixamorig:LeftForeArm"),
        chain({"left_wrist_roll_skel", "left_wrist_pitch_skel", "left_wrist_yaw_skel", "left_hand_roll_skel"}, "mixamorig:LeftHand"),
        chain({"right_shoulder_pitch_skel", "right_shoulder_roll_skel", "right_shoulder_yaw_skel"}, "mixamorig:RightArm"),
        chain({"right_elbow_skel"}, "mixamorig:RightForeArm"),
        chain({"right_wrist_roll_skel", "right_wrist_pitch_skel", "right_wrist_yaw_skel", "right_hand_roll_skel"}, "mixamorig:RightHand")
    };
}

} // namespace Phyxel::Scene::Motion
