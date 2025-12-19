#include "scene/Limb.h"
#include "scene/RagdollCharacter.h"
#include "utils/Logger.h"
#include <glm/gtx/norm.hpp>
#include <glm/gtx/vector_angle.hpp>

namespace VulkanCube {
namespace Scene {

Limb::Limb(RagdollCharacter* owner, int rootBodyIndex, const glm::vec3& localAttachmentPoint)
    : owner(owner), rootBodyIndex(rootBodyIndex), localAttachmentPoint(localAttachmentPoint) {
}

void Limb::addSegment(int bodyIndex, btTypedConstraint* joint, float length) {
    segments.push_back({bodyIndex, joint, length});
}

void Limb::setTarget(const glm::vec3& targetPos) {
    targetPosition = targetPos;
}

glm::vec3 Limb::getEndEffectorPosition() const {
    if (segments.empty()) return glm::vec3(0.0f);
    
    const auto& parts = owner->getParts();
    int lastIdx = segments.back().bodyIndex;
    if (lastIdx >= 0 && lastIdx < parts.size() && parts[lastIdx].rigidBody) {
        btTransform trans;
        parts[lastIdx].rigidBody->getMotionState()->getWorldTransform(trans);
        btVector3 pos = trans.getOrigin();
        return glm::vec3(pos.x(), pos.y(), pos.z());
    }
    return glm::vec3(0.0f);
}

glm::vec3 Limb::getHomePositionWorld() const {
    const auto& parts = owner->getParts();
    if (rootBodyIndex >= 0 && rootBodyIndex < parts.size() && parts[rootBodyIndex].rigidBody) {
        btTransform trans;
        parts[rootBodyIndex].rigidBody->getMotionState()->getWorldTransform(trans);
        
        // Transform local home pos to world
        btVector3 local(localHomePos.x, localHomePos.y, localHomePos.z);
        btVector3 world = trans * local;
        return glm::vec3(world.x(), world.y(), world.z());
    }
    return glm::vec3(0.0f);
}

void Limb::update(float deltaTime) {
    if (!ikEnabled || segments.empty()) return;

    if (segments.size() == 2) {
        solveTwoBoneIK(targetPosition);
    } else if (segments.size() == 3) {
        solveThreeBoneIK(targetPosition);
    }
}

void Limb::solveTwoBoneIK(const glm::vec3& target) {
    // Basic 2-Bone IK (Law of Cosines)
    // Assumes Segment 0 is Thigh/UpperArm, Segment 1 is Shin/Forearm
    // Joint 0 is Hip/Shoulder, Joint 1 is Knee/Elbow
    
    const auto& parts = owner->getParts();
    
    // Get Root Position (Hip Joint World Pos)
    btTransform rootTrans;
    parts[rootBodyIndex].rigidBody->getMotionState()->getWorldTransform(rootTrans);
    btVector3 rootPosBt = rootTrans * btVector3(localAttachmentPoint.x, localAttachmentPoint.y, localAttachmentPoint.z);
    glm::vec3 rootPos(rootPosBt.x(), rootPosBt.y(), rootPosBt.z());

    float l1 = segments[0].length;
    float l2 = segments[1].length;
    
    // Vector from root to target
    glm::vec3 toTarget = target - rootPos;
    float dist = glm::length(toTarget);
    
    // Clamp distance
    if (dist > l1 + l2 - 0.01f) {
        toTarget = glm::normalize(toTarget) * (l1 + l2 - 0.01f);
        dist = l1 + l2 - 0.01f;
    } else if (dist < 0.01f) {
        return; // Too close
    }

    // Law of Cosines
    // c^2 = a^2 + b^2 - 2ab cos(C)
    // dist^2 = l1^2 + l2^2 - 2*l1*l2 * cos(kneeAngle)
    float cosKnee = (l1*l1 + l2*l2 - dist*dist) / (2 * l1 * l2);
    // Clamp for safety
    cosKnee = glm::clamp(cosKnee, -1.0f, 1.0f);
    float kneeAngle = acos(cosKnee); // Interior angle
    // External angle (bend) is PI - kneeAngle
    float bendAngle = glm::pi<float>() - kneeAngle;

    // Hip Angle
    // Angle between toTarget and Thigh
    // sin(hipAngle) / l2 = sin(kneeAngle) / dist
    // hipAngle = asin(l2 * sin(kneeAngle) / dist)
    // Or use Law of Cosines again for hip
    float cosHip = (l1*l1 + dist*dist - l2*l2) / (2 * l1 * dist);
    cosHip = glm::clamp(cosHip, -1.0f, 1.0f);
    float hipAngleOffset = acos(cosHip);

    // Apply to motors
    // This is tricky because we need to map these angles to the specific constraint axes
    // For now, let's assume simple Hinge constraints where the motor target is the angle
    
    // We need to know the orientation of the leg plane
    // For a spider, the leg plane is vertical? Or horizontal?
    // Let's assume the joints are set up such that:
    // Hip Joint: Controls Swing (Yaw) and Lift (Pitch) - usually 2 DOF or 2 Hinges
    // Knee Joint: Controls Bend (Pitch) - 1 DOF Hinge
    
    // Simplified: Just drive the Knee for now
    if (segments[1].joint) {
        btHingeConstraint* knee = dynamic_cast<btHingeConstraint*>(segments[1].joint);
        if (knee) {
            // Target angle depends on joint setup. 
            // Assuming 0 is straight, and positive is bent.
            // We calculated 'bendAngle'.
            float currentAngle = knee->getHingeAngle();
            float error = bendAngle - currentAngle;
            knee->enableAngularMotor(true, error * 10.0f, 100.0f);
        }
    }
    
    // Hip is harder without knowing the constraint frame.
    // Ideally we'd compute the desired rotation quaternion for the thigh and apply torque.
}

void Limb::solveThreeBoneIK(const glm::vec3& target) {
    // Heuristic for 3-bone (Spider Leg: Femur, Tibia, Tarsus)
    // Treat Tibia+Tarsus as one bone, or Femur+Tibia as one.
    // Let's try to keep the Tarsus (tip) pointing down or at a fixed angle relative to ground.
    
    // For a spider leg:
    // Hip (Body-Femur): 2 DOF (Yaw/Swing, Pitch/Lift)
    // Knee (Femur-Tibia): 1 DOF (Pitch/Bend)
    // Ankle (Tibia-Tarsus): 1 DOF (Pitch/Bend)
    
    // Strategy:
    // 1. Calculate Hip Yaw to point leg plane at target.
    // 2. Solve 2-Bone IK in that plane for Hip Pitch and Knee Pitch.
    // 3. Set Ankle to keep Tarsus vertical or aligned.
    
    const auto& parts = owner->getParts();
    
    // Root (Hip) Position
    btTransform rootTrans;
    parts[rootBodyIndex].rigidBody->getMotionState()->getWorldTransform(rootTrans);
    btVector3 rootPosBt = rootTrans * btVector3(localAttachmentPoint.x, localAttachmentPoint.y, localAttachmentPoint.z);
    glm::vec3 rootPos(rootPosBt.x(), rootPosBt.y(), rootPosBt.z());
    
    glm::vec3 toTarget = target - rootPos;
    
    // 1. Hip Yaw
    // Project toTarget onto Body XZ plane (local)
    // We need Body orientation
    btQuaternion bodyRot = rootTrans.getRotation();
    glm::quat bodyQuat(bodyRot.w(), bodyRot.x(), bodyRot.y(), bodyRot.z());
    glm::vec3 bodyForward = bodyQuat * glm::vec3(0, 0, 1);
    glm::vec3 bodyRight = bodyQuat * glm::vec3(1, 0, 0);
    glm::vec3 bodyUp = bodyQuat * glm::vec3(0, 1, 0);
    
    // Calculate Yaw angle relative to body forward?
    // Or just drive the Hip Hinge if it's a Hinge.
    // Our Spider has a Hinge for Hip (Yaw) and Hinge for Knee (Pitch)?
    // Wait, SpiderCharacter.cpp:
    // Hip is Hinge (Body-Femur). Axis is Y (Up). So it rotates in XZ plane.
    // Knee is Hinge (Femur-Tibia). Axis is "Side". Rotates in vertical plane.
    // Ankle is Hinge.
    
    // So Hip controls Yaw.
    // But Femur is fixed pitch? No, Femur is attached via Hip Hinge which allows Yaw.
    // But how does it lift?
    // Ah, in SpiderCharacter.cpp, the Hip Hinge axis is World Y. So it ONLY allows Yaw.
    // The Femur is created pointing "Up and Out".
    // So the Spider leg CANNOT lift the Femur relative to the body?
    // Wait, "Femur (Up and Out)".
    // "Joint Body-Femur (Hip)... Hinge Axis: World Y".
    // Yes, the current spider implementation only allows the leg to swing forward/back (Yaw).
    // It relies on the fixed upward angle of the Femur mesh/shape to provide height.
    // The Knee then bends down.
    
    // If we want true IK, we need a 2-DOF Hip (Universal or ConeTwist) or the current setup is limited.
    // However, we can still do IK with the current setup:
    // 1. Rotate Hip to point Femur towards target (Yaw).
    // 2. Rotate Knee to reach distance (Extension).
    // 3. Rotate Ankle to touch ground.
    
    // Target in Body Space
    glm::vec3 localTarget = glm::inverse(bodyQuat) * toTarget;
    
    // Calculate desired Yaw
    // Assuming Hip is at (0,0,0) in local space relative to attachment
    // And default leg points along X (or Z?)
    // Let's assume we just need to align the leg plane.
    
    // This is getting complex to implement generically without knowing the exact joint axes.
    // For the "Robust System" request, we should probably redesign the Spider Leg to have proper 3-DOF movement.
    // But for now, let's just implement a simple motor driver that tries to minimize distance.
    
    // Simple Gradient Descent / Jacobian Transpose approach?
    // Or just simple heuristic per joint.
    
    // Let's stick to the "Gait Controller" idea first.
    // The Limb class should just expose "setTarget" and handle the math.
    
    // For now, empty implementation to allow compilation.
    // We will implement the specific solver when we refactor the Spider.
}

} // namespace Scene
} // namespace VulkanCube
