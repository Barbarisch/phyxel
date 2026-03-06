#include "scene/SpiderCharacter.h"
#include "graphics/RenderCoordinator.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Scene {

SpiderCharacter::SpiderCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos)
    : RagdollCharacter(physicsWorld, startPos), walkTime(0.0f), moveSpeed(5.0f) {
    createSpiderRagdoll(startPos);
}

SpiderCharacter::~SpiderCharacter() {
    // Base class handles cleanup of parts and constraints
}

void SpiderCharacter::createSpiderRagdoll(const glm::vec3& startPos) {
    // Scales
    float scale = 1.0f; // Global scale factor
    glm::vec3 bodyScale(0.4f * scale, 0.3f * scale, 0.5f * scale);
    glm::vec3 abdomenScale(0.6f * scale, 0.5f * scale, 0.7f * scale);
    
    // Leg segments: Femur (Thigh), Tibia (Shin), Tarsus (Foot)
    glm::vec3 femurScale(0.1f * scale, 0.1f * scale, 0.4f * scale); 
    glm::vec3 tibiaScale(0.08f * scale, 0.08f * scale, 0.5f * scale); 
    glm::vec3 tarsusScale(0.06f * scale, 0.06f * scale, 0.4f * scale);

    // 1. Create Cephalothorax (Body)
    btRigidBody* body = physicsWorld->createCube(startPos, bodyScale, 10.0f);
    body->setUserPointer((void*)2); // Mark as enemy/spider
    body->setDamping(0.5f, 0.5f);
    parts.push_back({body, bodyScale, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), "SpiderBody"});
    bodyIndex = 0;

    // 2. Create Abdomen (Rear)
    glm::vec3 abdomenPos = startPos + glm::vec3(0, 0.1f, -bodyScale.z * 0.5f - abdomenScale.z * 0.5f - 0.05f);
    btRigidBody* abdomen = physicsWorld->createCube(abdomenPos, abdomenScale, 5.0f);
    abdomen->setUserPointer((void*)2);
    parts.push_back({abdomen, abdomenScale, glm::vec4(0.3f, 0.1f, 0.1f, 1.0f), "SpiderAbdomen"});
    abdomenIndex = 1;

    // Connect Abdomen to Body
    {
        btTransform localA, localB;
        localA.setIdentity(); localB.setIdentity();
        localA.setOrigin(btVector3(0, 0.1f, -bodyScale.z * 0.5f));
        localB.setOrigin(btVector3(0, 0, abdomenScale.z * 0.5f));
        
        btConeTwistConstraint* joint = new btConeTwistConstraint(*body, *abdomen, localA, localB);
        joint->setLimit(0.2f, 0.2f, 0.1f);
        physicsWorld->addConstraint(joint, true);
        constraints.push_back(joint);
    }

    // 3. Create Legs (4 pairs)
    float zOffsets[] = { 0.4f, 0.15f, -0.15f, -0.4f };
    float xOffset = bodyScale.x * 0.5f;
    float yawAngles[] = { 0.8f, 0.4f, -0.4f, -0.8f }; // Radians

    legs.resize(8);

    for (int i = 0; i < 4; ++i) {
        // Create both Left (side=-1) and Right (side=1) legs
        for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
            float side = (sideIdx == 0) ? -1.0f : 1.0f;
            int legIdx = (sideIdx == 0) ? i : (i + 4);
            
            float z = zOffsets[i] * bodyScale.z * 2.0f;
            float yaw = (sideIdx == 0) ? yawAngles[i] : -yawAngles[i];

            glm::vec3 rootPos = startPos + glm::vec3(side * xOffset, 0, z);
            
            // --- Femur (Up and Out) ---
            glm::vec3 femurDir = glm::normalize(glm::vec3(side * cos(yaw), 0.8f, sin(yaw))); 
            glm::vec3 femurPos = rootPos + femurDir * (femurScale.z * 0.5f);
            
            btRigidBody* femur = physicsWorld->createCube(femurPos, femurScale, 1.0f);
            
            // Rotate Femur to align with direction
            // Default cube is aligned with Z. We want Z to point to femurDir.
            // Calculate rotation from (0,0,1) to femurDir
            glm::vec3 defaultDir(0, 0, 1);
            glm::quat femurQuat = glm::rotation(defaultDir, femurDir);
            btQuaternion femurRot(femurQuat.x, femurQuat.y, femurQuat.z, femurQuat.w);
            
            btTransform femurTrans;
            femur->getMotionState()->getWorldTransform(femurTrans);
            femurTrans.setRotation(femurRot);
            femur->setWorldTransform(femurTrans);
            femur->getMotionState()->setWorldTransform(femurTrans);

            femur->setUserPointer((void*)2);
            parts.push_back({femur, femurScale, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f), "Femur" + std::to_string(legIdx)});
            legs[legIdx].femur = (int)parts.size() - 1;

            // Joint Body-Femur (Hip)
            {
                // Hip Frame in World Space:
                // Origin: rootPos
                // Z-Axis (Hinge Axis): World Y (0, 1, 0)
                // X-Axis (Zero Angle): Leg Planar Direction (side*cos(yaw), 0, sin(yaw))
                // Y-Axis: Cross(Z, X)
                
                glm::vec3 hingeAxis(0, 1, 0); // World Y
                glm::vec3 zeroAngleDir = glm::normalize(glm::vec3(side * cos(yaw), 0, sin(yaw)));
                glm::vec3 orthoDir = glm::cross(hingeAxis, zeroAngleDir);
                
                // Construct rotation matrix columns
                // Bullet/GLM matrix is column-major
                glm::mat3 hipRotMat;
                hipRotMat[0] = zeroAngleDir; // X
                hipRotMat[1] = orthoDir;     // Y
                hipRotMat[2] = hingeAxis;    // Z
                
                glm::quat hipQuat = glm::quat_cast(hipRotMat);
                btQuaternion hipRot(hipQuat.x, hipQuat.y, hipQuat.z, hipQuat.w);
                
                btTransform hipFrameWorld;
                hipFrameWorld.setIdentity();
                hipFrameWorld.setOrigin(btVector3(rootPos.x, rootPos.y, rootPos.z));
                hipFrameWorld.setRotation(hipRot);
                
                // Calculate local frames
                // Body is at Identity (roughly, assuming startPos is origin for relative calc, but body is created at startPos)
                // Wait, body is created at startPos.
                // localA = Inverse(BodyWorld) * HipWorld
                btTransform bodyTrans;
                body->getMotionState()->getWorldTransform(bodyTrans);
                btTransform localA = bodyTrans.inverse() * hipFrameWorld;
                
                // localB = Inverse(FemurWorld) * HipWorld
                // We just set femurTrans
                btTransform localB = femurTrans.inverse() * hipFrameWorld;

                btHingeConstraint* hip = new btHingeConstraint(*body, *femur, localA, localB);
                hip->setLimit(-0.5f, 0.5f); // Swing limits
                physicsWorld->addConstraint(hip, true);
                constraints.push_back(hip);
                legs[legIdx].hipJoint = hip;
            }

            // --- Tibia (Down and Out) ---
            glm::vec3 tibiaDir = glm::normalize(glm::vec3(side * cos(yaw) * 1.5f, -0.5f, sin(yaw) * 1.5f));
            glm::vec3 tibiaPos = femurPos + femurDir * (femurScale.z * 0.5f) + tibiaDir * (tibiaScale.z * 0.5f);

            btRigidBody* tibia = physicsWorld->createCube(tibiaPos, tibiaScale, 1.0f);
            
            // Rotate Tibia
            glm::quat tibiaQuat = glm::rotation(defaultDir, tibiaDir);
            btQuaternion tibiaRot(tibiaQuat.x, tibiaQuat.y, tibiaQuat.z, tibiaQuat.w);
            
            btTransform tibiaTrans;
            tibia->getMotionState()->getWorldTransform(tibiaTrans);
            tibiaTrans.setRotation(tibiaRot);
            tibia->setWorldTransform(tibiaTrans);
            tibia->getMotionState()->setWorldTransform(tibiaTrans);

            tibia->setUserPointer((void*)2);
            parts.push_back({tibia, tibiaScale, glm::vec4(0.08f, 0.08f, 0.08f, 1.0f), "Tibia" + std::to_string(legIdx)});
            legs[legIdx].tibia = (int)parts.size() - 1;

            // Joint Femur-Tibia (Knee)
            {
                // Knee Frame in World Space
                // Origin: Joint position (End of Femur)
                // Z-Axis (Hinge Axis): Perpendicular to Leg Plane?
                // Leg Plane is defined by FemurDir and TibiaDir? Or just Side Axis?
                // Let's use the local X axis of the Femur (which should be "Side") as the Hinge Axis.
                // Femur X axis = femurRot * (1,0,0).
                
                glm::vec3 jointPos = femurPos + femurDir * (femurScale.z * 0.5f);
                
                // Hinge Axis: Cross(FemurDir, Up)? Or just the "Side" vector we used for Hip X?
                // Actually, for a simple spider leg, the Knee axis is horizontal, perpendicular to the leg plane.
                // Leg Plane Normal = Cross(Up, LegDir).
                // So Hinge Axis = Leg Plane Normal.
                glm::vec3 legPlaneNormal = glm::normalize(glm::cross(glm::vec3(0,1,0), glm::vec3(side * cos(yaw), 0, sin(yaw))));
                
                // Zero Angle: Aligned with Femur Direction?
                // If Angle 0 means straight leg.
                // X-Axis (Zero): FemurDir.
                // Y-Axis: Cross(Z, X).
                
                glm::vec3 hingeAxis = legPlaneNormal;
                glm::vec3 zeroAngleDir = femurDir;
                glm::vec3 orthoDir = glm::cross(hingeAxis, zeroAngleDir);
                
                glm::mat3 kneeRotMat;
                kneeRotMat[0] = zeroAngleDir;
                kneeRotMat[1] = orthoDir;
                kneeRotMat[2] = hingeAxis;
                
                glm::quat kneeQuat = glm::quat_cast(kneeRotMat);
                btQuaternion kneeRot(kneeQuat.x, kneeQuat.y, kneeQuat.z, kneeQuat.w);
                
                btTransform kneeFrameWorld;
                kneeFrameWorld.setIdentity();
                kneeFrameWorld.setOrigin(btVector3(jointPos.x, jointPos.y, jointPos.z));
                kneeFrameWorld.setRotation(kneeRot);
                
                btTransform localA = femurTrans.inverse() * kneeFrameWorld;
                btTransform localB = tibiaTrans.inverse() * kneeFrameWorld;

                btHingeConstraint* knee = new btHingeConstraint(*femur, *tibia, localA, localB);
                // Positive angle bends DOWN (Tibia moves towards -Y relative to Femur)
                // We want the leg to be able to bend down significantly
                knee->setLimit(0.0f, 2.5f); 
                physicsWorld->addConstraint(knee, true);
                constraints.push_back(knee);
                legs[legIdx].kneeJoint = knee;
            }

            // --- Tarsus (Down and In - Tip) ---
            glm::vec3 tarsusDir = glm::normalize(glm::vec3(side * cos(yaw) * 0.5f, -1.0f, sin(yaw) * 0.5f));
            glm::vec3 tarsusPos = tibiaPos + tibiaDir * (tibiaScale.z * 0.5f) + tarsusDir * (tarsusScale.z * 0.5f);

            btRigidBody* tarsus = physicsWorld->createCube(tarsusPos, tarsusScale, 1.0f);
            
            // Rotate Tarsus
            glm::quat tarsusQuat = glm::rotation(defaultDir, tarsusDir);
            btQuaternion tarsusRot(tarsusQuat.x, tarsusQuat.y, tarsusQuat.z, tarsusQuat.w);
            
            btTransform tarsusTrans;
            tarsus->getMotionState()->getWorldTransform(tarsusTrans);
            tarsusTrans.setRotation(tarsusRot);
            tarsus->setWorldTransform(tarsusTrans);
            tarsus->getMotionState()->setWorldTransform(tarsusTrans);

            tarsus->setUserPointer((void*)2);
            tarsus->setFriction(4.0f); // Increased friction for grip (was 2.0)
            parts.push_back({tarsus, tarsusScale, glm::vec4(0.05f, 0.05f, 0.05f, 1.0f), "Tarsus" + std::to_string(legIdx)});
            legs[legIdx].tarsus = (int)parts.size() - 1;

            // Joint Tibia-Tarsus (Ankle)
            {
                glm::vec3 jointPos = tibiaPos + tibiaDir * (tibiaScale.z * 0.5f);
                
                // Hinge Axis: Same as Knee (Leg Plane Normal)
                glm::vec3 legPlaneNormal = glm::normalize(glm::cross(glm::vec3(0,1,0), glm::vec3(side * cos(yaw), 0, sin(yaw))));
                
                // Zero Angle: Aligned with Tibia Direction
                glm::vec3 hingeAxis = legPlaneNormal;
                glm::vec3 zeroAngleDir = tibiaDir;
                glm::vec3 orthoDir = glm::cross(hingeAxis, zeroAngleDir);
                
                glm::mat3 ankleRotMat;
                ankleRotMat[0] = zeroAngleDir;
                ankleRotMat[1] = orthoDir;
                ankleRotMat[2] = hingeAxis;
                
                glm::quat ankleQuat = glm::quat_cast(ankleRotMat);
                btQuaternion ankleRot(ankleQuat.x, ankleQuat.y, ankleQuat.z, ankleQuat.w);
                
                btTransform ankleFrameWorld;
                ankleFrameWorld.setIdentity();
                ankleFrameWorld.setOrigin(btVector3(jointPos.x, jointPos.y, jointPos.z));
                ankleFrameWorld.setRotation(ankleRot);
                
                btTransform localA = tibiaTrans.inverse() * ankleFrameWorld;
                btTransform localB = tarsusTrans.inverse() * ankleFrameWorld;

                btHingeConstraint* ankle = new btHingeConstraint(*tibia, *tarsus, localA, localB);
                ankle->setLimit(-1.0f, 1.0f);
                physicsWorld->addConstraint(ankle, true);
                constraints.push_back(ankle);
                legs[legIdx].ankleJoint = ankle;
            }
        }
    }
}

