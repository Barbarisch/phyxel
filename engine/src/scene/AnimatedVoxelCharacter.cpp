#include "scene/AnimatedVoxelCharacter.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "core/ChunkManager.h"
#include "core/GpuParticlePhysics.h"
#include "graphics/RaycastVisualizer.h"
#include "utils/Logger.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
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
        clearSegmentBoxes();
        // Base class handles cleanup (skips useDirectTransform parts safely)
    }
    
    void AnimatedVoxelCharacter::createController(const glm::vec3& position) {
        m_originalHalfHeight = 0.95f;
        m_originalHalfWidth  = 0.25f;
        m_kinVelocity  = glm::vec3(0.0f);
        m_kinGrounded  = false;
        worldPosition  = position;
    }

    void AnimatedVoxelCharacter::resolveKinematicMovement(float dt) {
        auto* voxelWorld = physicsWorld ? physicsWorld->getVoxelWorld() : nullptr;
        const float halfW = m_originalHalfWidth;
        const float halfH = m_originalHalfHeight;

        if (voxelWorld) {
            // Gravity
            if (!m_kinGrounded)
                m_kinVelocity.y -= 9.81f * dt;

            // --- Vertical ---
            worldPosition.y += m_kinVelocity.y * dt;

            // Ground resolution
            glm::vec3 feetPos(worldPosition.x, worldPosition.y, worldPosition.z);
            float groundY = voxelWorld->findGroundY(feetPos, halfW, halfH + 1.0f);

            if (groundY > -1e8f && worldPosition.y < groundY) {
                worldPosition.y = groundY;
                if (m_kinVelocity.y < 0.0f)
                    m_kinVelocity.y = 0.0f;
                m_kinGrounded = true;
            } else {
                m_kinGrounded = false;
            }

            // --- Horizontal ---
            const glm::vec3 charHE(halfW, halfH - 0.05f, halfW);

            worldPosition.x += m_kinVelocity.x * dt;
            {
                glm::vec3 c(worldPosition.x, worldPosition.y + halfH, worldPosition.z);
                if (voxelWorld->overlapsTerrain(c, charHE) || voxelWorld->overlapsAnyBody(c, charHE))
                    worldPosition.x -= m_kinVelocity.x * dt;
            }

            worldPosition.z += m_kinVelocity.z * dt;
            {
                glm::vec3 c(worldPosition.x, worldPosition.y + halfH, worldPosition.z);
                if (voxelWorld->overlapsTerrain(c, charHE) || voxelWorld->overlapsAnyBody(c, charHE))
                    worldPosition.z -= m_kinVelocity.z * dt;
            }
        } else {
            // No voxel world — integrate freely (fallback, should not happen in normal use)
            m_kinVelocity.y -= 9.81f * dt;
            worldPosition += m_kinVelocity * dt;
        }
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
        // Parse archetype from .anim file header (e.g. "# archetype: humanoid_normal")
        {
            std::ifstream animIn(animFile);
            if (animIn.is_open()) {
                std::string headerLine;
                while (std::getline(animIn, headerLine)) {
                    if (headerLine.empty()) continue;
                    if (headerLine[0] != '#') break; // stop at first non-comment line
                    const std::string key = "# archetype:";
                    if (headerLine.compare(0, key.size(), key) == 0) {
                        std::string val = headerLine.substr(key.size());
                        // trim whitespace
                        size_t start = val.find_first_not_of(" \t");
                        size_t end = val.find_last_not_of(" \t\r\n");
                        if (start != std::string::npos && end != std::string::npos) {
                            m_archetype = val.substr(start, end - start + 1);
                            LOG_INFO("Character", "Parsed archetype '{}' from {}", m_archetype, animFile);
                        }
                        break;
                    }
                }
            }
        }

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
        std::vector<glm::mat4> globalTransforms(skeleton.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            const auto& bone = skeleton.bones[i];
            glm::mat4 local = glm::translate(glm::mat4(1.0f), bone.localPosition);
            local = local * glm::mat4_cast(bone.localRotation);
            if (bone.parentId == -1)
                globalTransforms[i] = local;
            else if (bone.parentId >= 0 && bone.parentId < static_cast<int>(skeleton.bones.size()))
                globalTransforms[i] = globalTransforms[bone.parentId] * local;
        }

        float minY = 0.0f, maxY = 0.0f;
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            float y = globalTransforms[i][3][1];
            if (i == 0) { minY = maxY = y; }
            else { minY = std::min(minY, y); maxY = std::max(maxY, y); }
        }

        float characterHeight = (maxY - minY) + 0.3f;
        if (characterHeight < 0.5f) characterHeight = 1.0f;

        skeletonFootOffset_ = minY;
        m_originalHalfHeight = characterHeight * 0.5f;
        m_originalHalfWidth  = 0.25f;

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

                if (shapes.size() > 1) {
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
                        part.useDirectTransform = true;
                        part.boneGroupId = boneId;
                        part.worldPos = worldPosition;
                        part.worldRot = glm::quat(1, 0, 0, 0);
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
        boneOffsets[boneId] = offset;

        RagdollPart part;
        part.useDirectTransform = true;
        part.boneGroupId        = boneId;
        part.worldPos           = worldPosition;
        part.worldRot           = glm::quat(1, 0, 0, 0);
        part.scale              = size;
        part.color              = color;
        part.name               = boneName;
        parts.push_back(part);
    }

    void AnimatedVoxelCharacter::clearBodies() {
        clearSegmentBoxes();
        boneOffsets.clear();
        parts.clear();
        detachAll();
    }

    float AnimatedVoxelCharacter::getControllerHalfHeight() const {
        return m_originalHalfHeight;
    }

    float AnimatedVoxelCharacter::getControllerHalfWidth() const {
        return m_originalHalfWidth;
    }

    glm::vec3 AnimatedVoxelCharacter::getControllerVelocity() const {
        return m_kinVelocity;
    }

    void AnimatedVoxelCharacter::resolveBodyPartCollisions() {
        if (!m_chunkManager || m_segmentBoxes.empty()) return;

        glm::vec3 totalPush(0.0f);

        for (const auto& seg : m_segmentBoxes) {
            glm::vec3 halfExtents = seg.halfExtents;
            glm::vec3 center      = seg.center;
            int       boneId      = seg.boneId;

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

        if (glm::length(totalPush) > 0.001f) {
            float maxPush = 0.5f;
            if (glm::length(totalPush) > maxPush)
                totalPush = glm::normalize(totalPush) * maxPush;
            worldPosition += totalPush;
        }
    }

    void AnimatedVoxelCharacter::rebuildCompoundShape() {
        // No-op: compound shape replaced by kinematic VoxelOccupancyGrid queries
    }

    void AnimatedVoxelCharacter::updateCompoundTransforms() {
        // No-op: compound shape replaced by kinematic VoxelOccupancyGrid queries
    }

    std::vector<AnimatedVoxelCharacter::BoneAABB> AnimatedVoxelCharacter::getBoneAABBs() const {
        std::vector<BoneAABB> result;
        for (const auto& seg : m_segmentBoxes) {
            BoneAABB aabb;
            aabb.boneId      = seg.boneId;
            aabb.boneName    = seg.boneName;
            aabb.center      = seg.center;
            aabb.halfExtents = seg.halfExtents;
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

        int boneId   = it->second;
        int attachId = m_nextAttachmentId++;

        m_attachments.push_back({attachId, boneId, size, offset, color, label,
                                  worldPosition, glm::quat(1, 0, 0, 0)});

        RagdollPart part;
        part.useDirectTransform = true;
        part.boneGroupId        = attachId + 1000;
        part.worldPos           = worldPosition;
        part.worldRot           = glm::quat(1, 0, 0, 0);
        part.scale              = size;
        part.color              = color;
        part.name               = label.empty() ? "attachment" : label;
        parts.push_back(part);

        LOG_INFO("AnimatedVoxelCharacter", "Attached '{}' to bone '{}' (id {})", label, boneName, attachId);
        return attachId;
    }

    void AnimatedVoxelCharacter::detachFromBone(int attachmentId) {
        auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
                               [attachmentId](const BoneAttachment& a) { return a.id == attachmentId; });
        if (it == m_attachments.end()) return;

        int groupId = it->id + 1000;
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                    [groupId](const RagdollPart& p) { return p.boneGroupId == groupId; }),
                    parts.end());
        m_attachments.erase(it);
    }

    void AnimatedVoxelCharacter::detachAll() {
        for (auto& att : m_attachments) {
            int groupId = att.id + 1000;
            parts.erase(std::remove_if(parts.begin(), parts.end(),
                        [groupId](const RagdollPart& p) { return p.boneGroupId == groupId; }),
                        parts.end());
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

    void AnimatedVoxelCharacter::sitAt(const glm::vec3& seatAnchorPos, float facingYaw,
                                       const glm::vec3& sitDownOffset,
                                       const glm::vec3& sittingIdleOffset,
                                       const glm::vec3& sitStandUpOffset,
                                       float sitBlendDur,
                                       float seatHeightOffset) {
        if (m_isSitting) return;

        // --- Cache hip bone index for per-frame tracking ---
        m_hipBoneIndex = -1;
        m_bindPoseHipHeight = 0.8f;  // sensible fallback
        if (!skeleton.bones.empty()) {
            std::vector<glm::mat4> globalT(skeleton.bones.size(), glm::mat4(1.0f));
            for (size_t i = 0; i < skeleton.bones.size(); ++i) {
                const auto& bone = skeleton.bones[i];
                glm::mat4 local = glm::translate(glm::mat4(1.0f), bone.localPosition)
                                * glm::mat4_cast(bone.localRotation);
                globalT[i] = (bone.parentId < 0) ? local : globalT[bone.parentId] * local;
            }
            for (size_t i = 0; i < skeleton.bones.size(); ++i) {
                std::string n = skeleton.bones[i].name;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n.find("hip") != std::string::npos) {
                    m_hipBoneIndex = static_cast<int>(i);
                    float h = globalT[i][3][1] - skeletonFootOffset_;
                    if (h >= 0.1f) m_bindPoseHipHeight = h;
                    break;
                }
            }
        }

        // Store seat anchor and apply height offset
        m_seatSurfacePos = seatAnchorPos;
        m_seatSurfacePos.y += seatHeightOffset;
        m_seatFacingYaw   = facingYaw;
        m_isSitting       = true;

        // Store per-state foot snap offsets
        m_sitDownOffset      = sitDownOffset;
        m_sittingIdleOffset  = sittingIdleOffset;
        m_sitStandUpOffset   = sitStandUpOffset;
        m_sitBlendDuration   = sitBlendDur;

        // Snap feet to the SitDown position immediately (no separate approach)
        glm::vec3 initialPos = m_seatSurfacePos + m_sitDownOffset;
        currentYaw = facingYaw;
        setPosition(initialPos);
        m_kinVelocity = glm::vec3(0.0f);

        currentState = AnimatedCharacterState::SitDown;
        stateTimer   = 0.0f;
        animTime     = 0.0f;
        LOG_DEBUG("Character", "sitAt: anchor=({:.2f},{:.2f},{:.2f}) feetSnap=({:.2f},{:.2f},{:.2f}) hipBone={}",
                  m_seatSurfacePos.x, m_seatSurfacePos.y, m_seatSurfacePos.z,
                  initialPos.x, initialPos.y, initialPos.z,
                  m_hipBoneIndex);
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
        m_kinVelocity = glm::vec3(0.0f);
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
        float verticalVel = m_kinVelocity.y;

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
                    m_kinVelocity.y = 7.0f;
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
                    m_kinVelocity.y = 7.0f;
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
                    m_kinVelocity.y = 7.0f;
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
                    m_kinVelocity.y = 7.0f;
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
                    m_kinVelocity.y = 7.0f;
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
                    m_kinVelocity.y = 7.0f;
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

    void AnimatedVoxelCharacter::detectAndApplyStepUp(const glm::vec3&, float) {
        // Disabled: will be re-implemented using VoxelOccupancyGrid queries
    }

    void AnimatedVoxelCharacter::update(float deltaTime) {
        m_totalTime += deltaTime;

        // --- Derez drain: spawn voxels whose detach time has arrived ---
        if (m_derezState && m_derezState->active && m_gpuPhysics) {
            m_derezState->elapsed += deltaTime;
            while (m_derezState->nextIdx < m_derezState->queue.size()) {
                const DerezEntry& entry = m_derezState->queue[m_derezState->nextIdx];
                if (entry.detachTime > m_derezState->elapsed) break;

                GpuParticlePhysics::SpawnParams sp;
                sp.position = entry.worldPos;
                // Small random lateral drift — gravity does the real work
                float rx = ((rand() % 1000) / 1000.f - 0.5f) * 0.8f;
                float ry = ((rand() % 1000) / 1000.f - 0.5f) * 0.3f;
                float rz = ((rand() % 1000) / 1000.f - 0.5f) * 0.8f;
                sp.velocity    = glm::vec3(rx, ry, rz);
                sp.scale       = entry.scale;
                sp.color       = entry.color;
                sp.lifetime    = 18.0f + (rand() % 120) / 10.f; // 18–30 s
                m_gpuPhysics->queueSpawn(sp);

                // Mask the voxel from rendering
                if (entry.partIndex < parts.size()) {
                    parts[entry.partIndex].active = false;
                }

                ++m_derezState->nextIdx;
            }
            // Freeze controller once all voxels are gone
            if (m_derezState->nextIdx >= m_derezState->queue.size()) {
                m_kinVelocity = glm::vec3(0.0f);
            }
            return; // skip normal animation/physics while derezzing
        }

        // --- Sitting sequence: bypass normal physics movement ---
        // Foot-anchored model: each state snaps worldPosition (= feet) to a fixed point.
        // The animation itself moves the hips/torso — we never lerp worldPosition.
        if (m_isSitting) {
            {
                glm::vec3 snapPos;
                if (currentState == AnimatedCharacterState::SitDown)
                    snapPos = m_seatSurfacePos + m_sitDownOffset;
                else if (currentState == AnimatedCharacterState::SittingIdle)
                    snapPos = m_seatSurfacePos + m_sittingIdleOffset;
                else
                    snapPos = m_seatSurfacePos + m_sitStandUpOffset;

                worldPosition = snapPos;
                m_kinVelocity = glm::vec3(0.0f);
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
                    // Use the per-seat blend duration for sit transitions
                    if (isBlending) {
                        blendDuration = m_sitBlendDuration;
                    }
                }
            }

            goto animate_and_render;
        }

        // 1. Update Physics Controller
        {
        bool usedExternalVelocity = false;
        {
            // External velocity mode (used by NPC patrol behavior)
            if (hasExternalVelocity) {
                usedExternalVelocity = true;
                m_kinVelocity.x = externalVelocity.x;
                m_kinVelocity.z = externalVelocity.z;
                hasExternalVelocity = false;

                // Face movement direction
                float speed = glm::length(glm::vec2(externalVelocity.x, externalVelocity.z));
                if (speed > 0.01f) {
                    currentYaw = atan2(externalVelocity.x, externalVelocity.z);
                }

                resolveKinematicMovement(deltaTime);

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

            // Preserve vertical velocity (gravity handled by resolveKinematicMovement)
            m_kinVelocity.x = moveVel.x;
            m_kinVelocity.z = moveVel.z;

            resolveKinematicMovement(deltaTime);
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

        // Hook for subclass IK corrections (e.g. HybridCharacter)
        applyIKCorrections(deltaTime);

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

        // Compute model-to-world base matrix (shared by all bones)
        glm::vec3 visualOrigin = worldPosition - glm::vec3(0.0f, skeletonFootOffset_, 0.0f);
        glm::mat4 modelMatrix  = glm::translate(glm::mat4(1.0f), visualOrigin);
        modelMatrix = glm::rotate(modelMatrix, currentYaw, glm::vec3(0, 1, 0));

        float animRotation = 0.0f;
        if (currentClipIndex >= 0 && currentClipIndex < static_cast<int>(clips.size())) {
            const std::string& animName = clips[currentClipIndex].name;
            auto rit = animationRotationOffsets.find(animName);
            if (rit != animationRotationOffsets.end()) animRotation = rit->second;
        }
        if (animRotation == 0.0f) {
            std::string stateKey = "idle";
            switch (currentState) {
                case AnimatedCharacterState::StartWalk:       stateKey = "start_walking";    break;
                case AnimatedCharacterState::Walk:            stateKey = "walk";              break;
                case AnimatedCharacterState::Run:             stateKey = "run";               break;
                case AnimatedCharacterState::Jump:            stateKey = "jump";              break;
                case AnimatedCharacterState::Fall:            stateKey = "jump_down";         break;
                case AnimatedCharacterState::Land:            stateKey = "landing";           break;
                case AnimatedCharacterState::Crouch:          stateKey = "crouch";            break;
                case AnimatedCharacterState::CrouchIdle:      stateKey = "crouch";            break;
                case AnimatedCharacterState::CrouchWalk:      stateKey = "crouched_walking";  break;
                case AnimatedCharacterState::StandUp:         stateKey = "crouch_to_stand";   break;
                case AnimatedCharacterState::Attack:          stateKey = "attack";            break;
                case AnimatedCharacterState::TurnLeft:        stateKey = "left_turn";         break;
                case AnimatedCharacterState::TurnRight:       stateKey = "right_turn";        break;
                case AnimatedCharacterState::StrafeLeft:      stateKey = "left_strafe";       break;
                case AnimatedCharacterState::StrafeRight:     stateKey = "right_strafe";      break;
                case AnimatedCharacterState::WalkStrafeLeft:  stateKey = "left_strafe_walk";  break;
                case AnimatedCharacterState::WalkStrafeRight: stateKey = "right_strafe_walk"; break;
                default: break;
            }
            auto rit = animationRotationOffsets.find(stateKey);
            if (rit != animationRotationOffsets.end()) animRotation = rit->second;
        }
        if (animRotation != 0.0f)
            modelMatrix = glm::rotate(modelMatrix, glm::radians(animRotation), glm::vec3(0, 1, 0));

        // Update worldPos/worldRot for every direct-transform part (one matrix lookup per bone group)
        for (auto& [boneId, offset] : boneOffsets) {
            if (boneId < 0 || boneId >= static_cast<int>(skeleton.bones.size())) continue;
            const Phyxel::Bone& bone = skeleton.bones[boneId];

            glm::mat4 finalTransform = modelMatrix * bone.globalTransform;
            finalTransform = glm::translate(finalTransform, offset);

            if (doDebug && (bone.name == "Hips" || boneId == 0)) {
                glm::vec3 bonePos = glm::vec3(finalTransform[3]);
                std::cout << "Bone " << bone.name << " GlobalPos: "
                          << bonePos.x << ", " << bonePos.y << ", " << bonePos.z << std::endl;
            }

            glm::vec3 pos = glm::vec3(finalTransform[3]);
            glm::quat rot = glm::quat_cast(finalTransform);

            for (auto& part : parts) {
                if (part.useDirectTransform && part.boneGroupId == boneId) {
                    part.worldPos = pos;
                    part.worldRot = rot;
                }
            }
        }

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

            att.worldPos = attPos;
            att.worldRot = attRot;
            for (auto& part : parts) {
                if (part.useDirectTransform && part.boneGroupId == att.id + 1000) {
                    part.worldPos = attPos;
                    part.worldRot = attRot;
                }
            }
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
            info.position    = seg.center;
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

            // --- Compute from skeleton child vectors ---
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

            m_segmentBoxes.push_back({ seg.name, boneId, glm::vec3(0.0f), halfExtents, seg.isArm, false });
        }

        LOG_INFO_FMT("Character", "Built " << m_segmentBoxes.size() << "/8 segment collision boxes");
    }

    void AnimatedVoxelCharacter::clearSegmentBoxes() {
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
            if (seg.boneId < 0 ||
                seg.boneId >= static_cast<int>(skeleton.bones.size())) continue;

            const Phyxel::Bone& bone = skeleton.bones[seg.boneId];
            glm::vec3 offset = boneOffsets.count(seg.boneId) ? boneOffsets.at(seg.boneId) : glm::vec3(0.0f);

            glm::mat4 finalTransform = modelMatrix * bone.globalTransform
                                     * glm::translate(glm::mat4(1.0f), offset);

            seg.center = glm::vec3(finalTransform[3]);
        }

        // Push updated segment boxes to voxel world as kinematic obstacles.
        // Pass m_kinVelocity so the solver generates speed-proportional push impulses.
        if (auto* vw = physicsWorld ? physicsWorld->getVoxelWorld() : nullptr) {
            std::vector<Physics::VoxelDynamicsWorld::KinematicObstacle> obstacles;
            obstacles.reserve(m_segmentBoxes.size());
            for (const auto& seg : m_segmentBoxes) {
                Physics::VoxelDynamicsWorld::KinematicObstacle ob;
                ob.center      = seg.center;
                ob.halfExtents = seg.halfExtents;
                ob.velocity    = m_kinVelocity;
                obstacles.push_back(ob);
            }
            vw->setKinematicObstacles(std::move(obstacles));
        }
    }

    void AnimatedVoxelCharacter::checkSegmentVoxelOverlap() {
        if (!m_chunkManager || m_segmentBoxes.empty()) return;

        for (auto& seg : m_segmentBoxes) {
            seg.colliding = false;
            const glm::vec3& center = seg.center;
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
            glm::vec3 center = seg.center;

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

    // ---- Derez implementation ----

    void AnimatedVoxelCharacter::beginDerez(Phyxel::GpuParticlePhysics* gpu, float duration,
                                            DerezPattern pattern) {
        if (!gpu || parts.empty()) return;
        if (m_derezState && m_derezState->active) return; // already in progress

        m_gpuPhysics = gpu;

        DerezState state;
        state.duration = duration;
        state.active   = true;

        // --- Snapshot world positions for every active voxel ---
        // Parts share bone bodies; world position = bone_transform * voxel_offset.
        // Store original partIndex so we can set active=false when the voxel detaches.
        state.queue.reserve(parts.size());

        glm::vec3 characterCenter = getPosition();

        for (size_t i = 0; i < parts.size(); ++i) {
            const RagdollPart& part = parts[i];
            if (!part.active) continue;

            glm::mat4 model = glm::translate(glm::mat4(1.0f), part.worldPos) * glm::mat4_cast(part.worldRot);
            glm::vec3 wp = glm::vec3(model * glm::vec4(part.offset, 1.0f));

            DerezEntry entry;
            entry.worldPos   = wp;
            entry.scale      = part.scale;
            entry.color      = part.color;
            entry.partIndex  = i;
            entry.detachTime = 0.0f; // assigned below
            state.queue.push_back(entry);
        }

        if (state.queue.empty()) return;

        // --- Assign detachTime based on pattern ---
        const size_t n = state.queue.size();
        std::vector<size_t> order(n);
        std::iota(order.begin(), order.end(), 0);

        if (pattern == DerezPattern::Wave) {
            // Sort by world-Y ascending: lowest voxels (feet) fall first
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return state.queue[a].worldPos.y < state.queue[b].worldPos.y;
            });
        } else if (pattern == DerezPattern::Periphery) {
            // Sort by distance from character center descending: tips fall first
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                float da = glm::distance(state.queue[a].worldPos, characterCenter);
                float db = glm::distance(state.queue[b].worldPos, characterCenter);
                return da > db;
            });
        } else {
            // Random order
            std::mt19937 rng(static_cast<unsigned>(rand()));
            std::shuffle(order.begin(), order.end(), rng);
        }

        // Assign times linearly with ±5% jitter
        for (size_t rank = 0; rank < n; ++rank) {
            float base  = (static_cast<float>(rank) / static_cast<float>(n)) * duration;
            float jitter = ((rand() % 100) / 100.f - 0.5f) * (duration / n) * 0.5f;
            state.queue[order[rank]].detachTime = glm::max(0.0f, base + jitter);
        }

        // Sort queue by detachTime so the drain loop is a simple sequential walk
        std::sort(state.queue.begin(), state.queue.end(), [](const DerezEntry& a, const DerezEntry& b) {
            return a.detachTime < b.detachTime;
        });

        // Freeze movement input immediately
        currentForwardInput = 0.0f;
        currentTurnInput    = 0.0f;
        currentStrafeInput  = 0.0f;

        m_derezState = std::move(state);

        LOG_INFO_FMT("AnimatedCharacter", "beginDerez: " << n << " voxels over "
                     << duration << "s, pattern=" << static_cast<int>(pattern));

        // Freeze all physics bodies so they don't fall under gravity during the effect.
        // Bodies remain in the physics world so RenderCoordinator can still read their transforms.
        // Without this, the controller/bone bodies fall ~20m over 2 seconds and accumulate
        // contact manifolds that can cause a crash when the entity is destroyed.
        clearSegmentBoxes();
        m_kinVelocity = glm::vec3(0.0f);
    }

    bool AnimatedVoxelCharacter::isDerezzing() const {
        return m_derezState.has_value() && m_derezState->active;
    }

    bool AnimatedVoxelCharacter::isFullyDerezed() const {
        return m_derezState.has_value()
            && m_derezState->active
            && m_derezState->nextIdx >= m_derezState->queue.size();
    }

}
}
