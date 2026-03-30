#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"
#include <iostream>
#include <algorithm>
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
        float oldHalfHeight = 0.9f;
        if (controllerBody->getCollisionShape()->getShapeType() == BOX_SHAPE_PROXYTYPE) {
            const btBoxShape* box = static_cast<const btBoxShape*>(controllerBody->getCollisionShape());
            oldHalfHeight = box->getHalfExtentsWithMargin().y();
        }
        float feetY = trans.getOrigin().y() - oldHalfHeight;
        float feetX = trans.getOrigin().x();
        float feetZ = trans.getOrigin().z();

        // Remove old controller body
        physicsWorld->removeCube(controllerBody);
        controllerBody = nullptr;

        // Create new controller with correct height.
        // createCube applies 0.95 shrink for dynamic objects, so request slightly larger.
        float requestHeight = characterHeight / 0.95f;
        glm::vec3 size(0.85f, requestHeight, 0.85f);
        float newHalfHeight = characterHeight / 2.0f;
        glm::vec3 center(feetX, feetY + newHalfHeight, feetZ);

        controllerBody = physicsWorld->createCube(center, size, 60.0f);
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
        // Remove all bone bodies from the physics world
        for (auto& pair : boneBodies) {
            if (pair.second) {
                physicsWorld->removeCube(pair.second);
            }
        }
        boneBodies.clear();
        boneOffsets.clear();
        parts.clear();

        // Also clear attachments
        detachAll();
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
                        // Go straight to Walk (crossfade blend handles the transition)
                        if (currentState != AnimatedCharacterState::Walk) {
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

    void AnimatedVoxelCharacter::update(float deltaTime) {
        // 1. Update Physics Controller
        bool usedExternalVelocity = false;
        if (controllerBody) {
            // External velocity mode (used by NPC patrol behavior)
            if (hasExternalVelocity) {
                usedExternalVelocity = true;
                btVector3 currentVel = controllerBody->getLinearVelocity();
                controllerBody->setLinearVelocity(btVector3(externalVelocity.x, currentVel.y(), externalVelocity.z));
                controllerBody->activate(true);
                hasExternalVelocity = false;

                // Face movement direction
                float speed = glm::length(glm::vec2(externalVelocity.x, externalVelocity.z));
                if (speed > 0.01f) {
                    currentYaw = atan2(externalVelocity.x, externalVelocity.z);
                }

                // Update world position from physics
                btTransform trans;
                controllerBody->getMotionState()->getWorldTransform(trans);
                btVector3 pos = trans.getOrigin();
                float halfHeight = 0.9f;
                if (controllerBody->getCollisionShape()->getShapeType() == BOX_SHAPE_PROXYTYPE) {
                    const btBoxShape* box = static_cast<const btBoxShape*>(controllerBody->getCollisionShape());
                    halfHeight = box->getHalfExtentsWithMargin().y();
                }
                worldPosition = glm::vec3(pos.x(), pos.y() - halfHeight, pos.z());

                // Play walk or idle animation based on speed (case-insensitive, try variants)
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

        if (currentClipIndex >= 0 && currentClipIndex < clips.size()) {
            animTime += deltaTime;
            
            // Determine looping for current animation
            bool loop = (currentState != AnimatedCharacterState::Attack && 
                         currentState != AnimatedCharacterState::Jump && 
                         currentState != AnimatedCharacterState::Crouch &&
                         currentState != AnimatedCharacterState::CrouchIdle);
            
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

}
}