void SpiderCharacter::update(float deltaTime) {
    // Debug printing
    static float debugTimer = 0.0f;
    debugTimer += deltaTime;
    if (debugTimer > 1.0f) {
        debugTimer = 0.0f;
        LOG_INFO_FMT("Spider", "Control: Fwd=" << currentForward << " Turn=" << currentTurn << " WalkTime=" << walkTime);
        if (!legs.empty() && legs[0].hipJoint) {
            btHingeConstraint* hip = static_cast<btHingeConstraint*>(legs[0].hipJoint);
            LOG_INFO_FMT("Spider", "Leg 0 (Left) Hip Angle: " << hip->getHingeAngle());
        }
        if (legs.size() > 4 && legs[4].hipJoint) {
            btHingeConstraint* hip = static_cast<btHingeConstraint*>(legs[4].hipJoint);
            LOG_INFO_FMT("Spider", "Leg 4 (Right) Hip Angle: " << hip->getHingeAngle());
        }
    }

    // Smooth control inputs
    float smoothFactor = 5.0f * deltaTime;
    currentForward = currentForward + (targetForward - currentForward) * smoothFactor;
    currentTurn = currentTurn + (targetTurn - currentTurn) * smoothFactor;

    // Only advance walk cycle if moving
    float speedMagnitude = std::abs(currentForward) + std::abs(currentTurn);
    if (speedMagnitude > 0.01f) {
        walkTime += deltaTime * moveSpeed * speedMagnitude;
    }

    // Procedural Animation (Tripod Gait)
    // Group A: Legs 0, 2, 5, 7
    // Group B: Legs 1, 3, 4, 6
    
    for (int i = 0; i < 8; ++i) {
        bool isGroupA = (i == 0 || i == 2 || i == 5 || i == 7);
        float phaseOffset = isGroupA ? 0.0f : 3.14159f;
        float cycle = walkTime + phaseOffset;
        
        // Determine side (0-3 Left, 4-7 Right)
        bool isLeft = (i < 4);
        float sideMultiplier = isLeft ? 1.0f : -1.0f;

        // Calculate stride amplitude based on turn
        // To turn Left (Positive Turn), Right side must move faster
        float strideScale = 1.0f;
        if (isLeft) {
            strideScale = currentForward - currentTurn;
        } else {
            strideScale = currentForward + currentTurn;
        }

        // Swing (Hip Yaw)
        // Positive Angle = Backward for BOTH sides
        // We want Forward motion (Negative Angle) during Lift (sin goes 1 -> -1)
        // So we want swing to track sin(cycle)
        float swingAmp = 0.5f;
        float swingTarget = sin(cycle) * swingAmp * strideScale;
        
        // Lift (Knee/Ankle Pitch)
        // Scale lift with speed so legs settle when idle
        float liftAmp = 0.8f * std::min(1.0f, speedMagnitude * 2.0f); 
        float liftTarget = std::max(0.0f, -cos(cycle)) * liftAmp; 
        
        // Apply to motors
        // Increased motor strength to support body weight without suspension
        float motorStrength = 500.0f; // Was 50.0f
        float motorSpeed = 10.0f;     // Was 5.0f

        if (legs[i].hipJoint) {
            btHingeConstraint* hip = static_cast<btHingeConstraint*>(legs[i].hipJoint);
            hip->enableAngularMotor(true, (swingTarget - hip->getHingeAngle()) * motorSpeed, motorStrength);
        }
        
        if (legs[i].kneeJoint) {
            btHingeConstraint* knee = static_cast<btHingeConstraint*>(legs[i].kneeJoint);
            // Default bent pose is around 0.8 (Positive = Down). Lift means bending more (more positive)
            float baseAngle = 0.8f;
            float target = baseAngle + liftTarget;
            knee->enableAngularMotor(true, (target - knee->getHingeAngle()) * motorSpeed, motorStrength);
        }
        
        if (legs[i].ankleJoint) {
            btHingeConstraint* ankle = static_cast<btHingeConstraint*>(legs[i].ankleJoint);
            float baseAngle = 0.4f;
            float target = baseAngle - liftTarget * 0.5f; // Extend ankle slightly when lifting
            ankle->enableAngularMotor(true, (target - ankle->getHingeAngle()) * motorSpeed, motorStrength);
        }
    }
    
    // Keep upright (PID controller for orientation) - Keep this to prevent flipping
    if (bodyIndex != -1) {
        btRigidBody* body = parts[bodyIndex].rigidBody;
        btTransform trans;
        body->getMotionState()->getWorldTransform(trans);
        
        btVector3 up(0, 1, 0);
        btVector3 currentUp = trans.getBasis().getColumn(1); // Y axis
        btVector3 axis = currentUp.cross(up);
        float angle = currentUp.angle(up);
        
        if (angle > 0.05f) {
            float torqueStrength = 200.0f; // Increased to help stability
            body->activate();
            body->applyTorque(axis * angle * torqueStrength);
        }
    }
}

} // namespace Scene
} // namespace Phyxel
