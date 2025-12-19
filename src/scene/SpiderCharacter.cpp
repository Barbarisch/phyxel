#include "scene/SpiderCharacter.h"
#include "graphics/RenderCoordinator.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace VulkanCube {
namespace Scene {

SpiderCharacter::SpiderCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& startPos)
    : RagdollCharacter(physicsWorld, startPos), moveSpeed(5.0f) {
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
    
    // Initialize Procedural System
    gaitController = std::make_unique<GaitController>(this);
    gaitController->setStepHeight(0.3f);
    gaitController->setStepDuration(0.2f);
    gaitController->setStepDistance(0.4f);

    for (int i = 0; i < 4; ++i) {
        // Create both Left (side=-1) and Right (side=1) legs
        for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
            float side = (sideIdx == 0) ? -1.0f : 1.0f;
            int legIdx = (sideIdx == 0) ? i : (i + 4);
            
            float z = zOffsets[i] * bodyScale.z * 2.0f;
            float yaw = (sideIdx == 0) ? yawAngles[i] : -yawAngles[i];

            glm::vec3 rootPos = startPos + glm::vec3(side * xOffset, 0, z);
            glm::vec3 localAttachment = glm::vec3(side * xOffset, 0, z); // Relative to body center (startPos)
            
            // Create Limb Object
            auto limb = std::make_unique<Limb>(this, bodyIndex, localAttachment);

            // --- Femur (Up and Out) ---
            glm::vec3 femurDir = glm::normalize(glm::vec3(side * cos(yaw), 0.8f, sin(yaw))); 
            glm::vec3 femurPos = rootPos + femurDir * (femurScale.z * 0.5f);
            
            btRigidBody* femur = physicsWorld->createCube(femurPos, femurScale, 1.0f);
            
            // Rotate Femur to align with direction
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
                glm::vec3 hingeAxis(0, 1, 0); // World Y
                glm::vec3 zeroAngleDir = glm::normalize(glm::vec3(side * cos(yaw), 0, sin(yaw)));
                glm::vec3 orthoDir = glm::cross(hingeAxis, zeroAngleDir);
                
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
                
                btTransform bodyTrans;
                body->getMotionState()->getWorldTransform(bodyTrans);
                btTransform localA = bodyTrans.inverse() * hipFrameWorld;
                btTransform localB = femurTrans.inverse() * hipFrameWorld;

                btHingeConstraint* hip = new btHingeConstraint(*body, *femur, localA, localB);
                hip->setLimit(-0.5f, 0.5f); // Swing limits
                physicsWorld->addConstraint(hip, true);
                constraints.push_back(hip);
                legs[legIdx].hipJoint = hip;
                
                limb->addSegment(legs[legIdx].femur, hip, femurScale.z);
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
                glm::vec3 jointPos = femurPos + femurDir * (femurScale.z * 0.5f);
                glm::vec3 legPlaneNormal = glm::normalize(glm::cross(glm::vec3(0,1,0), glm::vec3(side * cos(yaw), 0, sin(yaw))));
                
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
                knee->setLimit(0.0f, 2.5f); 
                physicsWorld->addConstraint(knee, true);
                constraints.push_back(knee);
                legs[legIdx].kneeJoint = knee;
                
                limb->addSegment(legs[legIdx].tibia, knee, tibiaScale.z);
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
            tarsus->setFriction(4.0f); 
            parts.push_back({tarsus, tarsusScale, glm::vec4(0.05f, 0.05f, 0.05f, 1.0f), "Tarsus" + std::to_string(legIdx)});
            legs[legIdx].tarsus = (int)parts.size() - 1;

            // Joint Tibia-Tarsus (Ankle)
            {
                glm::vec3 jointPos = tibiaPos + tibiaDir * (tibiaScale.z * 0.5f);
                glm::vec3 legPlaneNormal = glm::normalize(glm::cross(glm::vec3(0,1,0), glm::vec3(side * cos(yaw), 0, sin(yaw))));
                
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
                
                limb->addSegment(legs[legIdx].tarsus, ankle, tarsusScale.z);
            }
            
            // Set Home Position (Current World Pos relative to Body)
            // We want the home position to be the current position of the tip (Tarsus end)
            // Tarsus end is tarsusPos + tarsusDir * (tarsusScale.z * 0.5f)
            glm::vec3 tipPos = tarsusPos + tarsusDir * (tarsusScale.z * 0.5f);
            
            // Transform to Body Local Space
            // Body is at startPos with Identity rotation
            glm::vec3 localHome = tipPos - startPos;
            limb->setHomePosition(localHome);
            
            // Add to GaitController
            gaitController->addLimb(limb.get());
            
            // Store Limb
            limbs.push_back(std::move(limb));
        }
    }
}

