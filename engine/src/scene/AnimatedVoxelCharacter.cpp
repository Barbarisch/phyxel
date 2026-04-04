#include "scene/AnimatedVoxelCharacter.h"
#include "core/ChunkManager.h"
#include "graphics/RaycastVisualizer.h"
#include "utils/Logger.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Phyxel {
namespace Scene {

    AnimatedVoxelCharacter::AnimatedVoxelCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position)
        : RagdollCharacter(physicsWorld, position), worldPosition(position) {
        createController(position);
    }

    AnimatedVoxelCharacter::~AnimatedVoxelCharacter() {
        // Clean up segment boxes before clearing bone bodies
        clearSegmentBoxes();

        // Base class handles cleanup of rigid bodies in 'parts'
        boneBodies.clear();
        
        if (controllerBody) {
            physicsWorld->removeCube(controllerBody);
            controllerBody = nullptr;
        }

        // Clean up compound shape and its children
        if (m_compoundShape) {
            while (m_compoundShape->getNumChildShapes() > 0) {
                m_compoundShape->removeChildShapeByIndex(m_compoundShape->getNumChildShapes() - 1);
            }
            delete m_compoundShape;
            m_compoundShape = nullptr;
        }
        for (auto* shape : m_compoundChildShapes) {
            delete shape;
        }
        m_compoundChildShapes.clear();
    }
    
    void AnimatedVoxelCharacter::createController(const glm::vec3& position) {
        // Create a box for the controller (invisible physics representation).
        // Step-up over subcube-height obstacles (≈0.333 blocks) is handled via
        // raycast detection + position correction in update(), not via shape geometry.
        constexpr float halfWidth = 0.25f;
        constexpr float halfHeight = 0.95f;
        constexpr float npcMass = 60.0f;

        // Center is at +halfHeight so feet are at 'position'
        // Use createCubeInternal with shrink=1.0 (no shrink) — the 0.95 shrink in
        // createCube is for debris separation, not character controllers.
        Physics::PhysicsWorld::CubeCreationParams params;
        params.position = position + glm::vec3(0, halfHeight, 0);
        params.size = glm::vec3(halfWidth * 2.0f, halfHeight * 2.0f, halfWidth * 2.0f);
        params.mass = npcMass;
        params.sizeShrinkFactor = 1.0f;
        params.friction = 0.0f;
        params.restitution = 0.0f;
        controllerBody = physicsWorld->createCubeInternal(params);
        controllerBody->setUserPointer(this); // Mark as character part to prevent auto-cleanup
        
        // Prevent tipping over (lock rotation on all axes — we handle rotation manually)
        controllerBody->setAngularFactor(btVector3(0, 0, 0));
        
        // Low friction for smooth movement, we handle stopping manually
        controllerBody->setFriction(0.0f);
        controllerBody->setRestitution(0.0f);
        
        // Disable sleeping
        controllerBody->setActivationState(DISABLE_DEACTIVATION);
    }

    void AnimatedVoxelCharacter::setAppearance(const CharacterAppearance& appearance) {
        appearance_ = appearance;
    }

    void AnimatedVoxelCharacter::recolorFromAppearance() {
        for (auto& part : parts) {
            // Skip parts with alpha=0 (physics-only bounding boxes)
            if (part.color.a > 0.0f) {
                part.color = appearance_.getColorForBone(part.name);
            }
        }
    }

    bool AnimatedVoxelCharacter::loadModel(const std::string& animFile) {
        if (animSystem.loadFromFile(animFile, skeleton, clips, voxelModel)) {
            // Store original unscaled template for later rebuilds
            originalSkeleton_ = skeleton;
            originalVoxelModel_ = voxelModel;
            originalClips_ = clips;
            hasOriginalTemplate_ = true;

            LOG_INFO_FMT("Character", "=== Loaded Animations (" << clips.size() << ") ===");
            for (size_t i = 0; i < clips.size(); ++i) {
                LOG_INFO_FMT("Character", "  [" << i << "] " << clips[i].name << " (Duration: " << clips[i].duration << "s)");
            }
            LOG_INFO_FMT("Character", "=====================================");

            // Auto-detect morphology if not already set
            if (appearance_.morphology == MorphologyType::Unknown) {
                appearance_.morphology = detectMorphology(skeleton);
            }

            configureAnimationFixes();
            buildBodiesFromModel();
            return true;
        }
        return false;
    }

    bool AnimatedVoxelCharacter::loadFromSkeleton(const Phyxel::Skeleton& skel,
                                                   const Phyxel::VoxelModel& model,
                                                   const std::vector<Phyxel::AnimationClip>& animations) {
        skeleton = skel;
        voxelModel = model;
        clips = animations;

        // Store original unscaled template for later rebuilds
        originalSkeleton_ = skel;
        originalVoxelModel_ = model;
        originalClips_ = animations;
        hasOriginalTemplate_ = true;

        LOG_INFO_FMT("Character", "=== Loaded from skeleton template (" << clips.size() << " animations, " << voxelModel.shapes.size() << " shapes) ===");
        for (size_t i = 0; i < clips.size(); ++i) {
            LOG_INFO_FMT("Character", "  [" << i << "] " << clips[i].name << " (Duration: " << clips[i].duration << "s)");
        }

        // Auto-detect morphology if not already set
        if (appearance_.morphology == MorphologyType::Unknown) {
            appearance_.morphology = detectMorphology(skeleton);
        }

        configureAnimationFixes();
        applySkeletonProportions();
        resizeController();
        buildBodiesFromModel();
        return true;
    }

    // Determine per-limb scale factors for a bone based on its lowercased name.
    // Dispatches to morphology-specific logic.
    static void getLimbScales(const std::string& nameLower, const CharacterAppearance& app,
                              float& lengthScale, float& thicknessScale) {
        lengthScale = app.heightScale;
        thicknessScale = app.bulkScale;

        switch (app.morphology) {
        case MorphologyType::Quadruped:
            if (nameLower.find("head") != std::string::npos) {
                lengthScale = app.headScale;
                thicknessScale = app.headScale;
            } else if (nameLower.find("neck") != std::string::npos) {
                lengthScale = app.heightScale * app.neckLengthScale;
            } else if (nameLower.find("tail") != std::string::npos) {
                lengthScale = app.heightScale * app.tailLengthScale;
            } else if (nameLower.find("paw") != std::string::npos ||
                       nameLower.find("ankle") != std::string::npos) {
                lengthScale = app.heightScale * app.legLengthScale;
                thicknessScale = app.bulkScale * 0.9f;
            } else if (nameLower.find("leg") != std::string::npos) {
                lengthScale = app.heightScale * app.legLengthScale;
            } else if (nameLower.find("shoulder") != std::string::npos) {
                lengthScale = app.shoulderWidthScale;
                thicknessScale = app.bulkScale * app.shoulderWidthScale;
            } else if (nameLower.find("chest") != std::string::npos ||
                       nameLower.find("belly") != std::string::npos ||
                       nameLower.find("pelvis") != std::string::npos) {
                lengthScale = app.heightScale * app.torsoLengthScale;
            }
            break;

        case MorphologyType::Arachnid:
            if (nameLower.find("coxa") != std::string::npos ||
                nameLower.find("femur") != std::string::npos ||
                nameLower.find("tibia") != std::string::npos ||
                nameLower.find("leg") != std::string::npos) {
                lengthScale = app.heightScale * app.legLengthScale;
            } else if (nameLower.find("abdomen") != std::string::npos) {
                lengthScale = app.heightScale * app.torsoLengthScale;
                thicknessScale = app.bulkScale * 1.1f;
            } else if (nameLower.find("thorax") != std::string::npos ||
                       nameLower.find("cephalothorax") != std::string::npos) {
                lengthScale = app.heightScale * app.torsoLengthScale;
            } else if (nameLower.find("fang") != std::string::npos ||
                       nameLower.find("pedipalp") != std::string::npos) {
                lengthScale = app.headScale;
            }
            break;

        case MorphologyType::Dragon:
            if (nameLower.find("head") != std::string::npos ||
                nameLower.find("jaw") != std::string::npos ||
                nameLower.find("snout") != std::string::npos) {
                lengthScale = app.headScale;
                thicknessScale = app.headScale;
            } else if (nameLower.find("neck") != std::string::npos) {
                lengthScale = app.heightScale * app.neckLengthScale;
            } else if (nameLower.find("wing") != std::string::npos) {
                lengthScale = app.heightScale * app.wingSpanScale;
            } else if (nameLower.find("tail") != std::string::npos) {
                lengthScale = app.heightScale * app.tailLengthScale;
            } else if (nameLower.find("leg") != std::string::npos ||
                       nameLower.find("forearm") != std::string::npos) {
                lengthScale = app.heightScale * app.legLengthScale;
            } else if (nameLower.find("pelvis") != std::string::npos ||
                       nameLower.find("belly") != std::string::npos ||
                       nameLower.find("breast") != std::string::npos) {
                lengthScale = app.heightScale * app.torsoLengthScale;
            }
            break;

        case MorphologyType::Humanoid:
        case MorphologyType::Unknown:
        default:
            // Original humanoid logic
            if (nameLower.find("head") != std::string::npos ||
                nameLower.find("neck") != std::string::npos) {
                if (nameLower.find("head") != std::string::npos) {
                    lengthScale = app.headScale;
                    thicknessScale = app.headScale;
                } else {
                    lengthScale = app.heightScale * app.torsoLengthScale;
                }
            } else if (nameLower.find("arm") != std::string::npos ||
                       nameLower.find("forearm") != std::string::npos ||
                       nameLower.find("hand") != std::string::npos) {
                lengthScale = app.heightScale * app.armLengthScale;
            } else if (nameLower.find("shoulder") != std::string::npos) {
                lengthScale = app.shoulderWidthScale;
                thicknessScale = app.bulkScale * app.shoulderWidthScale;
            } else if (nameLower.find("leg") != std::string::npos ||
                       nameLower.find("upleg") != std::string::npos ||
                       nameLower.find("foot") != std::string::npos) {
                lengthScale = app.heightScale * app.legLengthScale;
            } else if (nameLower.find("spine") != std::string::npos ||
                       nameLower.find("chest") != std::string::npos) {
                lengthScale = app.heightScale * app.torsoLengthScale;
            } else if (nameLower.find("hip") != std::string::npos) {
                lengthScale = app.heightScale;
                thicknessScale = app.bulkScale;
            }
            break;
        }
    }

    void AnimatedVoxelCharacter::applySkeletonProportions() {
        // Scale skeleton joint positions and animation keyframes based on appearance proportions.
        // Without this, only the visual box sizes change but joints stay in place,
        // so characters all look the same height/shape.
        for (auto& bone : skeleton.bones) {
            if (bone.parentId == -1) continue; // Skip root bone

            std::string nameLower = bone.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

            float lengthScale = 1.0f, thicknessScale = 1.0f;
            getLimbScales(nameLower, appearance_, lengthScale, thicknessScale);

            // Scale the bone's position relative to its parent (this is what determines
            // actual limb length / body proportions)
            bone.localPosition *= lengthScale;
            bone.currentPosition = bone.localPosition;
        }

        // Scale animation position keyframes to match the new skeleton proportions.
        // Otherwise animations would snap joints back to their original unscaled positions.
        for (auto& clip : clips) {
            for (auto& channel : clip.channels) {
                if (channel.boneId <= 0 ||
                    channel.boneId >= static_cast<int>(skeleton.bones.size()))
                    continue;
                if (channel.positionKeys.empty()) continue;

                std::string nameLower = skeleton.bones[channel.boneId].name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

                float lengthScale = 1.0f, thicknessScale = 1.0f;
                getLimbScales(nameLower, appearance_, lengthScale, thicknessScale);

                for (auto& key : channel.positionKeys) {
                    key.value *= lengthScale;
                }
            }
        }
    }

    float AnimatedVoxelCharacter::computeSkeletonHeight() const {
        if (skeleton.bones.empty()) return 1.8f;

        // Compute proper global transforms (respecting bone rotations) to find the
        // real model-space Y extent.  The naive approach of summing localPosition.y
        // values up the parent chain is wrong because bones like legs are *rotated*
        // so their localPosition points downward in world space.
        std::vector<glm::mat4> globalTransforms(skeleton.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            const auto& bone = skeleton.bones[i];
            glm::mat4 local = glm::translate(glm::mat4(1.0f), bone.localPosition);
            local = local * glm::mat4_cast(bone.localRotation);

            if (bone.parentId == -1) {
                globalTransforms[i] = local;
            } else if (bone.parentId >= 0 && bone.parentId < static_cast<int>(skeleton.bones.size())) {
                globalTransforms[i] = globalTransforms[bone.parentId] * local;
            }
        }

        float minY = 0.0f, maxY = 0.0f;
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            float y = globalTransforms[i][3][1]; // model-space Y
            if (i == 0) { minY = maxY = y; }
            else { minY = std::min(minY, y); maxY = std::max(maxY, y); }
        }

        // Add padding for head volume top and foot sole bottom
        return (maxY - minY) + 0.3f;
    }

    void AnimatedVoxelCharacter::resizeController() {
        if (!controllerBody) return;

        // Compute proper model-space global transforms to find the lowest bone (feet)
        std::vector<glm::mat4> globalTransforms(skeleton.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            const auto& bone = skeleton.bones[i];
            glm::mat4 local = glm::translate(glm::mat4(1.0f), bone.localPosition);
            local = local * glm::mat4_cast(bone.localRotation);

            if (bone.parentId == -1) {
                globalTransforms[i] = local;
            } else if (bone.parentId >= 0 && bone.parentId < static_cast<int>(skeleton.bones.size())) {
                globalTransforms[i] = globalTransforms[bone.parentId] * local;
            }
        }

        float minY = 0.0f, maxY = 0.0f;
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            float y = globalTransforms[i][3][1];
            if (i == 0) { minY = maxY = y; }
            else { minY = std::min(minY, y); maxY = std::max(maxY, y); }
        }

        float characterHeight = (maxY - minY) + 0.3f;
        if (characterHeight < 0.5f) characterHeight = 1.0f;

        // skeletonFootOffset_ = how far below model origin (Y=0) the feet actually are.
        // For an unscaled Mixamo character this is ~0 (origin at feet).
        // For a dwarf with shorter legs, minY is higher (less negative / closer to 0),
        // meaning feet don't reach as far down, so the visual model needs to be shifted
        // down to keep feet on the ground.
        skeletonFootOffset_ = minY;

        // Get current feet position from existing controller
        btTransform trans;
        controllerBody->getMotionState()->getWorldTransform(trans);
        float oldHalfHeight = getControllerHalfHeight();
        float feetY = trans.getOrigin().y() - oldHalfHeight;
        float feetX = trans.getOrigin().x();
        float feetZ = trans.getOrigin().z();

        // Remove old controller body
        physicsWorld->removeCube(controllerBody);
        controllerBody = nullptr;

        // Create new controller with correct height.
        // Use createCubeInternal with shrink=1.0 — no debris shrink for controllers.
        float newHalfHeight = characterHeight / 2.0f;
        glm::vec3 center(feetX, feetY + newHalfHeight, feetZ);

        Physics::PhysicsWorld::CubeCreationParams params;
        params.position = center;
        params.size = glm::vec3(0.50f, characterHeight, 0.50f);
        params.mass = 60.0f;
        params.sizeShrinkFactor = 1.0f;
        params.friction = 0.0f;
        params.restitution = 0.0f;
        controllerBody = physicsWorld->createCubeInternal(params);
        controllerBody->setUserPointer(this);
        controllerBody->setAngularFactor(btVector3(0, 0, 0));
        controllerBody->setFriction(0.0f);
        controllerBody->setRestitution(0.0f);
        controllerBody->setActivationState(DISABLE_DEACTIVATION);

        LOG_INFO_FMT("Character", "resizeController: height=" << characterHeight 
                      << " footOffset=" << skeletonFootOffset_
                      << " minY=" << minY << " maxY=" << maxY);
    }

    void AnimatedVoxelCharacter::buildBodiesFromModel() {
        if (voxelModel.shapes.empty()) {
            std::cout << "No model shapes found. Generating default bone shapes." << std::endl;

            // Build children map
            std::map<int, std::vector<int>> childrenMap;
            for (const auto& b : skeleton.bones) {
                if (b.parentId != -1) childrenMap[b.parentId].push_back(b.id);
            }

            for (const auto& bone : skeleton.bones) {
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

                if (childrenMap[bone.id].size() > 0) {
                    hasChild = true;
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
                        for (int childId : childrenMap[bone.id]) {
                            targetVector += skeleton.bones[childId].localPosition;
                        }
                        targetVector /= (float)childrenMap[bone.id].size();
                    }
                }

                float len = glm::length(targetVector);
                if (len < 0.01f) len = 0.1f;

                glm::vec3 size(0.1f);
                glm::vec3 offset = targetVector * 0.5f;

                if (!hasChild) {
                    offset = glm::vec3(0.0f);
                    size = glm::vec3(0.05f);
                } else {
                    glm::vec3 absDir = glm::abs(targetVector);
                    float thickness = len * 0.25f;
                    thickness = glm::clamp(thickness, 0.05f, 0.15f);

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

                glm::vec4 color = appearance_.getColorForBone(bone.name);

                // Apply per-limb proportion scales
                std::string nameLwr = bone.name;
                std::transform(nameLwr.begin(), nameLwr.end(), nameLwr.begin(), ::tolower);
                float limbLength = 1.0f, limbThickness = 1.0f;
                getLimbScales(nameLwr, appearance_, limbLength, limbThickness);

                if (nameLwr.find("head") != std::string::npos) {
                    size *= appearance_.headScale;
                } else {
                    glm::vec3 absDir2 = glm::abs(offset);
                    if (absDir2.y >= absDir2.x && absDir2.y >= absDir2.z) {
                        size.x *= limbThickness;
                        size.y *= limbLength;
                        size.z *= limbThickness;
                    } else {
                        // Horizontal bone (shoulders, etc)
                        size.x *= limbLength;
                        size.y *= limbThickness;
                        size.z *= limbThickness;
                    }
                }
                offset.y *= limbLength;

                addVoxelBone(bone.name, size, offset, color);
            }
        } else {
            // Group shapes by bone
            std::map<int, std::vector<Phyxel::BoneShape>> shapesByBone;
            for (const auto& shape : voxelModel.shapes) {
                if (shape.boneId >= 0 && shape.boneId < static_cast<int>(skeleton.bones.size())) {
                    shapesByBone[shape.boneId].push_back(shape);
                }
            }

            for (auto& pair : shapesByBone) {
                int boneId = pair.first;
                const auto& shapes = pair.second;
                std::string boneName = skeleton.bones[boneId].name;

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

                glm::vec3 minPt(1e9f);
                glm::vec3 maxPt(-1e9f);

                for (const auto& shape : shapes) {
                    glm::vec3 halfSize = shape.size * 0.5f;
                    minPt = glm::min(minPt, shape.offset - halfSize);
                    maxPt = glm::max(maxPt, shape.offset + halfSize);
                }

                glm::vec3 totalSize = maxPt - minPt;
                glm::vec3 centerOffset = (minPt + maxPt) * 0.5f;
                totalSize = glm::max(totalSize, glm::vec3(0.05f));

                // Per-limb scaling
                float limbLength = 1.0f, limbThickness = 1.0f;
                getLimbScales(nameLower, appearance_, limbLength, limbThickness);

                if (nameLower.find("head") != std::string::npos) {
                    totalSize *= appearance_.headScale;
                    centerOffset *= appearance_.headScale;
                } else {
                    totalSize.x *= limbThickness;
                    totalSize.y *= limbLength;
                    totalSize.z *= limbThickness;
                    centerOffset.y *= limbLength;
                }

                addVoxelBone(boneName, totalSize, centerOffset, glm::vec4(0,0,0,0));

                if (boneBodies.find(boneId) != boneBodies.end()) {
                    btRigidBody* body = boneBodies[boneId];

                    if (!parts.empty()) parts.pop_back();

                    for (const auto& shape : shapes) {
                        glm::vec3 relativeOffset = shape.offset - centerOffset;
                        glm::vec4 color = appearance_.getColorForBone(boneName);

                        glm::vec3 scaledSize = shape.size;
                        if (nameLower.find("head") != std::string::npos) {
                            scaledSize *= appearance_.headScale;
                            relativeOffset *= appearance_.headScale;
                        } else {
                            scaledSize.x *= limbThickness;
                            scaledSize.y *= limbLength;
                            scaledSize.z *= limbThickness;
                            relativeOffset.y *= limbLength;
                        }

                        RagdollPart part;
                        part.rigidBody = body;
                        part.scale = scaledSize;
                        part.color = color;
                        part.name = boneName;
                        part.offset = relativeOffset;
                        parts.push_back(part);
                    }
                }
            }
        }

        // Build 8-segment collision boxes from the now-populated bone bodies
        buildSegmentBoxes();
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

    void AnimatedVoxelCharacter::clearBodies() {
        // Remove segment boxes before bone bodies (they reference bone IDs)
        clearSegmentBoxes();

        // Remove all bone bodies from the physics world
        for (auto& pair : boneBodies) {
            if (pair.second) {
                physicsWorld->removeCube(pair.second);
            }
        }
        boneBodies.clear();
        boneOffsets.clear();
        parts.clear();

        // NOTE: Compound shape is NOT cleaned up here. It will be replaced
        // by rebuildCompoundShape() (called after buildBodiesFromModel) or
        // freed in the destructor. PhysicsWorld::removeCube() doesn't delete
        // collision shapes, so the orphaned compound is safe.
        m_boneToCompoundChild.clear();

        // Also clear attachments
        detachAll();
    }

    float AnimatedVoxelCharacter::getControllerHalfHeight() const {
        return m_originalHalfHeight;
    }

    float AnimatedVoxelCharacter::getControllerHalfWidth() const {
        return m_originalHalfWidth;
    }

    void AnimatedVoxelCharacter::resolveBodyPartCollisions() {
        if (!m_chunkManager || !controllerBody || boneBodies.empty()) return;

        glm::vec3 totalPush(0.0f);

        for (const auto& [boneId, body] : boneBodies) {
            btCollisionShape* shape = body->getCollisionShape();
            if (shape->getShapeType() != BOX_SHAPE_PROXYTYPE) continue;

            const btBoxShape* box = static_cast<const btBoxShape*>(shape);
            btVector3 he = box->getHalfExtentsWithMargin();
            glm::vec3 halfExtents(he.x(), he.y(), he.z());

            btTransform trans;
            body->getMotionState()->getWorldTransform(trans);
            btVector3 origin = trans.getOrigin();
            glm::vec3 center(origin.x(), origin.y(), origin.z());

            // Compute integer voxel range overlapping this bone AABB
            glm::vec3 bMin = center - halfExtents;
            glm::vec3 bMax = center + halfExtents;
            int xMin = static_cast<int>(std::floor(bMin.x));
            int yMin = static_cast<int>(std::floor(bMin.y));
            int zMin = static_cast<int>(std::floor(bMin.z));
            int xMax = static_cast<int>(std::floor(bMax.x));
            int yMax = static_cast<int>(std::floor(bMax.y));
            int zMax = static_cast<int>(std::floor(bMax.z));

            for (int x = xMin; x <= xMax; ++x) {
                for (int y = yMin; y <= yMax; ++y) {
                    for (int z = zMin; z <= zMax; ++z) {
                        if (!m_chunkManager->hasVoxelAt(glm::ivec3(x, y, z))) continue;

                        // Voxel occupies [x, x+1) x [y, y+1) x [z, z+1)
                        glm::vec3 vMin(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                        glm::vec3 vMax = vMin + glm::vec3(1.0f);

                        // AABB overlap on each axis
                        float overlapX = std::min(bMax.x, vMax.x) - std::max(bMin.x, vMin.x);
                        float overlapY = std::min(bMax.y, vMax.y) - std::max(bMin.y, vMin.y);
                        float overlapZ = std::min(bMax.z, vMax.z) - std::max(bMin.z, vMin.z);

                        if (overlapX <= 0.0f || overlapY <= 0.0f || overlapZ <= 0.0f) continue;

                        // Determine bone name for head check
                        bool isHead = false;
                        if (boneId < static_cast<int>(skeleton.bones.size())) {
                            std::string name = skeleton.bones[boneId].name;
                            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                            isHead = (name.find("head") != std::string::npos);
                        }

                        // Find minimum separation axis
                        // For XZ: push character away from voxel
                        // For Y: only push down for head (ceiling), skip otherwise
                        float pushX = 0.0f, pushY = 0.0f, pushZ = 0.0f;

                        // Determine push direction per axis (from voxel center to bone center)
                        float vCenterX = vMin.x + 0.5f;
                        float vCenterY = vMin.y + 0.5f;
                        float vCenterZ = vMin.z + 0.5f;

                        float dirX = (center.x > vCenterX) ? overlapX : -overlapX;
                        float dirY = (center.y > vCenterY) ? overlapY : -overlapY;
                        float dirZ = (center.z > vCenterZ) ? overlapZ : -overlapZ;

                        // Pick minimum penetration axis for push-out
                        if (isHead && overlapY < overlapX && overlapY < overlapZ) {
                            // Head hitting ceiling: push down
                            pushY = dirY;
                        } else if (overlapX < overlapZ) {
                            pushX = dirX;
                        } else {
                            pushZ = dirZ;
                        }

                        totalPush.x += pushX;
                        totalPush.y += pushY;
                        totalPush.z += pushZ;
                    }
                }
            }
        }

        // Apply accumulated push to controller body
        if (glm::length(totalPush) > 0.001f) {
            // Clamp push magnitude to prevent teleporting
            float maxPush = 0.5f;
            if (glm::length(totalPush) > maxPush) {
                totalPush = glm::normalize(totalPush) * maxPush;
            }

            btTransform trans;
            controllerBody->getMotionState()->getWorldTransform(trans);
            btVector3 pos = trans.getOrigin();
            pos += btVector3(totalPush.x, totalPush.y, totalPush.z);
            trans.setOrigin(pos);
            controllerBody->getMotionState()->setWorldTransform(trans);
            controllerBody->setWorldTransform(trans);

            // Update worldPosition to match
            float halfHeight = getControllerHalfHeight();
            worldPosition = glm::vec3(pos.x(), pos.y() - halfHeight, pos.z());
        }
    }

    void AnimatedVoxelCharacter::rebuildCompoundShape() {
        if (boneBodies.empty()) return;

        // Clean up old compound
        if (m_compoundShape) {
            while (m_compoundShape->getNumChildShapes() > 0) {
                m_compoundShape->removeChildShapeByIndex(m_compoundShape->getNumChildShapes() - 1);
            }
            delete m_compoundShape;
            m_compoundShape = nullptr;
        }
        for (auto* shape : m_compoundChildShapes) {
            delete shape;
        }
        m_compoundChildShapes.clear();
        m_boneToCompoundChild.clear();

        m_compoundShape = new btCompoundShape();

        // Add each bone's box shape as a child of the compound
        int childIdx = 0;
        for (auto& [boneId, body] : boneBodies) {
            btCollisionShape* boneShape = body->getCollisionShape();
            if (boneShape->getShapeType() != BOX_SHAPE_PROXYTYPE) continue;

            const btBoxShape* boneBox = static_cast<const btBoxShape*>(boneShape);
            btVector3 halfExtents = boneBox->getHalfExtentsWithMargin();

            // Create a copy of the bone's box shape for the compound
            btBoxShape* childShape = new btBoxShape(halfExtents);
            m_compoundChildShapes.push_back(childShape);

            // Start with identity — will be updated each frame
            btTransform childTransform;
            childTransform.setIdentity();
            m_compoundShape->addChildShape(childTransform, childShape);

            m_boneToCompoundChild[boneId] = childIdx;
            childIdx++;
        }

        if (childIdx == 0) {
            delete m_compoundShape;
            m_compoundShape = nullptr;
            return;
        }

        // Save original box dimensions before replacing
        if (controllerBody) {
            btCollisionShape* oldShape = controllerBody->getCollisionShape();
            if (oldShape && oldShape->getShapeType() == BOX_SHAPE_PROXYTYPE) {
                const btBoxShape* box = static_cast<const btBoxShape*>(oldShape);
                m_originalHalfHeight = box->getHalfExtentsWithMargin().y();
                m_originalHalfWidth = box->getHalfExtentsWithMargin().x();
            }
            controllerBody->setCollisionShape(m_compoundShape);
            // Old box shape is managed by PhysicsWorld (created via createCube), don't delete it here

            // Update compound transforms immediately using bind pose
            updateCompoundTransforms();
        }

        LOG_INFO("Character", "Built compound shape with {} bone children", childIdx);
    }

    void AnimatedVoxelCharacter::updateCompoundTransforms() {
        if (!m_compoundShape || !controllerBody) return;

        // Get controller body world transform
        btTransform controllerWorldTrans;
        controllerBody->getMotionState()->getWorldTransform(controllerWorldTrans);
        btTransform controllerInverse = controllerWorldTrans.inverse();

        for (auto& [boneId, childIdx] : m_boneToCompoundChild) {
            if (boneId >= static_cast<int>(skeleton.bones.size())) continue;

            const Phyxel::Bone& bone = skeleton.bones[boneId];

            // Compute bone world transform (same as the bone body update loop)
            glm::vec3 visualOrigin = worldPosition - glm::vec3(0.0f, skeletonFootOffset_, 0.0f);
            glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), visualOrigin);
            modelMatrix = glm::rotate(modelMatrix, currentYaw, glm::vec3(0, 1, 0));

            // Apply animation rotation offset
            float animRotation = 0.0f;
            if (currentClipIndex >= 0 && currentClipIndex < static_cast<int>(clips.size())) {
                auto rit = animationRotationOffsets.find(clips[currentClipIndex].name);
                if (rit != animationRotationOffsets.end()) animRotation = rit->second;
            }
            if (animRotation != 0.0f) {
                modelMatrix = glm::rotate(modelMatrix, glm::radians(animRotation), glm::vec3(0, 1, 0));
            }

            glm::mat4 finalTransform = modelMatrix * bone.globalTransform;
            finalTransform = glm::translate(finalTransform, boneOffsets[boneId]);

            // Convert to Bullet world transform
            glm::vec3 boneWorldPos = glm::vec3(finalTransform[3]);
            glm::quat boneWorldRot = glm::quat_cast(finalTransform);
            btTransform boneWorldTrans;
            boneWorldTrans.setOrigin(btVector3(boneWorldPos.x, boneWorldPos.y, boneWorldPos.z));
            boneWorldTrans.setRotation(btQuaternion(boneWorldRot.x, boneWorldRot.y, boneWorldRot.z, boneWorldRot.w));

            // Convert to controller-local frame
            btTransform localTrans = controllerInverse * boneWorldTrans;

            m_compoundShape->updateChildTransform(childIdx, localTrans, false);
        }

        // Recalculate AABB after all children updated
        m_compoundShape->recalculateLocalAabb();
    }

    std::vector<AnimatedVoxelCharacter::BoneAABB> AnimatedVoxelCharacter::getBoneAABBs() const {
        std::vector<BoneAABB> result;
        for (const auto& [boneId, body] : boneBodies) {
            btCollisionShape* shape = body->getCollisionShape();
            if (shape->getShapeType() != BOX_SHAPE_PROXYTYPE) continue;

            const btBoxShape* box = static_cast<const btBoxShape*>(shape);
            btVector3 halfExtents = box->getHalfExtentsWithMargin();

            btTransform trans;
            body->getMotionState()->getWorldTransform(trans);
            btVector3 pos = trans.getOrigin();

            BoneAABB aabb;
            aabb.boneId = boneId;
            aabb.boneName = (boneId < static_cast<int>(skeleton.bones.size())) ? skeleton.bones[boneId].name : "";
            aabb.center = glm::vec3(pos.x(), pos.y(), pos.z());
            aabb.halfExtents = glm::vec3(halfExtents.x(), halfExtents.y(), halfExtents.z());
            result.push_back(aabb);
        }
        return result;
    }

    int AnimatedVoxelCharacter::attachToBone(const std::string& boneName, const glm::vec3& size,
                                              const glm::vec3& offset, const glm::vec4& color,
                                              const std::string& label) {
        auto it = skeleton.boneMap.find(boneName);
        if (it == skeleton.boneMap.end()) {
            LOG_WARN("AnimatedVoxelCharacter", "Cannot attach to bone '{}' — not found", boneName);
            return -1;
        }

        int boneId = it->second;
        btRigidBody* body = physicsWorld->createStaticCube(worldPosition, size);
        body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        body->setActivationState(DISABLE_DEACTIVATION);

        // Ignore collision with controller and all bone bodies
        if (controllerBody) {
            body->setIgnoreCollisionCheck(controllerBody, true);
            controllerBody->setIgnoreCollisionCheck(body, true);
        }
        for (auto& pair : boneBodies) {
            body->setIgnoreCollisionCheck(pair.second, true);
            pair.second->setIgnoreCollisionCheck(body, true);
        }

        int attachId = m_nextAttachmentId++;
        m_attachments.push_back({attachId, boneId, body, size, offset, color, label});
        parts.push_back({body, size, color, label.empty() ? "attachment" : label});

        LOG_INFO("AnimatedVoxelCharacter", "Attached '{}' to bone '{}' (id {})", label, boneName, attachId);
        return attachId;
    }

    void AnimatedVoxelCharacter::detachFromBone(int attachmentId) {
        auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
                               [attachmentId](const BoneAttachment& a) { return a.id == attachmentId; });
        if (it == m_attachments.end()) return;

        // Remove from parts
        btRigidBody* body = it->body;
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                    [body](const RagdollPart& p) { return p.rigidBody == body; }),
                    parts.end());

        // Remove from physics
        physicsWorld->removeCube(body);
        m_attachments.erase(it);
    }

    void AnimatedVoxelCharacter::detachAll() {
        for (auto& att : m_attachments) {
            // Remove from parts
            parts.erase(std::remove_if(parts.begin(), parts.end(),
                        [&att](const RagdollPart& p) { return p.rigidBody == att.body; }),
                        parts.end());
            physicsWorld->removeCube(att.body);
        }
        m_attachments.clear();
    }

    void AnimatedVoxelCharacter::rebuildWithAppearance(const CharacterAppearance& appearance) {
        if (!hasOriginalTemplate_) return;

        appearance_ = appearance;

        // Remove existing bodies
        clearBodies();

        // Restore skeleton/model/clips from the original unscaled template
        skeleton = originalSkeleton_;
        voxelModel = originalVoxelModel_;
        clips = originalClips_;

        // Re-apply proportions and rebuild
        configureAnimationFixes();
        applySkeletonProportions();
        resizeController();
        buildBodiesFromModel();
    }

    CharacterSkeleton AnimatedVoxelCharacter::buildCharacterSkeleton() const {
        CharacterSkeleton cs;
        cs.skeleton = skeleton;
        cs.voxelModel = voxelModel;
        cs.appearance = appearance_;
        cs.computeBoneSizes();
        cs.generateJointDefs();
        return cs;
    }

    void AnimatedVoxelCharacter::playAnimation(const std::string& animName) {
        // If we are already playing this animation, do nothing
        if (currentClipIndex >= 0 && currentClipIndex < clips.size() && clips[currentClipIndex].name == animName) {
            return;
        }

        // Find the new animation (exact match first, then case-insensitive)
        int newClipIndex = -1;
        std::string animLower = animName;
        std::transform(animLower.begin(), animLower.end(), animLower.begin(), ::tolower);
        for (size_t i = 0; i < clips.size(); ++i) {
            if (clips[i].name == animName) {
                newClipIndex = (int)i;
                break;
            }
            std::string clipLower = clips[i].name;
            std::transform(clipLower.begin(), clipLower.end(), clipLower.begin(), ::tolower);
            if (clipLower == animLower && newClipIndex == -1) {
                newClipIndex = (int)i;
            }
        }

        if (newClipIndex != -1) {
            // Start blending
            previousClipIndex = currentClipIndex;
            previousAnimTime = animTime;
            
            currentClipIndex = newClipIndex;
            animTime = 0.0f;
            
            isBlending = true;
            blendFactor = 0.0f;
        } else {
            std::cerr << "Animation not found: " << animName << std::endl;
        }
    }

    std::vector<std::string> AnimatedVoxelCharacter::getAnimationNames() const {
        std::vector<std::string> names;
        for (const auto& clip : clips) {
            names.push_back(clip.name);
        }
        return names;
    }

    void AnimatedVoxelCharacter::cycleAnimation(bool next) {
        if (clips.empty()) return;
        
        int nextIndex = currentClipIndex;
        if (next) {
            nextIndex++;
            if (nextIndex >= clips.size()) nextIndex = 0;
        } else {
            nextIndex--;
            if (nextIndex < 0) nextIndex = (int)clips.size() - 1;
        }
        
        currentState = AnimatedCharacterState::Preview;
        playAnimation(clips[nextIndex].name);
        std::cout << "Preview Animation: " << clips[nextIndex].name << std::endl;
    }

    std::string AnimatedVoxelCharacter::getCurrentClipName() const {
        if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
            return clips[currentClipIndex].name;
        }
        return "";
    }

    float AnimatedVoxelCharacter::getAnimationProgress() const {
        if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
            float duration = clips[currentClipIndex].duration;
            if (duration > 0.0f) return animTime / duration;
        }
        return 0.0f;
    }

    float AnimatedVoxelCharacter::getAnimationDuration() const {
        if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
            return clips[currentClipIndex].duration;
        }
        return 0.0f;
    }

    glm::vec3 AnimatedVoxelCharacter::getForwardDirection() const {
        return glm::vec3(std::sin(currentYaw), 0.0f, std::cos(currentYaw));
    }

    void AnimatedVoxelCharacter::setAnimationState(AnimatedCharacterState state) {
        if (state == currentState) return;
        currentState = state;
        stateTimer = 0.0f;
    }

    bool AnimatedVoxelCharacter::reloadAnimations(const std::string& animFile) {
        Phyxel::Skeleton tempSkel;
        std::vector<Phyxel::AnimationClip> tempClips;
        Phyxel::VoxelModel tempModel;

        if (!animSystem.loadFromFile(animFile, tempSkel, tempClips, tempModel)) {
            return false;
        }

        // Replace only the animation clips, keep skeleton/model/bodies intact
        clips = std::move(tempClips);
        originalClips_ = clips;

        LOG_INFO_FMT("Character", "Reloaded {} animations from {}", clips.size(), animFile);

        // Reset to idle if current clip index is out of range
        if (currentClipIndex >= (int)clips.size()) {
            currentClipIndex = -1;
            currentState = AnimatedCharacterState::Idle;
        }
        // Reset blending state
        previousClipIndex = -1;
        isBlending = false;
        blendFactor = 0.0f;

        return true;
    }

    void AnimatedVoxelCharacter::setVoxelModel(const Phyxel::VoxelModel& model) {
        voxelModel = model;
        originalVoxelModel_ = model;
        clearBodies();
        buildBodiesFromModel();
    }

    void AnimatedVoxelCharacter::sitAt(const glm::vec3& approachPos, float facingYaw) {
        if (m_isSitting) return;

        // Teleport character to the approach position (in front of the chair),
        // NOT to the seat surface center. The sit-down animation plays from here.
        m_seatRootPos   = approachPos;
        m_seatFacingYaw = facingYaw;
        m_isSitting     = true;

        currentYaw = facingYaw;
        setPosition(m_seatRootPos);
        if (controllerBody) {
            controllerBody->setLinearVelocity(btVector3(0, 0, 0));
            controllerBody->setGravity(btVector3(0, 0, 0));  // No gravity while in sitting sequence
        }

        currentState = AnimatedCharacterState::SitDown;
        stateTimer   = 0.0f;
        animTime     = 0.0f;  // Reset anim time so stand_to_sit starts from frame 0
        LOG_DEBUG("Character", "sitAt: approachPos=({:.2f},{:.2f},{:.2f}) yaw={:.2f}",
                  approachPos.x, approachPos.y, approachPos.z, facingYaw);
    }

    void AnimatedVoxelCharacter::standUp() {
        if (!m_isSitting) return;
        currentState = AnimatedCharacterState::SitStandUp;
        stateTimer = 0.0f;
    }

    AnimatedCharacterState AnimatedVoxelCharacter::stringToState(const std::string& str) {
        if (str == "Idle") return AnimatedCharacterState::Idle;
        if (str == "StartWalk") return AnimatedCharacterState::StartWalk;
        if (str == "Walk") return AnimatedCharacterState::Walk;
        if (str == "Run") return AnimatedCharacterState::Run;
        if (str == "Jump") return AnimatedCharacterState::Jump;
        if (str == "Fall") return AnimatedCharacterState::Fall;
        if (str == "Land") return AnimatedCharacterState::Land;
        if (str == "Crouch") return AnimatedCharacterState::Crouch;
        if (str == "CrouchIdle") return AnimatedCharacterState::CrouchIdle;
        if (str == "CrouchWalk") return AnimatedCharacterState::CrouchWalk;
        if (str == "StandUp") return AnimatedCharacterState::StandUp;
        if (str == "Attack") return AnimatedCharacterState::Attack;
        if (str == "TurnLeft") return AnimatedCharacterState::TurnLeft;
        if (str == "TurnRight") return AnimatedCharacterState::TurnRight;
        if (str == "StrafeLeft") return AnimatedCharacterState::StrafeLeft;
        if (str == "StrafeRight") return AnimatedCharacterState::StrafeRight;
        if (str == "WalkStrafeLeft") return AnimatedCharacterState::WalkStrafeLeft;
        if (str == "WalkStrafeRight") return AnimatedCharacterState::WalkStrafeRight;
        if (str == "ClimbStairs") return AnimatedCharacterState::ClimbStairs;
        if (str == "DescendStairs") return AnimatedCharacterState::DescendStairs;
        if (str == "SitDown") return AnimatedCharacterState::SitDown;
        if (str == "SittingIdle") return AnimatedCharacterState::SittingIdle;
        if (str == "SitStandUp") return AnimatedCharacterState::SitStandUp;
        if (str == "Preview") return AnimatedCharacterState::Preview;
        return AnimatedCharacterState::Idle;
    }

    // Helper to configure animation fixes
    void AnimatedVoxelCharacter::configureAnimationFixes() {
        // No hardcoded fixes. We rely on the user providing correctly oriented animations.
        // If you need to rotate an animation, you can add it here:
        // animationRotationOffsets["walk"] = -90.0f; 
        
        // Example: Fix Jump_Down alignment if it floats or sinks
        // animationPositionOffsets["jump_down"] = glm::vec3(0.0f, -0.5f, 0.0f);
    }



    void AnimatedVoxelCharacter::setControlInput(float forward, float turn, float strafe) {
        currentForwardInput = forward;
        currentTurnInput = turn;
        currentStrafeInput = strafe;
    }

    void AnimatedVoxelCharacter::setSprint(bool sprint) {
        isSprinting = sprint;
    }

    void AnimatedVoxelCharacter::setPosition(const glm::vec3& pos) {
        worldPosition = pos;
        if (controllerBody) {
            float halfHeight = getControllerHalfHeight();

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

    void AnimatedVoxelCharacter::setMoveVelocity(const glm::vec3& velocity) {
        externalVelocity = velocity;
        hasExternalVelocity = true;
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

    void AnimatedVoxelCharacter::setAnimationMapping(const std::string& stateName, const std::string& animName) {
        animationMapping[stateName] = animName;
    }

    std::string AnimatedVoxelCharacter::getAnimationMapping(const std::string& stateName) const {
        auto it = animationMapping.find(stateName);
        if (it != animationMapping.end()) {
            return it->second;
        }
        return "";
    }

    void AnimatedVoxelCharacter::setAnimationRotationOffset(const std::string& animName, float rotationDegrees) {
        animationRotationOffsets[animName] = rotationDegrees;
    }

    void AnimatedVoxelCharacter::setAnimationPositionOffset(const std::string& animName, const glm::vec3& offset) {
        animationPositionOffsets[animName] = offset;
    }

    // Helper for debug logging
    std::string AnimatedVoxelCharacter::stateToString(AnimatedCharacterState state) const {
        switch (state) {
            case AnimatedCharacterState::Idle: return "Idle";
            case AnimatedCharacterState::StartWalk: return "StartWalk";
            case AnimatedCharacterState::Walk: return "Walk";
            case AnimatedCharacterState::Run: return "Run";
            case AnimatedCharacterState::Jump: return "Jump";
            case AnimatedCharacterState::Fall: return "Fall";
            case AnimatedCharacterState::Land: return "Land";
            case AnimatedCharacterState::Crouch: return "Crouch";
            case AnimatedCharacterState::CrouchIdle: return "CrouchIdle";
            case AnimatedCharacterState::CrouchWalk: return "CrouchWalk";
            case AnimatedCharacterState::StandUp: return "StandUp";
            case AnimatedCharacterState::Attack: return "Attack";
            case AnimatedCharacterState::TurnLeft: return "TurnLeft";
            case AnimatedCharacterState::TurnRight: return "TurnRight";
            case AnimatedCharacterState::StrafeLeft: return "StrafeLeft";
            case AnimatedCharacterState::StrafeRight: return "StrafeRight";
            case AnimatedCharacterState::WalkStrafeLeft: return "WalkStrafeLeft";
            case AnimatedCharacterState::WalkStrafeRight: return "WalkStrafeRight";
            case AnimatedCharacterState::BackwardWalk: return "BackwardWalk";
            case AnimatedCharacterState::StopWalk: return "StopWalk";
            case AnimatedCharacterState::StopRun: return "StopRun";
            case AnimatedCharacterState::ClimbStairs: return "ClimbStairs";
            case AnimatedCharacterState::DescendStairs: return "DescendStairs";
            case AnimatedCharacterState::SitDown: return "SitDown";
            case AnimatedCharacterState::SittingIdle: return "SittingIdle";
            case AnimatedCharacterState::SitStandUp: return "SitStandUp";
            case AnimatedCharacterState::Preview: return "Preview";
            default: return "Unknown";
        }
    }

    void AnimatedVoxelCharacter::updateStateMachine(float deltaTime) {
        AnimatedCharacterState previousState = currentState;
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
            case AnimatedCharacterState::StrafeLeft:
            case AnimatedCharacterState::StrafeRight:
            case AnimatedCharacterState::WalkStrafeLeft:
            case AnimatedCharacterState::WalkStrafeRight:
            case AnimatedCharacterState::ClimbStairs:
            case AnimatedCharacterState::DescendStairs:
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
                    m_hitFrameFired = false;
                } else if (verticalVel < -5.0f) {
                    // Falling detection (increased threshold to prevent jitter)
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
                    // Check for StandUp transition
                    if (currentState == AnimatedCharacterState::Crouch || 
                        currentState == AnimatedCharacterState::CrouchIdle || 
                        currentState == AnimatedCharacterState::CrouchWalk) {
                        currentState = AnimatedCharacterState::StandUp;
                        stateTimer = 0.0f;
                        break; 
                    }

                    // Movement Logic (W key gives negative forward, S gives positive)
                    bool isMovingForward = currentForwardInput < -0.01f;
                    bool isMovingBackward = currentForwardInput > 0.01f;
                    bool isMoving = isMovingForward || isMovingBackward;
                    bool isStrafing = glm::abs(currentStrafeInput) > 0.1f;
                    
                    if (currentForwardInput < -0.6f) {
                        currentState = AnimatedCharacterState::Run;
                    } else if (currentForwardInput > 0.6f) {
                        // Fast backward defaults to backward walk (no backward run)
                        currentState = AnimatedCharacterState::BackwardWalk;
                    } else if (isMovingForward && isStrafing) {
                        // Diagonal Movement
                        if (currentStrafeInput > 0) currentState = AnimatedCharacterState::WalkStrafeRight;
                        else currentState = AnimatedCharacterState::WalkStrafeLeft;
                    } else if (isMovingForward) {
                        // Stair detection: gentle vertical movement while walking forward
                        if (verticalVel > 0.5f && verticalVel < 5.0f) {
                            currentState = AnimatedCharacterState::ClimbStairs;
                        } else if (verticalVel < -0.5f && verticalVel > -5.0f) {
                            currentState = AnimatedCharacterState::DescendStairs;
                        } else if (currentState != AnimatedCharacterState::Walk) {
                            currentState = AnimatedCharacterState::Walk;
                        }
                    } else if (isMovingBackward) {
                        currentState = AnimatedCharacterState::BackwardWalk;
                    } else if (isStrafing) {
                        if (currentStrafeInput > 0) currentState = AnimatedCharacterState::StrafeRight;
                        else currentState = AnimatedCharacterState::StrafeLeft;
                    } else if (glm::abs(currentTurnInput) > 0.1f) {
                        // Turn in place
                        if (currentTurnInput > 0) currentState = AnimatedCharacterState::TurnRight;
                        else currentState = AnimatedCharacterState::TurnLeft;
                    } else {
                        // No input — go straight to Idle (crossfade blend handles smoothing)
                        currentState = AnimatedCharacterState::Idle;
                    }
                }
                break;

            case AnimatedCharacterState::StandUp:
                if (isCrouching) {
                    currentState = AnimatedCharacterState::Crouch;
                    stateTimer = 0.0f;
                } else if (currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                    currentState = AnimatedCharacterState::Idle;
                } else if (glm::abs(currentForwardInput) > 0.1f) {
                    currentState = AnimatedCharacterState::Walk;
                } else if (glm::abs(currentStrafeInput) > 0.1f) {
                    if (currentStrafeInput > 0) currentState = AnimatedCharacterState::StrafeRight;
                    else currentState = AnimatedCharacterState::StrafeLeft;
                }
                break;

            case AnimatedCharacterState::StartWalk:
                // Jump can interrupt start-walk
                if (jumpRequested) {
                    currentState = AnimatedCharacterState::Jump;
                    stateTimer = 0.0f;
                    jumpRequested = false;
                    if (controllerBody) {
                        btVector3 vel = controllerBody->getLinearVelocity();
                        controllerBody->setLinearVelocity(btVector3(vel.x(), 7.0f, vel.z()));
                    }
                }
                // If stopped moving
                else if (glm::abs(currentForwardInput) < 0.01f) {
                    currentState = AnimatedCharacterState::Idle;
                } 
                // If started running
                else if (glm::abs(currentForwardInput) > 0.6f) {
                    currentState = AnimatedCharacterState::Run;
                }
                // If animation finished
                else if (currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                    currentState = AnimatedCharacterState::Walk;
                }
                break;

            case AnimatedCharacterState::BackwardWalk:
                // Jump can interrupt backward walk
                if (jumpRequested) {
                    currentState = AnimatedCharacterState::Jump;
                    stateTimer = 0.0f;
                    jumpRequested = false;
                    if (controllerBody) {
                        btVector3 vel = controllerBody->getLinearVelocity();
                        controllerBody->setLinearVelocity(btVector3(vel.x(), 7.0f, vel.z()));
                    }
                }
                // If stopped moving backward
                else if (currentForwardInput <= 0.01f) {
                    if (currentForwardInput < -0.01f) {
                        currentState = AnimatedCharacterState::Walk;
                    } else {
                        currentState = AnimatedCharacterState::StopWalk;
                        stateTimer = 0.0f;
                    }
                }
                break;

            case AnimatedCharacterState::StopWalk:
                // Jump can interrupt stop-walk
                if (jumpRequested) {
                    currentState = AnimatedCharacterState::Jump;
                    stateTimer = 0.0f;
                    jumpRequested = false;
                    if (controllerBody) {
                        btVector3 vel = controllerBody->getLinearVelocity();
                        controllerBody->setLinearVelocity(btVector3(vel.x(), 7.0f, vel.z()));
                    }
                }
                // One-shot deceleration from walk
                else if (glm::abs(currentForwardInput) > 0.01f) {
                    // Player started moving again — cancel stop
                    if (currentForwardInput < -0.01f) currentState = AnimatedCharacterState::Walk;
                    else currentState = AnimatedCharacterState::BackwardWalk;
                } else if (currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                    currentState = AnimatedCharacterState::Idle;
                }
                break;

            case AnimatedCharacterState::StopRun:
                // Jump can interrupt stop-run
                if (jumpRequested) {
                    currentState = AnimatedCharacterState::Jump;
                    stateTimer = 0.0f;
                    jumpRequested = false;
                    if (controllerBody) {
                        btVector3 vel = controllerBody->getLinearVelocity();
                        controllerBody->setLinearVelocity(btVector3(vel.x(), 7.0f, vel.z()));
                    }
                }
                // One-shot deceleration from run
                else if (glm::abs(currentForwardInput) > 0.1f) {
                    // Player started moving again — cancel stop
                    if (currentForwardInput < -0.6f) currentState = AnimatedCharacterState::Run;
                    else if (currentForwardInput < -0.01f) currentState = AnimatedCharacterState::Walk;
                    else currentState = AnimatedCharacterState::BackwardWalk;
                } else if (currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                    currentState = AnimatedCharacterState::Idle;
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
                    currentState = AnimatedCharacterState::Land;
                    stateTimer = 0.0f;
                }
                break;

            case AnimatedCharacterState::Land:
                // Jump can interrupt landing (bunny hop)
                if (jumpRequested) {
                    currentState = AnimatedCharacterState::Jump;
                    stateTimer = 0.0f;
                    jumpRequested = false;
                    if (controllerBody) {
                        btVector3 vel = controllerBody->getLinearVelocity();
                        controllerBody->setLinearVelocity(btVector3(vel.x(), 7.0f, vel.z()));
                    }
                }
                else if (currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                    currentState = AnimatedCharacterState::Idle;
                }
                // Allow moving to interrupt landing
                else if (glm::abs(currentForwardInput) > 0.1f) {
                    currentState = AnimatedCharacterState::Walk;
                }
                // Allow strafing to interrupt landing
                else if (glm::abs(currentStrafeInput) > 0.1f) {
                    if (currentStrafeInput > 0) currentState = AnimatedCharacterState::StrafeRight;
                    else currentState = AnimatedCharacterState::StrafeLeft;
                }
                break;

            case AnimatedCharacterState::Attack:
                // Check hit frame trigger
                if (!m_hitFrameFired && currentAnimDuration > 0.0f &&
                    stateTimer / currentAnimDuration >= m_hitFrameFraction) {
                    m_hitFrameFired = true;
                    if (m_onHitFrame) m_onHitFrame();
                }
                // Attack is a one-shot animation
                if (currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                    currentState = AnimatedCharacterState::Idle;
                    m_hitFrameFired = false;
                }
                break;

            case AnimatedCharacterState::SitDown:
                // One-shot: wait for sit-down animation to finish, then hold seated idle
                if (currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                    currentState = AnimatedCharacterState::SittingIdle;
                    stateTimer = 0.0f;
                }
                break;

            case AnimatedCharacterState::SittingIdle:
                // Stay seated. Jump or explicit standUp() call releases.
                if (jumpRequested) {
                    jumpRequested = false;
                    standUp();
                }
                break;

            case AnimatedCharacterState::SitStandUp:
                // One-shot: wait for stand-up animation, then restore gravity and go Idle
                if (currentAnimDuration > 0.0f && stateTimer >= currentAnimDuration) {
                    m_isSitting = false;
                    if (controllerBody) {
                        // Restore normal gravity
                        controllerBody->setGravity(
                            physicsWorld->getWorld()->getGravity());
                    }
                    currentState = AnimatedCharacterState::Idle;
                    stateTimer = 0.0f;
                }
                break;

            case AnimatedCharacterState::Preview:
                // Exit preview if any input is detected
                if (glm::abs(currentForwardInput) > 0.01f ||
                    glm::abs(currentStrafeInput) > 0.01f ||
                    glm::abs(currentTurnInput) > 0.01f ||
                    jumpRequested || attackRequested || isCrouching) {
                    currentState = AnimatedCharacterState::Idle;
                }
                break;

            default:
                currentState = AnimatedCharacterState::Idle;
                break;
        }

        if (currentState != previousState) {
            LOG_DEBUG_FMT("Character", "State Transition: " << stateToString(previousState) << " -> " << stateToString(currentState) 
                << " (Inputs: Fwd=" << currentForwardInput << ", Strafe=" << currentStrafeInput << ")");
        }
    }

    void AnimatedVoxelCharacter::detectAndApplyStepUp(const glm::vec3& desiredVelocity, float deltaTime, btDynamicsWorld* dynamicsWorld) {
        if (!controllerBody || !dynamicsWorld) return;

        // Tick cooldown
        if (m_stepCooldown > 0.0f) {
            m_stepCooldown -= deltaTime;
            if (m_stepCooldown > 0.0f) {
                return;
            }
        }

        // Only consider step-up when actually trying to move horizontally
        float desiredXZ = std::sqrt(desiredVelocity.x * desiredVelocity.x + desiredVelocity.z * desiredVelocity.z);
        if (desiredXZ < 0.3f) {
            m_blockedFrames = 0;
            return;
        }

        btVector3 bodyPos = controllerBody->getWorldTransform().getOrigin();
        glm::vec3 currentPos(bodyPos.x(), bodyPos.y(), bodyPos.z());

        // --- Blocked detection: compare actual XZ displacement to desired ---
        float actualDX = currentPos.x - m_prevStepCheckPos.x;
        float actualDZ = currentPos.z - m_prevStepCheckPos.z;
        float actualXZ = std::sqrt(actualDX * actualDX + actualDZ * actualDZ);
        float expectedXZ = desiredXZ * deltaTime;

        m_prevStepCheckPos = currentPos;

        // If we moved more than 25% of what we wanted, we're not blocked
        if (expectedXZ > 0.001f && actualXZ > expectedXZ * 0.25f) {
            m_blockedFrames = 0;
            return;
        }

        // Accumulate blocked frames — require 3 consecutive blocked frames to trigger
        m_blockedFrames++;
        if (m_blockedFrames < 3) return;

        // --- We're genuinely blocked. Now probe for a steppable obstacle ahead. ---
        float halfH = getControllerHalfHeight();
        float halfW = getControllerHalfWidth();
        float feetY = bodyPos.y() - halfH;
        glm::vec3 moveDir = glm::normalize(glm::vec3(desiredVelocity.x, 0.0f, desiredVelocity.z));

        // Cast a horizontal ray at shin height in the movement direction to find the obstacle
        float shinHeight = feetY + 0.05f;
        btVector3 hFrom(bodyPos.x(), shinHeight, bodyPos.z());
        float hProbeLen = halfW + 0.6f;
        btVector3 hTo(bodyPos.x() + moveDir.x * hProbeLen,
                      shinHeight,
                      bodyPos.z() + moveDir.z * hProbeLen);

        btCollisionWorld::AllHitsRayResultCallback hRay(hFrom, hTo);
        dynamicsWorld->rayTest(hFrom, hTo, hRay);

        float bestStepHeight = -1.0f;

        if (hRay.hasHit()) {
            // Find closest non-self hit
            float closestFraction = 2.0f;
            for (int i = 0; i < hRay.m_collisionObjects.size(); ++i) {
                if (hRay.m_collisionObjects[i] != controllerBody &&
                    hRay.m_hitFractions[i] < closestFraction) {
                    closestFraction = hRay.m_hitFractions[i];
                }
            }

            if (closestFraction < 2.0f) {
                // Found obstacle. Probe vertically just past it to find the top surface.
                float hitX = hFrom.x() + (hTo.x() - hFrom.x()) * closestFraction;
                float hitZ = hFrom.z() + (hTo.z() - hFrom.z()) * closestFraction;
                float probeX = hitX + moveDir.x * 0.05f;
                float probeZ = hitZ + moveDir.z * 0.05f;

                btVector3 vFrom(probeX, feetY + m_maxStepHeight + 0.5f, probeZ);
                btVector3 vTo(probeX, feetY - 0.1f, probeZ);

                btCollisionWorld::AllHitsRayResultCallback vRay(vFrom, vTo);
                dynamicsWorld->rayTest(vFrom, vTo, vRay);

                if (vRay.hasHit()) {
                    // Find the highest surface (smallest fraction in a downward ray)
                    float topFraction = 2.0f;
                    for (int i = 0; i < vRay.m_collisionObjects.size(); ++i) {
                        if (vRay.m_collisionObjects[i] != controllerBody &&
                            vRay.m_hitFractions[i] < topFraction) {
                            topFraction = vRay.m_hitFractions[i];
                        }
                    }
                    if (topFraction < 2.0f) {
                        float surfaceY = vFrom.y() + (vTo.y() - vFrom.y()) * topFraction;
                        float stepH = surfaceY - feetY;
                        if (stepH > 0.05f && stepH <= m_maxStepHeight) {
                            bestStepHeight = stepH;
                        }
                    }
                }
            }
        }

        if (bestStepHeight <= 0.0f) {
            // Log: blocked but no steppable obstacle
            if (m_stepDebugLog.size() >= STEP_LOG_MAX) m_stepDebugLog.erase(m_stepDebugLog.begin());
            m_stepDebugLog.push_back({m_totalTime, currentPos.x, currentPos.y, currentPos.z, 0.0f, 0.0f, "no_obstacle", m_blockedFrames});
            m_blockedFrames = 0;
            return;
        }

        // Apply upward velocity impulse to clear the obstacle
        float liftVel = std::max(3.0f, bestStepHeight * 10.0f);
        btVector3 vel = controllerBody->getLinearVelocity();
        if (vel.y() < liftVel) {
            controllerBody->setLinearVelocity(btVector3(vel.x(), liftVel, vel.z()));
            controllerBody->activate(true);
        }

        // Log: successful step-up
        if (m_stepDebugLog.size() >= STEP_LOG_MAX) m_stepDebugLog.erase(m_stepDebugLog.begin());
        m_stepDebugLog.push_back({m_totalTime, currentPos.x, currentPos.y, currentPos.z, bestStepHeight, bestStepHeight, "stepped", m_blockedFrames});

        m_stepCooldown = 0.3f;
        m_blockedFrames = 0;

        LOG_DEBUG_FMT("Character", "Step-up: height=" << bestStepHeight << " liftVel=" << liftVel);
    }

    void AnimatedVoxelCharacter::update(float deltaTime) {
        m_totalTime += deltaTime;

        // --- Sitting sequence: bypass normal physics movement ---
        if (m_isSitting) {
            if (controllerBody) {
                if (currentState == AnimatedCharacterState::SittingIdle) {
                    // Fully seated: pin controller to seat root position every frame
                    float halfHeight = getControllerHalfHeight();
                    btTransform trans;
                    trans.setIdentity();
                    trans.setOrigin(btVector3(m_seatRootPos.x,
                                             m_seatRootPos.y + halfHeight,
                                             m_seatRootPos.z));
                    controllerBody->setWorldTransform(trans);
                    controllerBody->getMotionState()->setWorldTransform(trans);
                    controllerBody->setLinearVelocity(btVector3(0, 0, 0));
                    worldPosition = m_seatRootPos;
                } else {
                    // SitDown / SitStandUp: keep controller where it is (no XZ drift)
                    btVector3 vel = controllerBody->getLinearVelocity();
                    controllerBody->setLinearVelocity(btVector3(0, vel.y(), 0));
                    btTransform trans;
                    controllerBody->getMotionState()->getWorldTransform(trans);
                    btVector3 pos = trans.getOrigin();
                    worldPosition = glm::vec3(pos.x(), pos.y() - getControllerHalfHeight(), pos.z());
                }
                currentYaw = m_seatFacingYaw;
            }

            // Run FSM — transitions SitDown→SittingIdle→SitStandUp→Idle
            updateStateMachine(deltaTime);

            // Animation selection for sitting states (this block is normally skipped by the
            // movement path, so we must do it explicitly here for clips to actually switch)
            {
                std::string targetAnim;
                switch (currentState) {
                    case AnimatedCharacterState::SitDown:     targetAnim = "stand_to_sit"; break;
                    case AnimatedCharacterState::SittingIdle: targetAnim = "sitting_idle"; break;
                    case AnimatedCharacterState::SitStandUp:  targetAnim = "sit_to_stand"; break;
                    default: targetAnim = "idle"; break;
                }
                // Respect user-defined mapping overrides
                auto mapIt = animationMapping.find(stateToString(currentState));
                if (mapIt != animationMapping.end()) targetAnim = mapIt->second;

                // Find and switch clip (case-insensitive)
                std::string targetLower = targetAnim;
                std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), ::tolower);
                int targetIndex = -1;
                for (size_t i = 0; i < clips.size(); ++i) {
                    std::string n = clips[i].name;
                    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                    if (n == targetLower) { targetIndex = (int)i; break; }
                }
                if (targetIndex >= 0 && targetIndex != currentClipIndex) {
                    previousClipIndex = currentClipIndex;
                    previousAnimTime  = animTime;
                    currentClipIndex  = targetIndex;
                    animTime          = 0.0f;
                    isBlending        = (previousClipIndex != -1);
                    blendFactor       = 0.0f;
                }
            }

            goto animate_and_render;
        }

        // 1. Update Physics Controller
        {
        bool usedExternalVelocity = false;
        if (controllerBody) {
            // External velocity mode (used by NPC patrol behavior)
            if (hasExternalVelocity) {
                usedExternalVelocity = true;
                btVector3 currentVel = controllerBody->getLinearVelocity();
                controllerBody->setLinearVelocity(btVector3(externalVelocity.x, currentVel.y(), externalVelocity.z));
                controllerBody->activate(true);
                hasExternalVelocity = false;

                // Step-up detection (DISABLED: causes upward drift on flat ground, will revisit)
                // detectAndApplyStepUp(externalVelocity, deltaTime, physicsWorld ? physicsWorld->getWorld() : nullptr);

                // Face movement direction
                float speed = glm::length(glm::vec2(externalVelocity.x, externalVelocity.z));
                if (speed > 0.01f) {
                    currentYaw = atan2(externalVelocity.x, externalVelocity.z);
                }

                // Update world position from physics
                btTransform trans;
                controllerBody->getMotionState()->getWorldTransform(trans);
                btVector3 pos = trans.getOrigin();
                float halfHeight = getControllerHalfHeight();
                worldPosition = glm::vec3(pos.x(), pos.y() - halfHeight, pos.z());

                // Play walk or idle animation based on speed
                std::vector<std::string> candidates;
                if (speed > 0.1f) {
                    candidates = {"walk", "walking", "Walk", "Walking", "unarmed_walk"};
                } else {
                    candidates = {"idle", "Idle", "Standing", "standing"};
                }
                int targetIndex = -1;
                for (const auto& candidate : candidates) {
                    for (size_t i = 0; i < clips.size(); ++i) {
                        if (clips[i].name == candidate) { targetIndex = static_cast<int>(i); break; }
                    }
                    if (targetIndex >= 0) break;
                }
                if (targetIndex >= 0 && targetIndex != currentClipIndex) {
                    previousClipIndex = currentClipIndex;
                    previousAnimTime = animTime;
                    currentClipIndex = targetIndex;
                    animTime = 0.0f;
                    blendFactor = 0.0f;
                    isBlending = true;
                }
            } else {
            // Normal input-driven movement
            // Handle Rotation
            float turnSpeed = 2.0f;
            currentYaw -= currentTurnInput * turnSpeed * deltaTime;

            // Check segment boxes against world voxels (uses positions from previous frame)
            checkSegmentVoxelOverlap();
            // If an arm segment hit a voxel during an active animation, interrupt it
            if (m_limbBlocked &&
                (currentState == AnimatedCharacterState::Attack ||
                 currentState == AnimatedCharacterState::Walk   ||
                 currentState == AnimatedCharacterState::Run)) {
                currentState = AnimatedCharacterState::Idle;
                stateTimer = 0.0f;
                m_hitFrameFired = false;
            }
            m_limbBlocked = false;

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
            
            // Override speed based on state if needed (only if anim speed wasn't sufficient)
            if (currentState == AnimatedCharacterState::Walk && moveSpeed < 0.1f) moveSpeed = 2.0f; 
            if (currentState == AnimatedCharacterState::StartWalk && moveSpeed < 0.1f) moveSpeed = 1.5f;
            if (currentState == AnimatedCharacterState::Run && moveSpeed < 0.1f) {
                moveSpeed = 5.0f; 
                if (glm::abs(currentForwardInput) > 0.9f) moveSpeed = 8.0f;
            }
            if (currentState == AnimatedCharacterState::StrafeLeft || currentState == AnimatedCharacterState::StrafeRight ||
                currentState == AnimatedCharacterState::WalkStrafeLeft || currentState == AnimatedCharacterState::WalkStrafeRight) {
                 moveSpeed = 2.0f;
                 if (glm::abs(currentStrafeInput) > 0.6f) moveSpeed = 4.0f;
            }
            if (currentState == AnimatedCharacterState::CrouchWalk) moveSpeed = 1.5f;
            if (currentState == AnimatedCharacterState::BackwardWalk) moveSpeed = 1.5f;
            if (currentState == AnimatedCharacterState::ClimbStairs) moveSpeed = 1.5f;
            if (currentState == AnimatedCharacterState::DescendStairs) moveSpeed = 1.5f;
            if (currentState == AnimatedCharacterState::StopWalk || currentState == AnimatedCharacterState::StopRun) moveSpeed = 0.5f;
            if (currentState == AnimatedCharacterState::Idle || currentState == AnimatedCharacterState::Attack || 
                currentState == AnimatedCharacterState::Crouch || currentState == AnimatedCharacterState::CrouchIdle ||
                currentState == AnimatedCharacterState::TurnLeft || currentState == AnimatedCharacterState::TurnRight) moveSpeed = 0.0f;

            // Invert Z to match standard camera orientation (Forward is -Z)
            glm::vec3 forwardDir(-sin(currentYaw), 0, -cos(currentYaw));
            glm::vec3 rightDir = glm::normalize(glm::cross(forwardDir, glm::vec3(0, 1, 0)));
            
            float inputDir = 0.0f;
            if (currentForwardInput > 0.01f) inputDir = 1.0f;
            else if (currentForwardInput < -0.01f) inputDir = -1.0f;
            
            float strafeDir = 0.0f;
            if (currentStrafeInput > 0.01f) strafeDir = 1.0f;
            else if (currentStrafeInput < -0.01f) strafeDir = -1.0f;
            
            // Allow some air control or movement during jump?
            if (currentState == AnimatedCharacterState::Jump || currentState == AnimatedCharacterState::Fall) {
                // Ensure we have base speed if animation didn't provide it (e.g. in-place jump)
                if (moveSpeed < 0.1f) moveSpeed = 4.0f;
                
                // Reduce control in air?
                moveSpeed *= 0.8f; 
            }

            // Invert strafe direction to match standard controls (A=Left, D=Right)
            // rightDir is calculated as cross(forward, up). If forward is -Z, right is +X.
            // If strafeDir is positive (D), we want to move +X.
            // However, user reports it is backwards, so we invert it here.
            glm::vec3 moveDir = forwardDir * inputDir - rightDir * strafeDir;
            if (glm::length(moveDir) > 0.001f) moveDir = glm::normalize(moveDir);
            
            glm::vec3 moveVel = moveDir * moveSpeed;
            
            btVector3 currentVel = controllerBody->getLinearVelocity();
            // Preserve vertical velocity (gravity)
            controllerBody->setLinearVelocity(btVector3(moveVel.x, currentVel.y(), moveVel.z));

            // Step-up detection (DISABLED: causes upward drift on flat ground, will revisit)
            // detectAndApplyStepUp(glm::vec3(moveVel.x, 0.0f, moveVel.z), deltaTime, physicsWorld ? physicsWorld->getWorld() : nullptr);
            
            // Update World Position from Physics
            btTransform trans;
            controllerBody->getMotionState()->getWorldTransform(trans);
            btVector3 pos = trans.getOrigin();
            
            // Get actual half-height from collision shape to account for dynamic scaling
            float halfHeight = getControllerHalfHeight();
            
            // Pivot is at feet, body center is at +halfHeight
            worldPosition = glm::vec3(pos.x(), pos.y() - halfHeight, pos.z());
            } // end normal input-driven movement
            
            // Animation Selection Logic (only for input-driven mode; external velocity handles its own)
            if (!usedExternalVelocity) {
            std::string targetAnim = "idle";

            // DEBUG LOGGING
            static int debugFrameCounter = 0;
            bool shouldLog = (debugFrameCounter++ % 30 == 0);

            // Check user-defined mapping first
            std::string stateKey = stateToString(currentState);
            if (animationMapping.find(stateKey) != animationMapping.end()) {
                targetAnim = animationMapping[stateKey];
            } else {
                // Default hardcoded mapping
                switch (currentState) {
                    case AnimatedCharacterState::Idle: targetAnim = "idle"; break;
                    case AnimatedCharacterState::StartWalk: targetAnim = "start_walking"; break;
                    case AnimatedCharacterState::Walk: targetAnim = "walk"; break;
                    case AnimatedCharacterState::Run: 
                        if (isSprinting) targetAnim = "fast_run";
                        else targetAnim = "run"; 
                        break;
                    case AnimatedCharacterState::Jump: targetAnim = "jump"; break;
                    case AnimatedCharacterState::Fall: targetAnim = "jump_down"; break;
                    case AnimatedCharacterState::Land: targetAnim = "landing"; break;
                    case AnimatedCharacterState::Crouch: targetAnim = "standing_to_crouched"; break;
                    case AnimatedCharacterState::CrouchIdle: targetAnim = "crouch_idle"; break;
                    case AnimatedCharacterState::CrouchWalk: targetAnim = "crouched_walking"; break;
                    case AnimatedCharacterState::StandUp: targetAnim = "crouch_to_stand"; break;
                    case AnimatedCharacterState::Attack: targetAnim = "attack"; break;
                    case AnimatedCharacterState::TurnLeft: targetAnim = "left_turn"; break;
                    case AnimatedCharacterState::TurnRight: targetAnim = "right_turn"; break;
                    case AnimatedCharacterState::StrafeLeft: 
                        // Differentiate between walking strafe and running strafe based on sprint state
                        if (isSprinting) targetAnim = "left_strafe"; // Run strafe
                        else targetAnim = "left_strafe_walk"; // Walk strafe
                        break;
                    case AnimatedCharacterState::StrafeRight: 
                        // Differentiate between walking strafe and running strafe based on sprint state
                        if (isSprinting) targetAnim = "right_strafe"; // Run strafe
                        else targetAnim = "right_strafe_walk"; // Walk strafe
                        break;
                    case AnimatedCharacterState::WalkStrafeLeft: 
                        if (isSprinting) targetAnim = "left_strafe"; 
                        else targetAnim = "left_strafe_walk"; 
                        break;
                    case AnimatedCharacterState::WalkStrafeRight: 
                        if (isSprinting) targetAnim = "right_strafe"; 
                        else targetAnim = "right_strafe_walk"; 
                        break;
                    case AnimatedCharacterState::BackwardWalk: targetAnim = "walking_backward"; break;
                    case AnimatedCharacterState::StopWalk: targetAnim = "female_stop_walking"; break;
                    case AnimatedCharacterState::StopRun: targetAnim = "run_to_stop"; break;
                    case AnimatedCharacterState::ClimbStairs: targetAnim = "stair_up"; break;
                    case AnimatedCharacterState::DescendStairs: targetAnim = "stair_down"; break;
                    case AnimatedCharacterState::SitDown: targetAnim = "stand_to_sit"; break;
                    case AnimatedCharacterState::SittingIdle: targetAnim = "sitting_idle"; break;
                    case AnimatedCharacterState::SitStandUp: targetAnim = "sit_to_stand"; break;
                    case AnimatedCharacterState::Preview: targetAnim = ""; break;
                    default: targetAnim = "idle"; break;
                }
            }

            if (shouldLog) {
                std::cout << "DEBUG: Selected TargetAnim=" << targetAnim << std::endl;
            }

            // Apply Animation Position Offset
            if (animationPositionOffsets.find(targetAnim) != animationPositionOffsets.end()) {
                worldPosition += animationPositionOffsets[targetAnim];
            }
            
            // Skip animation update if in preview mode
            if (currentState == AnimatedCharacterState::Preview) {
                // Do nothing, let cycleAnimation handle it
            } else {
                // Find target animation index
                int targetIndex = -1;
                
                // Normalize targetAnim to lowercase for searching
                std::string targetAnimLower = targetAnim;
                std::transform(targetAnimLower.begin(), targetAnimLower.end(), targetAnimLower.begin(), ::tolower);

                // Priority search for better matching
                // 1. Exact match (case insensitive)
                for (size_t i = 0; i < clips.size(); ++i) {
                    std::string clipNameLower = clips[i].name;
                    std::transform(clipNameLower.begin(), clipNameLower.end(), clipNameLower.begin(), ::tolower);
                    if (clipNameLower == targetAnimLower) {
                        targetIndex = (int)i;
                        break;
                    }
                }
                

                
                if (targetIndex == -1) {
                     if (targetAnim == "idle") {
                         // If idle is missing, stop animation
                         if (currentClipIndex != -1) {
                             std::cout << "WARNING: 'idle' animation not found. Stopping animation." << std::endl;
                             std::cout << "Available animations: ";
                             for(const auto& clip : clips) std::cout << clip.name << " ";
                             std::cout << std::endl;
                             currentClipIndex = -1;
                             // Reset skeleton to bind pose so it doesn't freeze in a weird pose
                             for(auto& bone : skeleton.bones) {
                                 bone.currentPosition = bone.localPosition;
                                 bone.currentRotation = bone.localRotation;
                                 bone.currentScale = bone.localScale;
                             }
                         }
                     } else {
                         std::cout << "WARNING: Animation not found for target: " << targetAnim << std::endl;
                     }
                }
                
                // Switch if found and different
                if (targetIndex != -1 && targetIndex != currentClipIndex) {
                    // Start blending
                    previousClipIndex = currentClipIndex;
                    previousAnimTime = animTime;
                    currentClipIndex = targetIndex;
                    animTime = 0.0f;
                    
                    isBlending = true;
                    blendFactor = 0.0f;
                    
                    // If we didn't have a previous animation, just snap (no blend)
                    if (previousClipIndex == -1) {
                        isBlending = false;
                        // Reset skeleton to bind pose
                        for(auto& bone : skeleton.bones) {
                            bone.currentPosition = bone.localPosition;
                            bone.currentRotation = bone.localRotation;
                            bone.currentScale = bone.localScale;
                        }
                    }
                }
            }
            } // end !usedExternalVelocity
        }
        } // end movement block

        animate_and_render:
        if (currentClipIndex >= 0 && currentClipIndex < clips.size()) {
            animTime += deltaTime;
            
            // Determine looping for current animation
            bool loop = (currentState != AnimatedCharacterState::Attack &&
                         currentState != AnimatedCharacterState::Jump &&
                         currentState != AnimatedCharacterState::Crouch &&
                         currentState != AnimatedCharacterState::CrouchIdle &&
                         currentState != AnimatedCharacterState::SitDown &&
                         currentState != AnimatedCharacterState::SitStandUp);
            
            // Manual clamp for non-looping animations
            if (!loop && animTime > clips[currentClipIndex].duration) {
                animTime = clips[currentClipIndex].duration;
            }
            
            // Special case for CrouchIdle
            if (currentState == AnimatedCharacterState::CrouchIdle) {
                animTime = clips[currentClipIndex].duration;
            }
            
            if (isBlending && previousClipIndex >= 0 && previousClipIndex < clips.size()) {
                blendFactor += deltaTime / blendDuration;
                if (blendFactor >= 1.0f) {
                    blendFactor = 1.0f;
                    isBlending = false;
                    animSystem.updateAnimation(skeleton, clips[currentClipIndex], animTime, loop);
                } else {
                    // Blend with previous animation
                    // We freeze the previous animation at the transition point to avoid it looping unexpectedly
                    // or jumping to start if it finished.
                    bool prevLoop = true; 
                    animSystem.blendAnimation(skeleton, 
                        clips[previousClipIndex], previousAnimTime, prevLoop,
                        clips[currentClipIndex], animTime, loop,
                        blendFactor);
                }
            } else {
                animSystem.updateAnimation(skeleton, clips[currentClipIndex], animTime, loop);
            }
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
            // skeletonFootOffset_ compensates for scaled proportions so feet stay on the ground.
            glm::vec3 visualOrigin = worldPosition - glm::vec3(0.0f, skeletonFootOffset_, 0.0f);
            glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), visualOrigin);
            modelMatrix = glm::rotate(modelMatrix, currentYaw, glm::vec3(0, 1, 0)); // Apply Yaw
            
            // Apply Animation Rotation Offset
            float animRotation = 0.0f;
            
            // Try to find offset by specific animation name first
            std::string currentAnimName = "";
            if (currentClipIndex >= 0 && currentClipIndex < clips.size()) {
                currentAnimName = clips[currentClipIndex].name;
                if (animationRotationOffsets.find(currentAnimName) != animationRotationOffsets.end()) {
                    animRotation = animationRotationOffsets[currentAnimName];
                }
            }

            // Fallback to state-based lookup if no specific animation offset found
            if (animRotation == 0.0f) {
                std::string stateKey = "idle";
                switch (currentState) {
                    case AnimatedCharacterState::Idle: stateKey = "idle"; break;
                    case AnimatedCharacterState::StartWalk: stateKey = "start_walking"; break;
                    case AnimatedCharacterState::Walk: stateKey = "walk"; break;
                    case AnimatedCharacterState::Run: stateKey = "run"; break;
                    case AnimatedCharacterState::Jump: stateKey = "jump"; break;
                    case AnimatedCharacterState::Fall: stateKey = "jump_down"; break;
                    case AnimatedCharacterState::Land: stateKey = "landing"; break;
                    case AnimatedCharacterState::Crouch: stateKey = "crouch"; break;
                    case AnimatedCharacterState::CrouchIdle: stateKey = "crouch"; break;
                    case AnimatedCharacterState::CrouchWalk: stateKey = "crouched_walking"; break;
                    case AnimatedCharacterState::StandUp: stateKey = "crouch_to_stand"; break;
                    case AnimatedCharacterState::Attack: stateKey = "attack"; break;
                    case AnimatedCharacterState::TurnLeft: stateKey = "left_turn"; break;
                    case AnimatedCharacterState::TurnRight: stateKey = "right_turn"; break;
                    case AnimatedCharacterState::StrafeLeft: stateKey = "left_strafe"; break;
                    case AnimatedCharacterState::StrafeRight: stateKey = "right_strafe"; break;
                    case AnimatedCharacterState::WalkStrafeLeft: stateKey = "left_strafe_walk"; break;
                    case AnimatedCharacterState::WalkStrafeRight: stateKey = "right_strafe_walk"; break;
                    default: stateKey = "idle"; break;
                }
                
                if (animationRotationOffsets.find(stateKey) != animationRotationOffsets.end()) {
                    animRotation = animationRotationOffsets[stateKey];
                }
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

        // Compound shape disabled — individual bone bodies provide hit detection via getBoneAABBs()

        // Sync 8 segment boxes to current animated pose, then draw debug if F5 is on
        updateSegmentBoxes();
        if (m_raycastVisualizer && m_raycastVisualizer->isEnabled()) {
            drawSegmentBoxDebug();
        }

        // Position bone attachments (weapons, equipment visuals)
        for (auto& att : m_attachments) {
            if (att.boneId < 0 || att.boneId >= static_cast<int>(skeleton.bones.size())) continue;

            const Phyxel::Bone& bone = skeleton.bones[att.boneId];

            glm::vec3 visualOrigin = worldPosition - glm::vec3(0.0f, skeletonFootOffset_, 0.0f);
            glm::mat4 attModelMatrix = glm::translate(glm::mat4(1.0f), visualOrigin);
            attModelMatrix = glm::rotate(attModelMatrix, currentYaw, glm::vec3(0, 1, 0));

            // Apply animation rotation offset (same as bone loop)
            float animRot = 0.0f;
            if (currentClipIndex >= 0 && currentClipIndex < static_cast<int>(clips.size())) {
                auto rit = animationRotationOffsets.find(clips[currentClipIndex].name);
                if (rit != animationRotationOffsets.end()) animRot = rit->second;
            }
            if (animRot != 0.0f) {
                attModelMatrix = glm::rotate(attModelMatrix, glm::radians(animRot), glm::vec3(0, 1, 0));
            }

            glm::mat4 attFinal = attModelMatrix * bone.globalTransform;
            attFinal = glm::translate(attFinal, att.offset);

            glm::vec3 attPos = glm::vec3(attFinal[3]);
            glm::quat attRot = glm::quat_cast(attFinal);

            btTransform attTrans;
            attTrans.setOrigin(btVector3(attPos.x, attPos.y, attPos.z));
            attTrans.setRotation(btQuaternion(attRot.x, attRot.y, attRot.z, attRot.w));
            att.body->getMotionState()->setWorldTransform(attTrans);
        }
    }

    void AnimatedVoxelCharacter::render(Graphics::RenderCoordinator* renderer) {
        // Rendering is handled by RenderCoordinator iterating over 'parts'
    }

    std::vector<AnimatedVoxelCharacter::SegmentBoxInfo> AnimatedVoxelCharacter::getSegmentBoxInfo() const {
        std::vector<SegmentBoxInfo> result;
        for (const auto& seg : m_segmentBoxes) {
            SegmentBoxInfo info;
            info.boneName    = seg.boneName;
            info.halfExtents = seg.halfExtents;
            info.isArm       = seg.isArm;
            info.colliding   = seg.colliding;
            if (seg.body) {
                btTransform trans;
                seg.body->getMotionState()->getWorldTransform(trans);
                btVector3 o = trans.getOrigin();
                info.position = glm::vec3(o.x(), o.y(), o.z());
            }
            result.push_back(info);
        }
        return result;
    }

    // =========================================================================
    // 8-Segment Collision Boxes
    // =========================================================================

    void AnimatedVoxelCharacter::buildSegmentBoxes() {
        clearSegmentBoxes();

        static const struct { const char* name; bool isArm; } kSegments[12] = {
            { "mixamorig:Head",         false },
            { "mixamorig:Spine2",       false },  // Upper chest / shoulders
            { "mixamorig:Spine1",       false },  // Mid torso / abdomen
            { "mixamorig:Hips",         false },  // Pelvis / lower torso
            { "mixamorig:LeftArm",      true  },
            { "mixamorig:RightArm",     true  },
            { "mixamorig:LeftForeArm",  true  },
            { "mixamorig:RightForeArm", true  },
            { "mixamorig:LeftUpLeg",    false },
            { "mixamorig:RightUpLeg",   false },
            { "mixamorig:LeftLeg",      false },
            { "mixamorig:RightLeg",     false },
        };

        // Build children map for skeleton-based size fallback
        std::map<int, std::vector<int>> childrenMap;
        for (const auto& b : skeleton.bones) {
            if (b.parentId != -1) childrenMap[b.parentId].push_back(b.id);
        }

        for (const auto& seg : kSegments) {
            auto boneIt = skeleton.boneMap.find(seg.name);
            if (boneIt == skeleton.boneMap.end()) {
                LOG_WARN_FMT("Character", "Segment box: bone not in skeleton: " << seg.name);
                continue;
            }
            int boneId = boneIt->second;

            glm::vec3 halfExtents(0.0f);
            std::string source;

            // --- Priority 1: derive from existing bone body (most accurate) ---
            auto bodyIt = boneBodies.find(boneId);
            if (bodyIt != boneBodies.end()) {
                btCollisionShape* s = bodyIt->second->getCollisionShape();
                if (s->getShapeType() == BOX_SHAPE_PROXYTYPE) {
                    btVector3 he = static_cast<const btBoxShape*>(s)->getHalfExtentsWithMargin();
                    halfExtents = glm::vec3(he.x(), he.y(), he.z()) * 0.8f;
                    source = "bone body";

                    // Enforce minimum thickness per segment type so boxes are visible
                    std::string nameLow = seg.name;
                    std::transform(nameLow.begin(), nameLow.end(), nameLow.begin(), ::tolower);
                    float minThk = 0.05f;
                    if (nameLow.find("spine") != std::string::npos ||
                        nameLow.find("hip")   != std::string::npos) minThk = 0.10f;
                    if (nameLow.find("upleg") != std::string::npos ||
                        nameLow.find("leg")   != std::string::npos) minThk = 0.08f;
                    if (nameLow.find("arm")   != std::string::npos) minThk = 0.07f;
                    // Keep the longest axis (bone length), clamp the two shorter axes
                    int maxAxis = (halfExtents.y >= halfExtents.x && halfExtents.y >= halfExtents.z) ? 1
                                : (halfExtents.x >= halfExtents.z) ? 0 : 2;
                    if (maxAxis != 0) halfExtents.x = glm::max(halfExtents.x, minThk);
                    if (maxAxis != 1) halfExtents.y = glm::max(halfExtents.y, minThk);
                    if (maxAxis != 2) halfExtents.z = glm::max(halfExtents.z, minThk);
                }
            }

            // --- Priority 2: compute from skeleton child vectors ---
            if (halfExtents == glm::vec3(0.0f)) {
                const Phyxel::Bone& bone = skeleton.bones[boneId];
                std::string nameLower = bone.name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

                float limbLength = 1.0f, limbThickness = 1.0f;
                getLimbScales(nameLower, appearance_, limbLength, limbThickness);

                glm::vec3 childVec(0.0f);
                int numChildren = 0;
                if (childrenMap.count(boneId)) {
                    for (int cid : childrenMap.at(boneId)) {
                        childVec += skeleton.bones[cid].localPosition;
                        ++numChildren;
                    }
                    if (numChildren > 0) childVec /= static_cast<float>(numChildren);
                }

                float len = glm::length(childVec);
                if (len < 0.05f) len = 0.2f;

                // Work directly in half-extents throughout to avoid /2 confusion
                float halfLen = len * limbLength * 0.5f;

                // Minimum HALF-extents per segment type — ensures boxes are visible
                float minHalfThk = 0.05f;
                if (nameLower.find("spine") != std::string::npos ||
                    nameLower.find("hip")   != std::string::npos)  minHalfThk = 0.10f;
                if (nameLower.find("upleg") != std::string::npos ||
                    nameLower.find("leg")   != std::string::npos)  minHalfThk = 0.08f;
                if (nameLower.find("arm")   != std::string::npos)  minHalfThk = 0.07f;
                float thk = glm::max(len * 0.15f * limbThickness, minHalfThk);

                if (nameLower.find("head") != std::string::npos) {
                    float s = glm::max(halfLen, 0.12f) * appearance_.headScale;
                    halfExtents = glm::vec3(s);
                } else {
                    glm::vec3 absDir = glm::abs(childVec);
                    if (absDir.x >= absDir.y && absDir.x >= absDir.z)
                        halfExtents = glm::vec3(halfLen, thk, thk);
                    else if (absDir.y >= absDir.x && absDir.y >= absDir.z)
                        halfExtents = glm::vec3(thk, halfLen, thk);
                    else
                        halfExtents = glm::vec3(thk, thk, halfLen);
                }
                source = "skeleton fallback";
            }

            halfExtents = glm::max(halfExtents, glm::vec3(0.04f));

            LOG_INFO_FMT("Character", "  Segment [" << seg.name << "] source=" << source
                << " he=(" << halfExtents.x << "," << halfExtents.y << "," << halfExtents.z << ")");

            // Create kinematic CF_NO_CONTACT_RESPONSE body — overlaps detected manually
            btRigidBody* body = physicsWorld->createStaticCube(worldPosition, halfExtents * 2.0f);
            body->setCollisionFlags(body->getCollisionFlags()
                | btCollisionObject::CF_KINEMATIC_OBJECT
                | btCollisionObject::CF_NO_CONTACT_RESPONSE);
            body->setActivationState(DISABLE_DEACTIVATION);

            if (controllerBody) {
                body->setIgnoreCollisionCheck(controllerBody, true);
                controllerBody->setIgnoreCollisionCheck(body, true);
            }
            for (auto& [id, boneBody] : boneBodies) {
                body->setIgnoreCollisionCheck(boneBody, true);
                boneBody->setIgnoreCollisionCheck(body, true);
            }
            for (auto& existing : m_segmentBoxes) {
                if (existing.body) {
                    body->setIgnoreCollisionCheck(existing.body, true);
                    existing.body->setIgnoreCollisionCheck(body, true);
                }
            }

            m_segmentBoxes.push_back({ seg.name, boneId, body, halfExtents, seg.isArm, false });
        }

        LOG_INFO_FMT("Character", "Built " << m_segmentBoxes.size() << "/8 segment collision boxes");
    }

    void AnimatedVoxelCharacter::clearSegmentBoxes() {
        for (auto& seg : m_segmentBoxes) {
            if (seg.body) {
                physicsWorld->removeCube(seg.body);
                seg.body = nullptr;
            }
        }
        m_segmentBoxes.clear();
        m_limbBlocked = false;
    }

    void AnimatedVoxelCharacter::updateSegmentBoxes() {
        if (m_segmentBoxes.empty()) return;

        // Compute the same model-to-world transform used by the bone body loop
        glm::vec3 visualOrigin = worldPosition - glm::vec3(0.0f, skeletonFootOffset_, 0.0f);
        glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), visualOrigin);
        modelMatrix = glm::rotate(modelMatrix, currentYaw, glm::vec3(0, 1, 0));

        float animRot = 0.0f;
        if (currentClipIndex >= 0 && currentClipIndex < static_cast<int>(clips.size())) {
            auto rit = animationRotationOffsets.find(clips[currentClipIndex].name);
            if (rit != animationRotationOffsets.end()) animRot = rit->second;
        }
        if (animRot != 0.0f) {
            modelMatrix = glm::rotate(modelMatrix, glm::radians(animRot), glm::vec3(0, 1, 0));
        }

        for (auto& seg : m_segmentBoxes) {
            if (!seg.body || seg.boneId < 0 ||
                seg.boneId >= static_cast<int>(skeleton.bones.size())) continue;

            const Phyxel::Bone& bone = skeleton.bones[seg.boneId];
            glm::vec3 offset = boneOffsets.count(seg.boneId) ? boneOffsets.at(seg.boneId) : glm::vec3(0.0f);

            glm::mat4 finalTransform = modelMatrix * bone.globalTransform
                                     * glm::translate(glm::mat4(1.0f), offset);

            glm::vec3 pos = glm::vec3(finalTransform[3]);
            glm::quat rot = glm::quat_cast(finalTransform);

            btTransform trans;
            trans.setOrigin(btVector3(pos.x, pos.y, pos.z));
            trans.setRotation(btQuaternion(rot.x, rot.y, rot.z, rot.w));
            seg.body->getMotionState()->setWorldTransform(trans);
        }
    }

    void AnimatedVoxelCharacter::checkSegmentVoxelOverlap() {
        if (!m_chunkManager || m_segmentBoxes.empty()) return;

        for (auto& seg : m_segmentBoxes) {
            seg.colliding = false;
            if (!seg.body) continue;

            btTransform trans;
            seg.body->getMotionState()->getWorldTransform(trans);
            btVector3 origin = trans.getOrigin();
            glm::vec3 center(origin.x(), origin.y(), origin.z());
            const glm::vec3& he = seg.halfExtents;

            int xMin = static_cast<int>(std::floor(center.x - he.x));
            int yMin = static_cast<int>(std::floor(center.y - he.y));
            int zMin = static_cast<int>(std::floor(center.z - he.z));
            int xMax = static_cast<int>(std::floor(center.x + he.x));
            int yMax = static_cast<int>(std::floor(center.y + he.y));
            int zMax = static_cast<int>(std::floor(center.z + he.z));

            bool hit = false;
            for (int x = xMin; x <= xMax && !hit; ++x)
                for (int y = yMin; y <= yMax && !hit; ++y)
                    for (int z = zMin; z <= zMax && !hit; ++z)
                        if (m_chunkManager->hasVoxelAt(glm::ivec3(x, y, z)))
                            hit = true;

            if (hit) {
                seg.colliding = true;
                if (seg.isArm) m_limbBlocked = true;
            }
        }
    }

    void AnimatedVoxelCharacter::drawSegmentBoxDebug() {
        if (!m_raycastVisualizer) return;

        // addLine() is cleared every frame by beginFrame(), so we just push lines each frame.
        // Use min/max corners computed from center ± halfExtents.
        auto addWireBox = [&](const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& col) {
            // Bottom
            m_raycastVisualizer->addLine({mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},col);
            m_raycastVisualizer->addLine({mx.x,mn.y,mn.z},{mx.x,mn.y,mx.z},col);
            m_raycastVisualizer->addLine({mx.x,mn.y,mx.z},{mn.x,mn.y,mx.z},col);
            m_raycastVisualizer->addLine({mn.x,mn.y,mx.z},{mn.x,mn.y,mn.z},col);
            // Top
            m_raycastVisualizer->addLine({mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},col);
            m_raycastVisualizer->addLine({mx.x,mx.y,mn.z},{mx.x,mx.y,mx.z},col);
            m_raycastVisualizer->addLine({mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z},col);
            m_raycastVisualizer->addLine({mn.x,mx.y,mx.z},{mn.x,mx.y,mn.z},col);
            // Verticals
            m_raycastVisualizer->addLine({mn.x,mn.y,mn.z},{mn.x,mx.y,mn.z},col);
            m_raycastVisualizer->addLine({mx.x,mn.y,mn.z},{mx.x,mx.y,mn.z},col);
            m_raycastVisualizer->addLine({mx.x,mn.y,mx.z},{mx.x,mx.y,mx.z},col);
            m_raycastVisualizer->addLine({mn.x,mn.y,mx.z},{mn.x,mx.y,mx.z},col);
        };

        for (const auto& seg : m_segmentBoxes) {
            if (!seg.body) continue;

            btTransform trans;
            seg.body->getMotionState()->getWorldTransform(trans);
            btVector3 o = trans.getOrigin();
            glm::vec3 center(o.x(), o.y(), o.z());

            glm::vec3 color;
            if (seg.colliding) {
                color = glm::vec3(1.0f, 0.0f, 0.0f);   // RED    — collision
            } else if (seg.boneName.find("Head") != std::string::npos) {
                color = glm::vec3(1.0f, 0.0f, 1.0f);   // MAGENTA — head
            } else if (seg.boneName.find("Spine") != std::string::npos) {
                color = glm::vec3(0.0f, 1.0f, 1.0f);   // CYAN   — torso
            } else if (seg.isArm) {
                color = glm::vec3(1.0f, 0.5f, 0.0f);   // ORANGE — arms
            } else {
                color = glm::vec3(1.0f, 1.0f, 0.0f);   // YELLOW — legs (not green, avoids controller color)
            }

            glm::vec3 he = seg.halfExtents;
            glm::vec3 mn = center - he;
            glm::vec3 mx = center + he;
            addWireBox(mn, mx, color);

            // Draw a cross at the bone center for clear position reference
            float r = glm::max(he.x, glm::max(he.y, he.z)) * 0.4f;
            m_raycastVisualizer->addLine(center - glm::vec3(r,0,0), center + glm::vec3(r,0,0), color);
            m_raycastVisualizer->addLine(center - glm::vec3(0,r,0), center + glm::vec3(0,r,0), color);
            m_raycastVisualizer->addLine(center - glm::vec3(0,0,r), center + glm::vec3(0,0,r), color);
        }
    }

}
}
