#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"
#include <iostream>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace VulkanCube {
namespace Scene {

    AnimatedVoxelCharacter::AnimatedVoxelCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position)
        : RagdollCharacter(physicsWorld, position), worldPosition(position) {
        createController(position);
    }

    AnimatedVoxelCharacter::~AnimatedVoxelCharacter() {
        // Base class handles cleanup of rigid bodies in 'parts'
        boneBodies.clear();
        
        if (controllerBody) {
            physicsWorld->removeCube(controllerBody);
            controllerBody = nullptr;
        }
    }
    
    void AnimatedVoxelCharacter::createController(const glm::vec3& position) {
        // Create a box for the controller (invisible physics representation)
        // Note: createCube takes FULL extents, not half-extents
        // We increase size slightly because dynamic objects are scaled down by 5% in PhysicsWorld
        // Target: 0.8 x 1.8 x 0.8. Request: 0.85 x 1.9 x 0.85
        glm::vec3 size(0.85f, 1.9f, 0.85f); 
        
        // Create dynamic body
        // Center is at +0.9 (half height) so feet are at 'position'
        // We'll let the update() logic handle the exact visual offset
        controllerBody = physicsWorld->createCube(position + glm::vec3(0, 0.9f, 0), size, 60.0f); // 60kg mass
        controllerBody->setUserPointer(this); // Mark as character part to prevent auto-cleanup
        
        // Prevent tipping over (lock rotation on X and Z)
        controllerBody->setAngularFactor(btVector3(0, 1, 0)); // Allow Y rotation? No, we handle rotation manually
        controllerBody->setAngularFactor(btVector3(0, 0, 0));
        
        // Friction
        controllerBody->setFriction(0.0f); // Low friction for movement, we handle stopping manually
        controllerBody->setRestitution(0.0f);
        
        // Disable sleeping
        controllerBody->setActivationState(DISABLE_DEACTIVATION);
    }

    bool AnimatedVoxelCharacter::loadModel(const std::string& animFile) {
        if (animSystem.loadFromFile(animFile, skeleton, clips, voxelModel)) {
            // Configure fixes after loading
            configureAnimationFixes();

            // Automatically construct voxel bones from the loaded model
            if (voxelModel.shapes.empty()) {
                std::cout << "No model shapes found in animation file. Generating default bone shapes." << std::endl;
                
                // Build children map
                std::map<int, std::vector<int>> childrenMap;
                for (const auto& b : skeleton.bones) {
                    if (b.parentId != -1) childrenMap[b.parentId].push_back(b.id);
                }

                for (const auto& bone : skeleton.bones) {
                    // Performance Optimization: Skip small bones (fingers, toes, facial features)
                    // This reduces the number of physics bodies from ~65 to ~15-20
                    std::string nameLower = bone.name;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    
                    if (nameLower.find("thumb") != std::string::npos || 
                        nameLower.find("index") != std::string::npos || 
                        nameLower.find("middle") != std::string::npos || 
                        nameLower.find("ring") != std::string::npos || 
                        nameLower.find("pinky") != std::string::npos || 
                        nameLower.find("eye") != std::string::npos || 
                        nameLower.find("toe") != std::string::npos ||
                        nameLower.find("end") != std::string::npos) {
                        continue;
                    }

                    glm::vec3 targetVector(0.0f);
                    bool hasChild = false;

                    // Find primary child to connect to
                    if (childrenMap[bone.id].size() > 0) {
                        hasChild = true;
                        // If multiple children (e.g. Hips), prefer Spine for the main body block
                        int targetChildId = -1;
                        if (childrenMap[bone.id].size() > 1) {
                            for (int childId : childrenMap[bone.id]) {
                                if (skeleton.bones[childId].name.find("Spine") != std::string::npos) {
                                    targetChildId = childId;
                                    break;
                                }
                            }
                        }
                        
                        if (targetChildId != -1) {
                            targetVector = skeleton.bones[targetChildId].localPosition;
                        } else {
                            // Average of all children
                            for (int childId : childrenMap[bone.id]) {
                                targetVector += skeleton.bones[childId].localPosition;
                            }
                            targetVector /= (float)childrenMap[bone.id].size();
                        }
                    }

                    float len = glm::length(targetVector);
                    if (len < 0.01f) len = 0.1f; // Minimum length for leaf bones or zero-distance children

                    // Determine orientation
                    glm::vec3 size(0.1f); // Default thickness
                    glm::vec3 offset = targetVector * 0.5f;

                    // If leaf node (no children), extend a bit in the direction of parent? 
                    // Or just make a small nub.
                    if (!hasChild) {
                        offset = glm::vec3(0.0f);
                        size = glm::vec3(0.05f); // Small joint nub
                    } else {
                        // Align box to dominant axis
                        glm::vec3 absDir = glm::abs(targetVector);
                        float thickness = len * 0.25f;
                        thickness = glm::clamp(thickness, 0.05f, 0.15f);
                        
                        // Make torso/head thicker
                        if (bone.name.find("Spine") != std::string::npos || bone.name.find("Head") != std::string::npos || bone.name.find("Hips") != std::string::npos) {
                            thickness = glm::clamp(len * 0.6f, 0.15f, 0.3f);
                        }

                        if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
                            size = glm::vec3(len, thickness, thickness);
                        } else if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
                            size = glm::vec3(thickness, len, thickness);
                        } else {
                            size = glm::vec3(thickness, thickness, len);
                        }
                    }
                    
                    glm::vec4 color(0.7f, 0.7f, 0.7f, 1.0f);
                    if (bone.name.find("Head") != std::string::npos) color = glm::vec4(1.0f, 0.8f, 0.6f, 1.0f);
                    else if (bone.name.find("Arm") != std::string::npos || bone.name.find("Hand") != std::string::npos) color = glm::vec4(0.2f, 0.6f, 1.0f, 1.0f);
                    else if (bone.name.find("Leg") != std::string::npos || bone.name.find("Foot") != std::string::npos) color = glm::vec4(0.2f, 0.2f, 0.8f, 1.0f);
                    else if (bone.name.find("Spine") != std::string::npos || bone.name.find("Torso") != std::string::npos) color = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
                    
                    addVoxelBone(bone.name, size, offset, color);
                }
            } else {
                // Group shapes by bone
                std::map<int, std::vector<Phyxel::BoneShape>> shapesByBone;
                for (const auto& shape : voxelModel.shapes) {
                    if (shape.boneId >= 0 && shape.boneId < skeleton.bones.size()) {
                        shapesByBone[shape.boneId].push_back(shape);
                    }
                }

                for (auto& pair : shapesByBone) {
                    int boneId = pair.first;
                    const auto& shapes = pair.second;
                    std::string boneName = skeleton.bones[boneId].name;

                    // Performance Optimization: Skip small bones (fingers, toes, facial features)
                    std::string nameLower = boneName;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    
                    if (nameLower.find("thumb") != std::string::npos || 
                        nameLower.find("index") != std::string::npos || 
                        nameLower.find("middle") != std::string::npos || 
                        nameLower.find("ring") != std::string::npos || 
                        nameLower.find("pinky") != std::string::npos || 
                        nameLower.find("eye") != std::string::npos || 
                        nameLower.find("toe") != std::string::npos ||
                        nameLower.find("end") != std::string::npos) {
                        continue;
                    }

                    // Calculate bounding box for physics body
                    glm::vec3 minPt(1e9f);
                    glm::vec3 maxPt(-1e9f);
                    
                    for (const auto& shape : shapes) {
                        glm::vec3 halfSize = shape.size * 0.5f;
                        minPt = glm::min(minPt, shape.offset - halfSize);
                        maxPt = glm::max(maxPt, shape.offset + halfSize);
                    }
                    
                    glm::vec3 totalSize = maxPt - minPt;
                    glm::vec3 centerOffset = (minPt + maxPt) * 0.5f;
                    
                    // Ensure minimum size for physics
                    totalSize = glm::max(totalSize, glm::vec3(0.05f));

                    // Create ONE physics body for the bone
                    addVoxelBone(boneName, totalSize, centerOffset, glm::vec4(0,0,0,0)); 
                    
                    // Now add all visual parts (voxels)
                    if (boneBodies.find(boneId) != boneBodies.end()) {
                        btRigidBody* body = boneBodies[boneId];
                        
                        // Remove the bounding box visual added by addVoxelBone
                        if (!parts.empty()) parts.pop_back(); 
                        
                        for (const auto& shape : shapes) {
                            // Calculate offset relative to the physics body center
                            glm::vec3 relativeOffset = shape.offset - centerOffset;
                            
                            // Assign colors based on body part names
                            glm::vec4 color(0.7f, 0.7f, 0.7f, 1.0f); 
                            
                            if (nameLower.find("head") != std::string::npos) color = glm::vec4(1.0f, 0.8f, 0.6f, 1.0f); 
                            else if (nameLower.find("arm") != std::string::npos) color = glm::vec4(0.2f, 0.6f, 1.0f, 1.0f); 
                            else if (nameLower.find("leg") != std::string::npos) color = glm::vec4(0.2f, 0.2f, 0.8f, 1.0f); 
                            else if (nameLower.find("torso") != std::string::npos || nameLower.find("spine") != std::string::npos) color = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f); 
                            
                            RagdollPart part;
                            part.rigidBody = body;
                            part.scale = shape.size;
                            part.color = color;
                            part.name = boneName;
                            part.offset = relativeOffset;
                            parts.push_back(part);
                        }
                    }
                }
            }
            return true;
        }
        return false;
    }

    void AnimatedVoxelCharacter::addVoxelBone(const std::string& boneName, const glm::vec3& size, const glm::vec3& offset, const glm::vec4& color) {
        if (skeleton.boneMap.find(boneName) == skeleton.boneMap.end()) {
            std::cerr << "Bone not found: " << boneName << std::endl;
            return;
        }

        int boneId = skeleton.boneMap[boneName];
        
        // Create a static cube initially
        btRigidBody* body = physicsWorld->createStaticCube(worldPosition, size);
        
        // Make it kinematic
        body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        body->setActivationState(DISABLE_DEACTIVATION);
        
        // Store it
        boneBodies[boneId] = body;
        boneOffsets[boneId] = offset;
        
        // Ignore collision with controller to prevent self-collision explosions
        if (controllerBody) {
            body->setIgnoreCollisionCheck(controllerBody, true);
            controllerBody->setIgnoreCollisionCheck(body, true);
        }
        
        // Ignore collision with other bones to prevent internal jitter
        for (auto& pair : boneBodies) {
            btRigidBody* otherBody = pair.second;
            if (otherBody != body) {
                body->setIgnoreCollisionCheck(otherBody, true);
                otherBody->setIgnoreCollisionCheck(body, true);
            }
        }
        
        // Add to parts for rendering
        parts.push_back({body, size, color, boneName});
    }

    void AnimatedVoxelCharacter::playAnimation(const std::string& animName) {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (clips[i].name == animName) {
                currentClipIndex = (int)i;
                animTime = 0.0f;
                return;
            }
        }
        std::cerr << "Animation not found: " << animName << std::endl;
    }

    // Helper to configure animation fixes
    void AnimatedVoxelCharacter::configureAnimationFixes() {
        // No hardcoded fixes. We rely on the user providing correctly oriented animations.
        // If you need to rotate an animation, you can add it here:
        // animationRotationOffsets["walk"] = -90.0f; 
    }



    void AnimatedVoxelCharacter::setControlInput(float forward, float turn) {
        currentForwardInput = forward;
        currentTurnInput = turn;
    }

    void AnimatedVoxelCharacter::setPosition(const glm::vec3& pos) {
        worldPosition = pos;
        if (controllerBody) {
            // Get actual half-height from collision shape to account for dynamic scaling
            float halfHeight = 0.9f; // Default fallback
            if (controllerBody->getCollisionShape()->getShapeType() == BOX_SHAPE_PROXYTYPE) {
                const btBoxShape* box = static_cast<const btBoxShape*>(controllerBody->getCollisionShape());
                halfHeight = box->getHalfExtentsWithMargin().y();
            }

            btTransform trans;
            trans.setIdentity();
            trans.setOrigin(btVector3(pos.x, pos.y + halfHeight, pos.z));
            controllerBody->setWorldTransform(trans);
            controllerBody->getMotionState()->setWorldTransform(trans);
            controllerBody->setLinearVelocity(btVector3(0,0,0));
            controllerBody->setAngularVelocity(btVector3(0,0,0));
        }
    }

    glm::vec3 AnimatedVoxelCharacter::getPosition() const {
        return worldPosition;
    }

    void AnimatedVoxelCharacter::jump() {
        jumpRequested = true;
    }

    void AnimatedVoxelCharacter::attack() {
        attackRequested = true;
    }

    void AnimatedVoxelCharacter::setCrouch(bool crouch) {
        isCrouching = crouch;
    }

    void AnimatedVoxelCharacter::updateStateMachine(float deltaTime) {
        stateTimer += deltaTime;
        
        // Get current animation duration if valid
        float currentAnimDuration = 0.0f;
        if (currentClipIndex >= 0 && currentClipIndex < clips.size()) {
            currentAnimDuration = clips[currentClipIndex].duration;
        }

        // Check vertical velocity for falling
        float verticalVel = 0.0f;
        if (controllerBody) {
            verticalVel = controllerBody->getLinearVelocity().y();
        }

        // State Transitions
        switch (currentState) {
            case AnimatedCharacterState::Idle:
            case AnimatedCharacterState::Walk:
            case AnimatedCharacterState::Run:
            case AnimatedCharacterState::Crouch:
            case AnimatedCharacterState::CrouchIdle:
            case AnimatedCharacterState::CrouchWalk:
            case AnimatedCharacterState::TurnLeft:
            case AnimatedCharacterState::TurnRight:
                // Handle Actions (High Priority)
                if (jumpRequested) {
                    std::cout << "DEBUG: Jump requested, switching state." << std::endl;
                    currentState = AnimatedCharacterState::Jump;
                    stateTimer = 0.0f;
                    jumpRequested = false;
                    // Apply physics impulse
                    if (controllerBody) {
                        btVector3 vel = controllerBody->getLinearVelocity();
                        controllerBody->setLinearVelocity(btVector3(vel.x(), 7.0f, vel.z())); 
                    }
                } else if (attackRequested) {
                    currentState = AnimatedCharacterState::Attack;
                    stateTimer = 0.0f;
                    attackRequested = false;
                } else if (verticalVel < -3.0f) {
                    // Falling detection
                    currentState = AnimatedCharacterState::Fall;
                    stateTimer = 0.0f;
                } else if (isCrouching) {
                    if (glm::abs(currentForwardInput) > 0.01f) {
                        currentState = AnimatedCharacterState::CrouchWalk;
                    } else {
                        // If we were walking, go to CrouchIdle immediately
                        if (currentState == AnimatedCharacterState::CrouchWalk) {
                             currentState = AnimatedCharacterState::CrouchIdle;
                        } 
                        // If we are just starting to crouch, go to Crouch (transition)
                        else if (currentState != AnimatedCharacterState::Crouch && currentState != AnimatedCharacterState::CrouchIdle) {
                             currentState = AnimatedCharacterState::Crouch;
                             stateTimer = 0.0f;
                        }
                        // If we are in Crouch and it finished, go to CrouchIdle
                        else if (currentState == AnimatedCharacterState::Crouch && currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                             currentState = AnimatedCharacterState::CrouchIdle;
                        }
                    }
                } else {
                    // Movement Logic
                    if (glm::abs(currentForwardInput) > 0.6f) {
                        currentState = AnimatedCharacterState::Run;
                    } else if (glm::abs(currentForwardInput) > 0.01f) {
                        currentState = AnimatedCharacterState::Walk;
                    } else if (glm::abs(currentTurnInput) > 0.1f) {
                        // Turn in place
                        if (currentTurnInput > 0) currentState = AnimatedCharacterState::TurnRight;
                        else currentState = AnimatedCharacterState::TurnLeft;
                    } else {
                        currentState = AnimatedCharacterState::Idle;
                    }
                }
                break;

            case AnimatedCharacterState::Jump:
                // Physics-driven transition
                // If we are falling significantly, switch to Fall
                if (verticalVel < -2.0f) {
                    currentState = AnimatedCharacterState::Fall;
                } 
                // If we hit something above or stopped moving up (apex), stay in Jump until fall starts
                // If we landed on a ledge (vel ~ 0)
                else if (glm::abs(verticalVel) < 0.01f && stateTimer > 0.5f) {
                     // We probably landed
                     currentState = AnimatedCharacterState::Idle;
                }
                break;

            case AnimatedCharacterState::Fall:
                // If we hit the ground (vertical velocity near 0)
                if (glm::abs(verticalVel) < 0.1f) {
                    currentState = AnimatedCharacterState::Idle;
                }
                break;

            case AnimatedCharacterState::Attack:
                // Attack is a one-shot animation
                if (currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                    currentState = AnimatedCharacterState::Idle;
                }
                break;
                
            default:
                currentState = AnimatedCharacterState::Idle;
                break;
        }
    }

    void AnimatedVoxelCharacter::update(float deltaTime) {
        // 1. Update Physics Controller
        if (controllerBody) {
            // Handle Rotation
            float turnSpeed = 2.0f;
            currentYaw -= currentTurnInput * turnSpeed * deltaTime;
            
            // Update State Machine
            updateStateMachine(deltaTime);

            // Handle Movement based on State
            float moveSpeed = 0.0f;
            
            // Use animation speed if available
            if (currentClipIndex >= 0 && currentClipIndex < clips.size()) {
                float animSpeed = clips[currentClipIndex].speed;
                if (animSpeed > 0.1f) {
                    moveSpeed = animSpeed;
                }
            }
            
            // Override speed based on state if needed
            if (currentState == AnimatedCharacterState::Walk) moveSpeed = 2.0f; // Fallback
            if (currentState == AnimatedCharacterState::Run) moveSpeed = 5.0f; // Fallback
            if (currentState == AnimatedCharacterState::CrouchWalk) moveSpeed = 1.5f;
            if (currentState == AnimatedCharacterState::Idle || currentState == AnimatedCharacterState::Attack || 
                currentState == AnimatedCharacterState::Crouch || currentState == AnimatedCharacterState::CrouchIdle ||
                currentState == AnimatedCharacterState::TurnLeft || currentState == AnimatedCharacterState::TurnRight) moveSpeed = 0.0f;

            // Invert Z to match standard camera orientation (Forward is -Z)
            glm::vec3 forwardDir(-sin(currentYaw), 0, -cos(currentYaw));
            
            float inputDir = 0.0f;
            if (currentForwardInput > 0.01f) inputDir = 1.0f;
            else if (currentForwardInput < -0.01f) inputDir = -1.0f;
            
            // Allow some air control or movement during jump?
            if (currentState == AnimatedCharacterState::Jump || currentState == AnimatedCharacterState::Fall) {
                // Ensure we have base speed if animation didn't provide it (e.g. in-place jump)
                if (moveSpeed < 0.1f) moveSpeed = 4.0f;
                
                // Reduce control in air?
                moveSpeed *= 0.8f; 
            }

            glm::vec3 moveVel = forwardDir * inputDir * moveSpeed;
            
            btVector3 currentVel = controllerBody->getLinearVelocity();
            // Preserve vertical velocity (gravity)
            controllerBody->setLinearVelocity(btVector3(moveVel.x, currentVel.y(), moveVel.z));
            
            // Update World Position from Physics
            btTransform trans;
            controllerBody->getMotionState()->getWorldTransform(trans);
            btVector3 pos = trans.getOrigin();
            
            // Get actual half-height from collision shape to account for dynamic scaling
            float halfHeight = 0.9f; // Default fallback
            if (controllerBody->getCollisionShape()->getShapeType() == BOX_SHAPE_PROXYTYPE) {
                const btBoxShape* box = static_cast<const btBoxShape*>(controllerBody->getCollisionShape());
                halfHeight = box->getHalfExtentsWithMargin().y();
            }
            
            // Pivot is at feet, body center is at +halfHeight
            worldPosition = glm::vec3(pos.x(), pos.y() - halfHeight, pos.z());
            
            // Animation Selection Logic
            std::string targetAnim = "idle";
            switch (currentState) {
                case AnimatedCharacterState::Idle: targetAnim = "idle"; break;
                case AnimatedCharacterState::Walk: targetAnim = "walk"; break;
                case AnimatedCharacterState::Run: targetAnim = "run"; break;
                case AnimatedCharacterState::Jump: targetAnim = "jump"; break;
                case AnimatedCharacterState::Fall: targetAnim = "jump_down"; break;
                case AnimatedCharacterState::Crouch: targetAnim = "standing_to_crouched"; break;
                case AnimatedCharacterState::CrouchIdle: targetAnim = "standing_to_crouched"; break;
                case AnimatedCharacterState::CrouchWalk: targetAnim = "crouched_walking"; break;
                case AnimatedCharacterState::Attack: targetAnim = "attack"; break;
                case AnimatedCharacterState::TurnLeft: targetAnim = "left_turn"; break;
                case AnimatedCharacterState::TurnRight: targetAnim = "right_turn"; break;
                default: targetAnim = "idle"; break;
            }
            
            // Find target animation index
            int targetIndex = -1;
            
            // Priority search for better matching
            // 1. Exact match (case insensitive)
            for (size_t i = 0; i < clips.size(); ++i) {
                std::string clipNameLower = clips[i].name;
                std::transform(clipNameLower.begin(), clipNameLower.end(), clipNameLower.begin(), ::tolower);
                if (clipNameLower == targetAnim) {
                    targetIndex = (int)i;
                    break;
                }
            }
            
            // 2. "Contains" match, but filter out "strafe" if we want "walk"
            if (targetIndex == -1) {
                for (size_t i = 0; i < clips.size(); ++i) {
                    std::string clipNameLower = clips[i].name;
                    std::transform(clipNameLower.begin(), clipNameLower.end(), clipNameLower.begin(), ::tolower);
                    
                    if (clipNameLower.find(targetAnim) != std::string::npos) {
                        // Special filtering
                        if (targetAnim == "walk" && clipNameLower.find("strafe") != std::string::npos) continue;
                        
                        targetIndex = (int)i;
                        break;
                    }
                }
            }
            
            if (targetIndex == -1 && targetAnim != "idle") {
                 std::cout << "WARNING: Animation not found for target: " << targetAnim << std::endl;
            }
            
            // Switch if found and different
            if (targetIndex != -1 && targetIndex != currentClipIndex) {
                // std::cout << "Switching animation to: " << clips[targetIndex].name << " (Index: " << targetIndex << ")" << std::endl;
                currentClipIndex = targetIndex;
                animTime = 0.0f;

                // Reset skeleton to bind pose to prevent artifacts from previous animations
                for(auto& bone : skeleton.bones) {
                    bone.currentPosition = bone.localPosition;
                    bone.currentRotation = bone.localRotation;
                    bone.currentScale = bone.localScale;
                }
            }
        }

        if (currentClipIndex >= 0 && currentClipIndex < clips.size()) {
            animTime += deltaTime;
            // Loop unless it's a one-shot action
            bool loop = (currentState != AnimatedCharacterState::Attack && 
                         currentState != AnimatedCharacterState::Jump && 
                         currentState != AnimatedCharacterState::Crouch &&
                         currentState != AnimatedCharacterState::CrouchIdle);
            
            // Manual clamp for non-looping animations to prevent double-play if state persists
            if (!loop && animTime > clips[currentClipIndex].duration) {
                animTime = clips[currentClipIndex].duration;
            }
            
            // Special case for CrouchIdle: Hold the last frame of Standing_To_Crouched
            if (currentState == AnimatedCharacterState::CrouchIdle) {
                animTime = clips[currentClipIndex].duration;
            }
            
            animSystem.updateAnimation(skeleton, clips[currentClipIndex], animTime, loop);
        }

        animSystem.updateGlobalTransforms(skeleton);

        // Update physics bodies
        static int debugFrame = 0;
        debugFrame++;
        // Print every 60 frames (approx 1 sec) OR if we just started moving (to catch the transition)
        bool doDebug = (debugFrame % 60 == 0) || (debugFrame < 10); 

        if (doDebug) {
            LOG_TRACE_FMT("Character", "=== CHARACTER DEBUG FRAME " << debugFrame << " ===");
            LOG_TRACE_FMT("Character", "Position: " << worldPosition.x << ", " << worldPosition.y << ", " << worldPosition.z);
            LOG_TRACE_FMT("Character", "Animation: " << (currentClipIndex >= 0 ? clips[currentClipIndex].name : "NONE") 
                      << " (Index: " << currentClipIndex << ") Time: " << animTime);
            
            LOG_TRACE_FMT("Character", "--- BONE STATUS ---");
            for (const auto& bone : skeleton.bones) {
                // Calculate global position from the matrix for debugging
                glm::vec3 globalPos = glm::vec3(bone.globalTransform[3]);
                
                LOG_TRACE_FMT("Character", "Bone '" << bone.name << "' (ID " << bone.id << "):");
                LOG_TRACE_FMT("Character", "  Bind Local: " << bone.localPosition.x << ", " << bone.localPosition.y << ", " << bone.localPosition.z);
                LOG_TRACE_FMT("Character", "  Current Local: " << bone.currentPosition.x << ", " << bone.currentPosition.y << ", " << bone.currentPosition.z);
                LOG_TRACE_FMT("Character", "  Current Scale: " << bone.currentScale.x << ", " << bone.currentScale.y << ", " << bone.currentScale.z);
                LOG_TRACE_FMT("Character", "  Current Rot: " << bone.currentRotation.x << ", " << bone.currentRotation.y << ", " << bone.currentRotation.z << ", " << bone.currentRotation.w);
                LOG_TRACE_FMT("Character", "  Model Space: " << globalPos.x << ", " << globalPos.y << ", " << globalPos.z);
            }
            LOG_TRACE_FMT("Character", "=======================");
        }

        for (auto& pair : boneBodies) {
            int boneId = pair.first;
            btRigidBody* body = pair.second;
            
            const Phyxel::Bone& bone = skeleton.bones[boneId];
            
            // Calculate world transform
            // Bone global transform is in model space. We need to apply model->world transform.
            
            glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), worldPosition);
            modelMatrix = glm::rotate(modelMatrix, currentYaw, glm::vec3(0, 1, 0)); // Apply Yaw
            
            // Apply Animation Rotation Offset
            float animRotation = 0.0f;
            std::string stateKey = "idle";
            switch (currentState) {
                case AnimatedCharacterState::Idle: stateKey = "idle"; break;
                case AnimatedCharacterState::Walk: stateKey = "walk"; break;
                case AnimatedCharacterState::Run: stateKey = "run"; break;
                case AnimatedCharacterState::Jump: stateKey = "jump"; break;
                case AnimatedCharacterState::Crouch: stateKey = "crouch"; break;
                case AnimatedCharacterState::Attack: stateKey = "attack"; break;
            }
            
            if (animationRotationOffsets.find(stateKey) != animationRotationOffsets.end()) {
                animRotation = animationRotationOffsets[stateKey];
            }
            
            if (animRotation != 0.0f) {
                 modelMatrix = glm::rotate(modelMatrix, glm::radians(animRotation), glm::vec3(0, 1, 0));
            }

            glm::mat4 finalTransform = modelMatrix * bone.globalTransform;
            
            // Apply offset (local to bone)
            finalTransform = glm::translate(finalTransform, boneOffsets[boneId]);

            if (doDebug && (bone.name == "Hips" || boneId == 0)) {
                 glm::vec3 bonePos = glm::vec3(finalTransform[3]);
                 std::cout << "Bone " << bone.name << " GlobalPos: " << bonePos.x << ", " << bonePos.y << ", " << bonePos.z << std::endl;
                 std::cout << "Bone Local Matrix Col3: " << bone.globalTransform[3][0] << ", " << bone.globalTransform[3][1] << ", " << bone.globalTransform[3][2] << std::endl;
            }

            // Convert to Bullet transform
            glm::vec3 pos = glm::vec3(finalTransform[3]);
            glm::quat rot = glm::quat_cast(finalTransform);
            
            btTransform trans;
            trans.setOrigin(btVector3(pos.x, pos.y, pos.z));
            trans.setRotation(btQuaternion(rot.x, rot.y, rot.z, rot.w));
            
            body->getMotionState()->setWorldTransform(trans);
        }
    }

    void AnimatedVoxelCharacter::render(Graphics::RenderCoordinator* renderer) {
        // Rendering is handled by RenderCoordinator iterating over 'parts'
    }

}
}