void SpiderCharacter::update(float deltaTime) {
    // Smooth control inputs
    float smoothFactor = 5.0f * deltaTime;
    currentForward = currentForward + (targetForward - currentForward) * smoothFactor;
    currentTurn = currentTurn + (targetTurn - currentTurn) * smoothFactor;

    // Calculate desired velocity
    // Forward is along Body Z (or -Z?)
    // In createSpiderRagdoll, legs are offset along Z.
    // Let's assume Body Forward is +Z or -Z.
    // Usually -Z is forward in OpenGL/Vulkan, but let's check the body creation.
    // Body is a box.
    
    if (bodyIndex != -1 && gaitController) {
        btRigidBody* body = parts[bodyIndex].rigidBody;
        btTransform trans;
        body->getMotionState()->getWorldTransform(trans);
        
        // Get forward vector from body rotation
        btQuaternion rot = trans.getRotation();
        glm::quat q(rot.w(), rot.x(), rot.y(), rot.z());
        glm::vec3 forward = q * glm::vec3(0, 0, 1); // Assuming Z is forward
        
        // Apply turn torque
        if (std::abs(currentTurn) > 0.01f) {
            body->activate();
            body->applyTorque(btVector3(0, currentTurn * -50.0f, 0));
        }
        
        // Desired velocity for Gait Controller
        glm::vec3 velocity = forward * currentForward * moveSpeed;
        
        // Update Gait Controller
        gaitController->update(deltaTime, velocity);
        
        // Apply force to body to actually move it (IK just moves legs)
        // The legs should pull the body, but with simple IK we might need to cheat a bit
        // or rely on friction.
        // If we just move legs, the body might not move if friction is high and legs slide.
        // But if legs are planted (high friction) and we move the body, the legs will drag.
        // Wait, the GaitController moves the FEET.
        // If the feet move, and they have friction, they push the body?
        // No, IK moves the feet relative to the body?
        // No, IK sets the target for the feet in WORLD space.
        // If the body moves, the feet stay planted (target doesn't change).
        // So we need to apply force to the body to move it towards the desired direction.
        // The legs will then "step" to keep up.
        
        if (std::abs(currentForward) > 0.01f) {
            body->activate();
            // Apply force in forward direction
            // We need enough force to overcome friction/inertia
            btVector3 force(velocity.x * 10.0f, 0, velocity.z * 10.0f);
            body->applyCentralForce(force);
        }
    }
    
    // Keep upright (PID controller for orientation)
    if (bodyIndex != -1) {
        btRigidBody* body = parts[bodyIndex].rigidBody;
        btTransform trans;
        body->getMotionState()->getWorldTransform(trans);
        
        btVector3 up(0, 1, 0);
        btVector3 currentUp = trans.getBasis().getColumn(1); // Y axis
        btVector3 axis = currentUp.cross(up);
        float angle = currentUp.angle(up);
        
        if (angle > 0.05f) {
            float torqueStrength = 200.0f;
            body->activate();
            body->applyTorque(axis * angle * torqueStrength);
        }
    }
}

} // namespace Scene
} // namespace VulkanCube
