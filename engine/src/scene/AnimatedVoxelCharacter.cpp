#include "scene/AnimatedVoxelCharacter.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "core/ChunkManager.h"
#include "core/GpuParticlePhysics.h"
#include "graphics/RaycastVisualizer.h"
#include "utils/Logger.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
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
        if (m_kinFrozen) return;  // anim editor: position is set externally
        auto* voxelWorld = physicsWorld ? physicsWorld->getVoxelWorld() : nullptr;
        const float halfW = m_originalHalfWidth;
        const float halfH = m_originalHalfHeight;

        if (voxelWorld) {
            // Gravity and ground-snap are suppressed when Y root motion owns vertical movement
            if (!m_yRootMotionActive) {

                // --- Terrain-glide Y control ---
                // When active, smoothly moves the capsule toward a step surface instead of
                // free-falling (step-down) or snapping (step-up). Keeps m_kinGrounded true
                // throughout so the state machine never enters Fall for small terrain steps.
                if (m_stepGlideTargetY > -1.0e29f) {
                    const float dir = (m_stepGlideTargetY >= worldPosition.y) ? 1.0f : -1.0f;
                    worldPosition.y += dir * m_stepGlideSpeed * dt;
                    if ((dir > 0.0f && worldPosition.y >= m_stepGlideTargetY) ||
                        (dir < 0.0f && worldPosition.y <= m_stepGlideTargetY)) {
                        worldPosition.y    = m_stepGlideTargetY;
                        m_stepGlideTargetY = -1.0e30f;
                    }
                    m_kinVelocity.y = 0.0f;
                    m_kinGrounded   = true;
                } else {
                    // Normal gravity
                    if (!m_kinGrounded)
                        m_kinVelocity.y -= 9.81f * dt;
                    worldPosition.y += m_kinVelocity.y * dt;
                }

                // --- Ground resolution ---
                // Skipped during DescendStairs / editor stair preview so the stair Y-drive
                // can lower the character without being snapped back up.
                bool skipGroundSnap = (currentState == AnimatedCharacterState::DescendStairs) ||
                    (currentState == AnimatedCharacterState::Preview && m_stairDriveActive);
                if (!skipGroundSnap) {
                    glm::vec3 feetPos(worldPosition.x, worldPosition.y, worldPosition.z);
                    float groundY = voxelWorld->findGroundY(feetPos, halfW, halfH + 1.0f);

                    if (groundY > -1e8f) {
                        if (worldPosition.y < groundY) {
                            // Sank below — hard snap (covers edge cases where glide was inactive)
                            worldPosition.y    = groundY;
                            m_stepGlideTargetY = -1.0e30f;
                            if (m_kinVelocity.y < 0.0f) m_kinVelocity.y = 0.0f;
                            m_kinGrounded = true;
                        } else if (worldPosition.y < groundY + 0.05f) {
                            // Within grounding tolerance — stay grounded
                            m_kinGrounded = true;
                        } else if (m_kinGrounded &&
                                   m_stepGlideTargetY < -1.0e29f &&
                                   worldPosition.y <= groundY + m_maxStepHeight + 0.05f) {
                            // Small step-down: ground dropped away by ≤ m_maxStepHeight.
                            // Smooth glide down instead of triggering free-fall / Fall state.
                            m_stepGlideTargetY = groundY;
                            m_kinVelocity.y    = 0.0f;
                            m_kinGrounded      = true;
                        } else {
                            m_kinGrounded = false;
                        }
                    } else {
                        m_kinGrounded = false;
                    }
                }
            }

            // --- Horizontal ---
            const glm::vec3 charHE(halfW, halfH - 0.05f, halfW);

            // During a step-up glide the capsule is physically below the obstacle top,
            // but we've already verified XZ is clear at that elevation.  Test against
            // the glide target Y so the step surface doesn't keep blocking forward movement.
            auto xzTestCenter = [&]() -> glm::vec3 {
                float testY = (m_stepGlideTargetY > -1.0e29f) ? m_stepGlideTargetY : worldPosition.y;
                return {worldPosition.x, testY + halfH, worldPosition.z};
            };

            // Probe obstacle height at current XZ; if within m_maxStepHeight, arm a glide.
            // Verifies XZ is clear at the elevated position before committing.
            auto tryStepUp = [&]() -> bool {
                if (!m_kinGrounded || m_stepGlideTargetY > -1.0e29f) return false;
                float probeStart = worldPosition.y + m_maxStepHeight + 0.01f;
                float obstTopY   = voxelWorld->findGroundY(
                    {worldPosition.x, probeStart, worldPosition.z},
                    halfW, m_maxStepHeight + 0.05f);
                if (obstTopY < -1e8f)                                  return false;
                if (obstTopY <= worldPosition.y + 0.005f)              return false; // not above us
                if (obstTopY >  worldPosition.y + m_maxStepHeight)     return false; // too tall
                // Verify XZ is clear at the elevated position
                float savedY = worldPosition.y;
                worldPosition.y = obstTopY + 0.001f;
                glm::vec3 cTest(worldPosition.x, worldPosition.y + halfH, worldPosition.z);
                bool clear = !voxelWorld->overlapsTerrain(cTest, charHE) &&
                             !voxelWorld->overlapsAnyBody(cTest, charHE);
                worldPosition.y = savedY;
                if (!clear) return false;
                m_stepGlideTargetY = obstTopY;
                return true;
            };

            worldPosition.x += m_kinVelocity.x * dt;
            {
                glm::vec3 c = xzTestCenter();
                if (voxelWorld->overlapsTerrain(c, charHE) || voxelWorld->overlapsAnyBody(c, charHE)) {
                    if (!tryStepUp())
                        worldPosition.x -= m_kinVelocity.x * dt;
                }
            }

            worldPosition.z += m_kinVelocity.z * dt;
            {
                glm::vec3 c = xzTestCenter();
                if (voxelWorld->overlapsTerrain(c, charHE) || voxelWorld->overlapsAnyBody(c, charHE)) {
                    if (!tryStepUp())
                        worldPosition.z -= m_kinVelocity.z * dt;
                }
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
            // Apply per-clip tuning metadata from "# clip_meta:" comment lines
            {
                std::ifstream metaIn(animFile);
                std::string ml;
                while (std::getline(metaIn, ml)) {
                    if (ml.empty()) continue;
                    if (ml[0] != '#') break; // header comments only
                    const std::string prefix = "# clip_meta:";
                    if (ml.compare(0, prefix.size(), prefix) != 0) continue;
                    std::istringstream ss(ml.substr(prefix.size()));
                    std::string clipName;
                    ss >> clipName;
                    auto it = std::find_if(clips.begin(), clips.end(),
                        [&](const AnimationClip& c){ return c.name == clipName; });
                    if (it == clips.end()) continue;
                    std::string kv;
                    while (ss >> kv) {
                        auto eq = kv.find('=');
                        if (eq == std::string::npos) continue;
                        std::string k = kv.substr(0, eq);
                        if (k == "type") { it->clipType = kv.substr(eq + 1); continue; }
                        float v = std::stof(kv.substr(eq + 1));
                        if (k == "warpEnabled")      it->warpEnabled      = (v != 0.0f);
                        else if (k == "authoredFallDist") it->authoredFallDist = v;
                        else if (k == "takeoffEnd")       it->takeoffEnd       = v;
                        else if (k == "contactFrame")     it->contactFrame     = v;
                        else if (k == "warpScaleMin")     it->warpScaleMin     = v;
                        else if (k == "warpScaleMax")     it->warpScaleMax     = v;
                        else if (k == "hitFrameFraction") it->hitFrameFraction = v;
                        else if (k == "interruptible")   it->interruptible   = (v != 0.0f);
                        else if (k == "interruptAfter")  it->interruptAfter  = v;
                        else if (k == "stairStepHeight") it->stairStepHeight = v;
                        else if (k == "stairStepDepth")  it->stairStepDepth  = v;
                        else if (k == "contactFrame1")      it->contactFrame1      = v;
                        else if (k == "contactFrame2")      it->contactFrame2      = v;
                        else if (k == "footIKSurfaceReach") it->footIKSurfaceReach = v;
                        else if (k == "footIKBodyRange")    it->footIKBodyRange    = v;
                        else if (k == "footIKEnabled") {} // editor-only, no runtime field
                    }
                }
            }

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

    // Phase-based time warp helpers.
    // The air phase [takeoffEnd, landingStart] is scaled by warpScale; the
    // takeoff and landing phases play at their authored speed.
    static float computeWarpedDuration(float dur, float te, float ls, float scale) {
        float T1 = te * dur;
        float T2 = ls * dur;
        return T1 + (T2 - T1) * scale + (dur - T2);
    }
    static float remapToAuthored(float t, float dur, float te, float ls, float scale) {
        float T1 = te * dur;
        float T2 = ls * dur;
        float airW = (T2 - T1) * scale;
        if (t <= T1) return t;
        if (airW > 0.0f && t <= T1 + airW)
            return T1 + (t - T1) / scale;
        return T2 + (t - T1 - airW);
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
            if (duration > 0.0f) {
                if (m_warpPreviewActive) {
                    float wd = computeWarpedDuration(duration, m_warpPreviewTakeoffN,
                                                     m_warpPreviewContactN, m_warpPreviewScale);
                    if (wd > 0.0f) return std::fmod(animTime, wd) / wd;
                }
                return std::fmod(animTime, duration) / duration;
            }
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

    void AnimatedVoxelCharacter::seekAnimation(float normalizedTime) {
        if (currentClipIndex < 0 || currentClipIndex >= (int)clips.size()) return;
        float dur = clips[currentClipIndex].duration;
        if (dur <= 0.0f) return;
        animTime = std::clamp(normalizedTime, 0.0f, 1.0f) * dur;
        // updateAnimation runs fmod(time, duration) for looping clips, so
        // animTime == duration wraps to frame 0. Clamp just below to show the
        // actual last frame instead.
        if (animTime >= dur) animTime = dur - 0.0001f;
        // Cancel any in-progress blend so the scrubbed pose is clean
        isBlending = false;
        blendFactor = 1.0f;
        m_prevAnimTime = animTime;
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

    void AnimatedVoxelCharacter::playAnchoredAnimation(const glm::vec3& destinationPos,
                                                        float facingYaw,
                                                        const std::string& clipName) {
        // Resolve clip name (case-insensitive)
        std::string targetLower = clipName;
        std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), ::tolower);
        int targetIndex = -1;
        for (int i = 0; i < (int)clips.size(); ++i) {
            std::string n = clips[i].name;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            if (n == targetLower) { targetIndex = i; break; }
        }
        if (targetIndex < 0) {
            LOG_WARN("Character", "playAnchoredAnimation: clip '{}' not found", clipName);
            return;
        }

        // Cancel any sitting state
        m_isSitting = false;

        // Teleport to destination — the clip's t=0 pose visually hides this
        setPosition(destinationPos);
        m_anchorPos       = destinationPos;
        m_anchorYaw       = facingYaw;
        m_anchorClipIndex = targetIndex;
        currentYaw        = facingYaw;
        m_kinVelocity     = glm::vec3(0.0f);

        // Switch clip
        previousClipIndex = currentClipIndex;
        previousAnimTime  = animTime;
        currentClipIndex  = targetIndex;
        animTime          = 0.0f;
        isBlending        = false;  // hard cut — t=0 pose hides it
        m_prevClipIndex   = targetIndex;  // prevent first-frame delta spike
        if (!skeleton.bones.empty())
            m_prevRootPos = skeleton.bones[0].currentPosition;

        m_isAnchoredAnim  = true;
        LOG_DEBUG("Character", "playAnchoredAnimation: '{}' at ({:.2f},{:.2f},{:.2f})",
                  clipName, destinationPos.x, destinationPos.y, destinationPos.z);
    }

    void AnimatedVoxelCharacter::playClipFromPosition(const glm::vec3& startPos, float facingYaw,
                                                      const std::string& clipName) {
        // Resolve clip index (case-insensitive)
        std::string targetLower = clipName;
        std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), ::tolower);
        int targetIndex = -1;
        for (int i = 0; i < (int)clips.size(); ++i) {
            std::string n = clips[i].name;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            if (n == targetLower) { targetIndex = i; break; }
        }
        if (targetIndex < 0) {
            LOG_WARN("Character", "playClipFromPosition: clip '{}' not found", clipName);
            return;
        }

        // Cancel anchored/sitting states
        m_isAnchoredAnim = false;
        m_isSitting      = false;

        // Teleport — setPosition zeros m_kinVelocity so gravity starts fresh
        setPosition(startPos);
        currentYaw = facingYaw;

        // Switch clip from t=0, no blend (hard cut)
        previousClipIndex = currentClipIndex;
        previousAnimTime  = animTime;
        currentClipIndex  = targetIndex;
        animTime          = 0.0f;
        isBlending        = false;
        m_prevClipIndex   = targetIndex;
        if (!skeleton.bones.empty())
            m_prevRootPos = skeleton.bones[0].currentPosition;

        currentState = AnimatedCharacterState::Preview;
        LOG_DEBUG("Character", "playClipFromPosition: '{}' from ({:.2f},{:.2f},{:.2f})",
                  clipName, startPos.x, startPos.y, startPos.z);
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
        worldPosition      = pos;
        m_kinVelocity      = glm::vec3(0.0f);
        m_kinGrounded      = false;       // re-evaluate grounded state after teleport
        m_stepGlideTargetY = -1.0e30f;   // cancel any in-progress terrain glide
        m_footPosValid     = false;       // invalidate foot velocity cache
        m_terrainIKBlend   = 0.0f;        // reset terrain IK transition blend
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
                // Player can interrupt the fall animation (e.g. for a long jump_down)
                // once past the committed takeoff point
                else if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
                    const auto& clip = clips[currentClipIndex];
                    if (clip.interruptible && currentAnimDuration > 0.0f &&
                        stateTimer / currentAnimDuration >= clip.interruptAfter &&
                        (glm::abs(currentForwardInput) > 0.1f || glm::abs(currentStrafeInput) > 0.1f)) {
                        currentState = AnimatedCharacterState::Walk;
                        stateTimer = 0.0f;
                    }
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
                // Movement input can cancel attack early once past the interruptAfter point
                else if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
                    const auto& clip = clips[currentClipIndex];
                    if (clip.interruptible && currentAnimDuration > 0.0f &&
                        stateTimer / currentAnimDuration >= clip.interruptAfter &&
                        (glm::abs(currentForwardInput) > 0.1f || glm::abs(currentStrafeInput) > 0.1f)) {
                        currentState = AnimatedCharacterState::Walk;
                        stateTimer = 0.0f;
                        m_hitFrameFired = false;
                    }
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

        // --- Anchored one-shot animation: bypass physics, freeze position ---
        if (m_isAnchoredAnim) {
            worldPosition = m_anchorPos;
            m_kinVelocity = glm::vec3(0.0f);
            currentYaw    = m_anchorYaw;
            // Release when clip finishes
            if (m_anchorClipIndex >= 0 && m_anchorClipIndex < (int)clips.size() &&
                animTime >= clips[m_anchorClipIndex].duration) {
                m_isAnchoredAnim = false;
                // If kinematic was frozen before (e.g. anim editor), restore freeze
                // so the character doesn't fall after the clip ends
                if (m_kinFrozen) m_kinVelocity = glm::vec3(0.0f);
                currentState     = AnimatedCharacterState::Idle;
                LOG_DEBUG("Character", "playAnchoredAnimation: clip finished, resuming Idle");
            }
            goto animate_and_render;
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
        float evalTime = animTime; // may be remapped for warp preview
        if (currentClipIndex >= 0 && currentClipIndex < clips.size()) {
            if (!m_animPaused)
                animTime += deltaTime * m_playbackSpeed;
            
            // Determine looping for current animation
            bool loop = (currentState != AnimatedCharacterState::Attack &&
                         currentState != AnimatedCharacterState::Jump &&
                         currentState != AnimatedCharacterState::Crouch &&
                         currentState != AnimatedCharacterState::CrouchIdle &&
                         currentState != AnimatedCharacterState::SitDown &&
                         currentState != AnimatedCharacterState::SitStandUp);

            // Y-root-motion clips are one-shot moves (step down, jump down, etc.).
            // In Preview mode they must NOT loop — each loop wrap would fire a large
            // upward delta (end_Y → start_Y) that visually teleports the character up.
            if (currentState == AnimatedCharacterState::Preview &&
                currentClipIndex >= 0 && currentClipIndex < (int)clips.size() &&
                clips[currentClipIndex].useRootMotion &&
                clips[currentClipIndex].rootMotionAxes.y) {
                loop = false;
            }
            
            // Manual clamp for non-looping animations
            if (!loop && animTime > clips[currentClipIndex].duration) {
                animTime = clips[currentClipIndex].duration;
            }
            
            // Special case for CrouchIdle
            if (currentState == AnimatedCharacterState::CrouchIdle) {
                animTime = clips[currentClipIndex].duration;
            }

            // Phase-based temporal warp: remap animTime so only the air phase stretches.
            if (m_warpPreviewActive) {
                float dur = clips[currentClipIndex].duration;
                float wd = computeWarpedDuration(dur, m_warpPreviewTakeoffN,
                                                  m_warpPreviewContactN, m_warpPreviewScale);
                if (wd > 0.0f) {
                    float t = loop ? std::fmod(animTime, wd) : std::min(animTime, wd);
                    evalTime = remapToAuthored(t, dur, m_warpPreviewTakeoffN,
                                               m_warpPreviewContactN, m_warpPreviewScale);
                }
            }

            float prevAnimTimeSnapshot = m_prevAnimTime;

            if (isBlending && previousClipIndex >= 0 && previousClipIndex < clips.size()) {
                blendFactor += deltaTime / blendDuration;
                if (blendFactor >= 1.0f) {
                    blendFactor = 1.0f;
                    isBlending = false;
                    animSystem.updateAnimation(skeleton, clips[currentClipIndex], evalTime, loop);
                } else {
                    // Blend with previous animation
                    // We freeze the previous animation at the transition point to avoid it looping unexpectedly
                    // or jumping to start if it finished.
                    bool prevLoop = true;
                    animSystem.blendAnimation(skeleton,
                        clips[previousClipIndex], previousAnimTime, prevLoop,
                        clips[currentClipIndex], evalTime, loop,
                        blendFactor);
                }
            } else {
                animSystem.updateAnimation(skeleton, clips[currentClipIndex], evalTime, loop);
            }
            m_prevAnimTime = animTime;

            // Root motion extraction: apply root bone displacement to worldPosition
            if (!isBlending && currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
                const AnimationClip& clip = clips[currentClipIndex];

                // Update Y root motion flag so resolveKinematicMovement suppresses gravity.
                // In Preview the drive only fires when explicitly armed (Test Step Down button),
                // never during passive playback — otherwise selecting the clip kills ground snap.
                bool isActiveStairDrive = !m_animPaused && clip.stairStepHeight > 0.0f &&
                    (currentState == AnimatedCharacterState::DescendStairs ||
                     currentState == AnimatedCharacterState::ClimbStairs   ||
                     (currentState == AnimatedCharacterState::Preview && m_stairDriveActive));
                m_yRootMotionActive = (!m_animPaused && clip.useRootMotion && clip.rootMotionAxes.y)
                                   || isActiveStairDrive;

                // Clip transition: reset prevRootPos so frame-1 delta is zero (no teleport spike)
                if (currentClipIndex != m_prevClipIndex) {
                    if (!skeleton.bones.empty())
                        m_prevRootPos = skeleton.bones[0].currentPosition;
                    m_prevClipIndex = currentClipIndex;
                }

                // Capture animated root position AFTER update, BEFORE any stripping
                glm::vec3 currentRootAnimated = skeleton.bones.empty() ? glm::vec3(0.0f) : skeleton.bones[0].currentPosition;
                if (clip.useRootMotion && !skeleton.bones.empty()) {
                    // Detect animation loop wrap: animTime is monotonically increasing so
                    // we can't compare it directly. Instead compare the fmod-wrapped eval
                    // times — when the clip restarts, the wrapped time jumps backward.
                    float clipDur = clip.duration;
                    bool loopWrapped = loop && clipDur > 0.0f &&
                        (std::fmod(animTime, clipDur) < std::fmod(prevAnimTimeSnapshot, clipDur));
                    if (!loopWrapped) {
                        // Use m_prevRootPos (animated pos from last frame) as the delta base.
                        // rootPosBefore would have been the stripped localPosition — wrong.
                        glm::vec3 delta = currentRootAnimated - m_prevRootPos;
                        if (!clip.rootMotionAxes.x) delta.x = 0.0f;
                        if (!clip.rootMotionAxes.y) delta.y = 0.0f;
                        if (!clip.rootMotionAxes.z) delta.z = 0.0f;

                        // Rotate XZ from model space into world space by character yaw
                        float cy = cosf(currentYaw), sy = sinf(currentYaw);
                        glm::vec3 worldDelta(
                            delta.x * cy - delta.z * sy,
                            delta.y,
                            delta.x * sy + delta.z * cy
                        );

                        // Apply per-axis with collision checks (same pattern as resolveKinematicMovement)
                        auto* voxelWorld = physicsWorld ? physicsWorld->getVoxelWorld() : nullptr;
                        const glm::vec3 charHE(m_originalHalfWidth, m_originalHalfHeight - 0.05f, m_originalHalfWidth);
                        auto blocked = [&](const glm::vec3& pos) -> bool {
                            if (!voxelWorld) return false;
                            glm::vec3 c(pos.x, pos.y + m_originalHalfHeight, pos.z);
                            return voxelWorld->overlapsTerrain(c, charHE) || voxelWorld->overlapsAnyBody(c, charHE);
                        };

                        if (worldDelta.x != 0.0f) {
                            worldPosition.x += worldDelta.x;
                            if (blocked(worldPosition)) worldPosition.x -= worldDelta.x;
                        }
                        if (worldDelta.y != 0.0f) {
                            worldPosition.y += worldDelta.y;
                            // Only block upward Y (ceiling). Downward root motion is intentional
                            // (stepping off a ledge); blocking it on the departure surface would
                            // prevent any descent at all.
                            if (worldDelta.y > 0.0f && blocked(worldPosition))
                                worldPosition.y -= worldDelta.y;
                        }
                        if (worldDelta.z != 0.0f) {
                            worldPosition.z += worldDelta.z;
                            if (blocked(worldPosition)) worldPosition.z -= worldDelta.z;
                        }
                    }

                    // Save animated position BEFORE stripping so next frame has the correct delta base
                    m_prevRootPos = currentRootAnimated;

                    // Strip extracted axes from root bone so the visual doesn't double-count
                    glm::vec3& rc = skeleton.bones[0].currentPosition;
                    if (clip.rootMotionAxes.x) rc.x = skeleton.bones[0].localPosition.x;
                    if (clip.rootMotionAxes.y) rc.y = skeleton.bones[0].localPosition.y;
                    if (clip.rootMotionAxes.z) rc.z = skeleton.bones[0].localPosition.z;
                } else {
                    // Keep m_prevRootPos in sync even when root motion is inactive,
                    // so the first frame of a root-motion animation has a valid base.
                    m_prevRootPos = currentRootAnimated;
                }
            } else if (isBlending && !skeleton.bones.empty()) {
                // Keep m_prevRootPos current during blend transitions
                m_prevRootPos = skeleton.bones[0].currentPosition;
                // Y root motion is not extracted during blending
                m_yRootMotionActive = false;
            }

            // Stair step drive: smoothly move worldPosition.y over one clip pass.
            // In Preview the drive only fires when m_stairDriveActive is set (Test Step Down).
            // It auto-disarms once animTime reaches the end of the first pass.
            if (!m_animPaused && !isBlending &&
                currentClipIndex >= 0 && currentClipIndex < (int)clips.size())
            {
                const AnimationClip& stairClip = clips[currentClipIndex];
                if (stairClip.stairStepHeight > 0.0f && stairClip.duration > 0.0f) {
                    bool doDescend = (currentState == AnimatedCharacterState::DescendStairs ||
                                      (currentState == AnimatedCharacterState::Preview && m_stairDriveActive));
                    bool doClimb   = (currentState == AnimatedCharacterState::ClimbStairs);
                    if (doDescend || doClimb) {
                        if (m_stairDriveNeedsInit) {
                            m_stairDriveWorldStart = worldPosition;
                            m_stairDriveNeedsInit = false;
                        }

                        float t = stairClip.duration > 0.0f
                                ? glm::clamp(animTime / stairClip.duration, 0.0f, 1.0f)
                                : 0.0f;

                        worldPosition.y = m_stairDriveWorldStart.y +
                                          (doClimb ? t : -t) * stairClip.stairStepHeight;

                        if (m_stairDriveActive && stairClip.stairStepDepth > 0.0f) {
                            glm::vec3 fwd = getForwardDirection();
                            worldPosition.x = m_stairDriveWorldStart.x + fwd.x * t * stairClip.stairStepDepth;
                            worldPosition.z = m_stairDriveWorldStart.z + fwd.z * t * stairClip.stairStepDepth;
                        }

                        if (m_stairDriveActive && animTime >= stairClip.duration) {
                            m_stairDriveActive = false;
                        }
                    }
                }
            }
        }

        // Warp preview: push root bone Y up by a decaying offset so the character
        // appears to start higher than authored without moving the controller.
        // Offset = extraY at t=0, linearly falls to 0 at contactFrame*duration.
        if (m_warpPreviewActive && !skeleton.bones.empty() && currentClipIndex >= 0) {
            float T1 = m_warpPreviewTakeoffN  * clips[currentClipIndex].duration;
            float T2 = m_warpPreviewContactN  * clips[currentClipIndex].duration;
            float fade;
            if (evalTime <= T1) {
                fade = 1.0f; // takeoff phase: hold at full offset
            } else if (T2 > T1) {
                fade = std::max(0.0f, 1.0f - (evalTime - T1) / (T2 - T1)); // decay through air
            } else {
                fade = 0.0f;
            }
            skeleton.bones[0].currentPosition.y += m_warpPreviewExtraY * fade;
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
        // +1/9 visual lift: foot block pivots sit at the bone center; the block half-extent
        // (~1/9) would otherwise sink the visual feet into the floor.
        static constexpr float k_modelVisualLift = 0.05f;
        glm::vec3 visualOrigin = worldPosition - glm::vec3(0.0f, skeletonFootOffset_, 0.0f)
                               + glm::vec3(0.0f, k_modelVisualLift, 0.0f);
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

            glm::vec3 visualOrigin = worldPosition - glm::vec3(0.0f, skeletonFootOffset_, 0.0f)
                                   + glm::vec3(0.0f, k_modelVisualLift, 0.0f);
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
    // Foot IK
    // =========================================================================

    void AnimatedVoxelCharacter::resolveFootBoneIds() {
        auto find = [&](const std::string& name) -> int {
            auto it = skeleton.boneMap.find(name);
            return (it != skeleton.boneMap.end()) ? it->second : -1;
        };
        m_leftFoot.upLegId  = find("mixamorig:LeftUpLeg");
        m_leftFoot.legId    = find("mixamorig:LeftLeg");
        m_leftFoot.footId   = find("mixamorig:LeftFoot");
        m_rightFoot.upLegId = find("mixamorig:RightUpLeg");
        m_rightFoot.legId   = find("mixamorig:RightLeg");
        m_rightFoot.footId  = find("mixamorig:RightFoot");

        // Cache pelvis bone for body-adjustment during IK
        m_ikHipBoneId = find("mixamorig:Hips");
        if (m_ikHipBoneId < 0) {
            for (const auto& [name, id] : skeleton.boneMap) {
                std::string n = name;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n.find("hip") != std::string::npos) { m_ikHipBoneId = id; break; }
            }
        }

        m_footIKCacheReady =
            (m_leftFoot.upLegId  >= 0 && m_leftFoot.legId  >= 0 && m_leftFoot.footId  >= 0) ||
            (m_rightFoot.upLegId >= 0 && m_rightFoot.legId >= 0 && m_rightFoot.footId >= 0);
    }

    void AnimatedVoxelCharacter::resetFootLocks() {
        m_leftFootLock  = {};
        m_rightFootLock = {};
        m_ikClipIndex   = -1;
    }

    void AnimatedVoxelCharacter::applyTwoBoneIK(
        int upLegId, int legId, int footId,
        const glm::mat4& invModel, const glm::vec3& targetWorld, float blend)
    {
        auto N = (int)skeleton.bones.size();
        if (upLegId < 0 || legId < 0 || footId < 0) return;
        if (upLegId >= N || legId >= N || footId >= N) return;

        Bone& upLeg = skeleton.bones[upLegId];
        Bone& leg   = skeleton.bones[legId];

        glm::vec3 A = glm::vec3(upLeg.globalTransform[3]);                    // hip pivot (model space)
        glm::vec3 B = glm::vec3(leg.globalTransform[3]);                      // knee pivot (model space)
        glm::vec3 C = glm::vec3(skeleton.bones[footId].globalTransform[3]);   // foot pivot (model space)

        // Convert target to model space and blend with animated foot position
        glm::vec3 T = glm::vec3(invModel * glm::vec4(targetWorld, 1.0f));
        T = glm::mix(C, T, blend);
        if (glm::length(T - C) < 0.001f) return;

        float L1 = glm::length(B - A);
        float L2 = glm::length(C - B);
        float D  = glm::clamp(glm::length(T - A),
                              std::abs(L1 - L2) + 0.001f,
                              L1 + L2 - 0.001f);
        if (L1 < 0.001f || L2 < 0.001f) return;

        glm::vec3 targetDir = glm::normalize(T - A);

        // Bend axis: project existing knee direction out of the target direction
        glm::vec3 kneeHint = B - A;
        glm::vec3 kneePerp = kneeHint - glm::dot(kneeHint, targetDir) * targetDir;
        if (glm::length(kneePerp) < 0.001f) {
            kneePerp = glm::cross(targetDir, glm::vec3(0, 1, 0));
            if (glm::length(kneePerp) < 0.001f)
                kneePerp = glm::cross(targetDir, glm::vec3(1, 0, 0));
        }
        glm::vec3 bendAxis = glm::normalize(glm::cross(targetDir, glm::normalize(kneePerp)));

        // Law of cosines — angle at hip
        float cosA = glm::clamp((L1*L1 + D*D - L2*L2) / (2.0f * L1 * D), -1.0f, 1.0f);
        float sinA = std::sqrt(1.0f - cosA * cosA);
        glm::vec3 newKneePos = A + L1 * glm::normalize(targetDir * cosA + glm::cross(bendAxis, targetDir) * sinA);

        // --- Update upLeg (hip→knee direction) ---
        glm::vec3 oldKneeDir = glm::normalize(B - A);
        glm::vec3 newKneeDir = glm::normalize(newKneePos - A);

        glm::quat upLegWorldRot = glm::normalize(glm::quat_cast(upLeg.globalTransform));
        glm::quat deltaUpLeg{1, 0, 0, 0};
        if (glm::dot(oldKneeDir, newKneeDir) < 0.9999f)
            deltaUpLeg = glm::rotation(oldKneeDir, newKneeDir);

        int parentId = upLeg.parentId;
        glm::quat parentWorldRot{1, 0, 0, 0};
        if (parentId >= 0 && parentId < N)
            parentWorldRot = glm::normalize(glm::quat_cast(skeleton.bones[parentId].globalTransform));

        upLeg.currentRotation = glm::normalize(
            glm::inverse(parentWorldRot) * deltaUpLeg * parentWorldRot * upLeg.currentRotation);

        // --- Update leg (knee→foot direction) ---
        // After upLeg rotation, the foot moves with the chain: C' = newKneePos + deltaUpLeg*(C-B)
        glm::vec3 C_moved    = newKneePos + (deltaUpLeg * (C - B));
        glm::vec3 oldFootDir = glm::normalize(C_moved - newKneePos);
        glm::vec3 newFootDir = glm::normalize(T - newKneePos);

        if (glm::dot(oldFootDir, newFootDir) < 0.9999f) {
            glm::quat deltaLeg         = glm::rotation(oldFootDir, newFootDir);
            glm::quat legParentNewRot  = glm::normalize(deltaUpLeg * upLegWorldRot);
            leg.currentRotation = glm::normalize(
                glm::inverse(legParentNewRot) * deltaLeg * legParentNewRot * leg.currentRotation);
        }
    }

    void AnimatedVoxelCharacter::applyIKCorrections(float deltaTime) {
        if (!m_footIKEnabled) return;
        auto* voxelWorld = physicsWorld ? physicsWorld->getVoxelWorld() : nullptr;
        if (!voxelWorld) return;

        if (!m_footIKCacheReady)
            resolveFootBoneIds();
        if (!m_footIKCacheReady) return;

        // Per-clip foot planting knobs — fall back to struct defaults if no clip loaded.
        // surfaceReach: how close the foot must be to a surface before the lock engages.
        //   Default 0.111 (1 microcube). Stair clips use 0.333 (1 subcube = the step height / 2).
        // bodyRange: max pelvis vertical shift to help legs reach locked foot positions.
        //   Default 0.111. Stair clips use 0.222.
        float surfaceReach = 0.111f;
        float bodyRange    = 0.111f;
        bool  isStairClip  = false;
        if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
            const auto& clip = clips[currentClipIndex];
            surfaceReach = clip.footIKSurfaceReach;
            bodyRange    = clip.footIKBodyRange;
            isStairClip  = (clip.clipType == "stair");
        }

        // Ramp the global IK blend in/out based on character state.
        // DescendStairs/ClimbStairs skip the ground snap (so m_kinVelocity.y accumulates
        // from gravity) — explicitly keep the blend active for those states.
        bool shouldApply = (m_kinVelocity.y > -2.0f ||
                            currentState == AnimatedCharacterState::DescendStairs ||
                            currentState == AnimatedCharacterState::ClimbStairs) &&
                           currentState != AnimatedCharacterState::Jump &&
                           currentState != AnimatedCharacterState::Fall &&
                           currentState != AnimatedCharacterState::Land;
        const float blendSpeed = 8.0f;
        if (shouldApply)
            m_footIKBlend = std::min(1.0f, m_footIKBlend + deltaTime * blendSpeed);
        else
            m_footIKBlend = std::max(0.0f, m_footIKBlend - deltaTime * blendSpeed * 2.0f);

        if (m_footIKBlend < 0.001f) {
            // Blend going to zero — also clear any stale foot locks
            m_leftFootLock  = {};
            m_rightFootLock = {};
            return;
        }

        // Reset foot locks when the clip changes or the animation wraps (loop detected).
        bool clipChanged = (currentClipIndex != m_ikClipIndex);
        bool animWrapped = (animTime < m_ikPrevAnimTime - 0.1f);
        if (clipChanged || animWrapped) {
            m_leftFootLock  = {};
            m_rightFootLock = {};
            m_ikClipIndex   = currentClipIndex;
        }
        m_ikPrevAnimTime = animTime;

        // Model matrix — reconstructed identically to the render sync below the IK hook.
        glm::vec3 visualOrigin = worldPosition - glm::vec3(0.0f, skeletonFootOffset_, 0.0f);
        glm::mat4 modelMatrix  = glm::translate(glm::mat4(1.0f), visualOrigin);
        modelMatrix = glm::rotate(modelMatrix, currentYaw, glm::vec3(0, 1, 0));
        glm::mat4 invModel = glm::inverse(modelMatrix);

        const bool hasViz = m_raycastVisualizer && m_raycastVisualizer->isEnabled();

        // Helper: get foot world position from current (possibly IK-modified) skeleton.
        auto footWorldPos = [&](const FootIKBones& fb) -> glm::vec3 {
            glm::vec3 m = glm::vec3(skeleton.bones[fb.footId].globalTransform[3]);
            return glm::vec3(modelMatrix * glm::vec4(m, 1.0f));
        };

        // Cache pre-IK foot positions and compute per-foot swing flags.
        // Pre-IK positions accurately reflect animation velocity; comparing them to the
        // previous frame's pre-IK positions gives a clean upward-velocity estimate.
        // If the foot is rising faster than 0.4 units/s it is in the swing phase and
        // should not be pulled back down to the terrain.
        bool leftSwing  = false;
        bool rightSwing = false;
        if (m_leftFoot.footId  >= 0 && m_leftFoot.footId  < (int)skeleton.bones.size() &&
            m_rightFoot.footId >= 0 && m_rightFoot.footId < (int)skeleton.bones.size()) {
            glm::vec3 lfw = footWorldPos(m_leftFoot);
            glm::vec3 rfw = footWorldPos(m_rightFoot);
            if (m_footPosValid) {
                float lVelY = (lfw.y - m_prevLeftFootWorld.y)  / std::max(deltaTime, 0.001f);
                float rVelY = (rfw.y - m_prevRightFootWorld.y) / std::max(deltaTime, 0.001f);
                leftSwing  = (lVelY > 0.4f);
                rightSwing = (rVelY > 0.4f);
            }
            m_prevLeftFootWorld  = lfw;
            m_prevRightFootWorld = rfw;
            m_footPosValid = true;
        }

        // Helper: search for the first solid surface beneath a world-space XZ position.
        // Returns world Y of the surface, or < -1e8 if none found within the search cone.
        auto findSurface = [&](const glm::vec3& footWorld) -> float {
            const float searchAbove = surfaceReach + 0.1f;
            const float searchDepth = surfaceReach * 2.0f + 0.5f; // extra depth for stair cases
            return voxelWorld->findGroundY(
                {footWorld.x, footWorld.y + searchAbove, footWorld.z}, 0.05f, searchDepth);
        };

        // ------------------------------------------------------------------
        // STAIR MODE — proximity-triggered persistent foot locking.
        //
        // As the stair drive lowers the capsule, each foot eventually comes
        // within surfaceReach of a step surface.  At that moment the foot's
        // world Y is locked to that surface.  From then on, 2-bone IK holds
        // the foot at the locked Y while the body continues descending —
        // producing the characteristic "stepping" motion rather than a
        // linear slide.  The lock releases only when the clip changes.
        // ------------------------------------------------------------------
        if (isStairClip) {
            // Normalized animation time — used for contact-frame triggers and logging.
            float normT = 0.0f;
            float cf1   = 0.0f;
            float cf2   = 0.0f;
            if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
                const auto& curClip = clips[currentClipIndex];
                if (curClip.duration > 0.0f)
                    normT = glm::clamp(animTime / curClip.duration, 0.0f, 1.0f);
                cf1 = curClip.contactFrame1;
                cf2 = curClip.contactFrame2;
            }

            // Lock foot when contact frame is reached (or proximity fallback when cf == 0).
            auto tryLockFoot = [&](const FootIKBones& fb, FootLockState& lock, float cf) {
                if (lock.active) return;
                if (fb.footId < 0 || fb.footId >= (int)skeleton.bones.size()) return;
                bool shouldTry = false;
                if (cf > 0.0f) {
                    shouldTry = (normT >= cf);
                } else {
                    glm::vec3 fw  = footWorldPos(fb);
                    float groundY = findSurface(fw);
                    if (groundY < -1e8f) return;
                    float correction = groundY - fw.y;
                    shouldTry = (correction < 0.01f && correction >= -surfaceReach);
                }
                if (!shouldTry) return;
                glm::vec3 fw  = footWorldPos(fb);
                float groundY = findSurface(fw);
                if (groundY < -1e8f) return;
                lock.active    = true;
                lock.lockedY   = groundY;
                lock.lockBlend = 0.0f;
            };

            tryLockFoot(m_leftFoot,  m_leftFootLock,  cf1);
            tryLockFoot(m_rightFoot, m_rightFootLock, cf2);

            // --- Per-frame stair IK diagnostic log ---
            // Logs capsule Y, both foot world Ys, lock state, and nearest surface Y.
            // Read these logs to understand what the foot is doing relative to the steps.
            {
                static float s_lastLogT = -1.0f;
                static int   s_lastClip = -1;
                if (s_lastClip != currentClipIndex) { s_lastLogT = -1.0f; s_lastClip = currentClipIndex; }

                // Log every 0.05 normalised time (~1 log per ~0.046s at 0.9167s duration)
                if (normT - s_lastLogT >= 0.05f || normT < s_lastLogT) {
                    s_lastLogT = normT;

                    auto getFootY = [&](const FootIKBones& fb) -> float {
                        if (fb.footId < 0 || fb.footId >= (int)skeleton.bones.size()) return -999.f;
                        return footWorldPos(fb).y;
                    };
                    auto getSurfY = [&](const FootIKBones& fb) -> float {
                        if (fb.footId < 0 || fb.footId >= (int)skeleton.bones.size()) return -999.f;
                        float g = findSurface(footWorldPos(fb));
                        return g < -1e8f ? -999.f : g;
                    };

                    float lfy  = getFootY(m_leftFoot);
                    float rfy  = getFootY(m_rightFoot);
                    float lsy  = getSurfY(m_leftFoot);
                    float rsy  = getSurfY(m_rightFoot);

                    LOG_INFO_FMT("StairIK",
                        "[t=" << std::fixed << std::setprecision(3) << normT
                        << "] capsY=" << worldPosition.y
                        << " | L_foot=" << lfy
                        << " L_surf=" << lsy
                        << " L_lock=" << (m_leftFootLock.active ? m_leftFootLock.lockedY : -999.f)
                        << " | R_foot=" << rfy
                        << " R_surf=" << rsy
                        << " R_lock=" << (m_rightFootLock.active ? m_rightFootLock.lockedY : -999.f));
                }
            }

            // Advance per-foot ease-in blends (0→1 at blendSpeed).
            if (m_leftFootLock.active)
                m_leftFootLock.lockBlend  = std::min(1.0f, m_leftFootLock.lockBlend  + deltaTime * blendSpeed);
            if (m_rightFootLock.active)
                m_rightFootLock.lockBlend = std::min(1.0f, m_rightFootLock.lockBlend + deltaTime * blendSpeed);

            // Pelvis body adjustment: average the locked-foot corrections and shift
            // the hip bone by half, so legs don't over-extend.
            if (bodyRange > 0.0f && m_ikHipBoneId >= 0 &&
                m_ikHipBoneId < (int)skeleton.bones.size())
            {
                float avgCorrection = 0.0f;
                int   count = 0;
                auto addCorr = [&](const FootIKBones& fb, const FootLockState& lock) {
                    if (!lock.active || fb.footId < 0) return;
                    float c = lock.lockedY - footWorldPos(fb).y;
                    avgCorrection += c * lock.lockBlend;
                    ++count;
                };
                addCorr(m_leftFoot, m_leftFootLock);
                addCorr(m_rightFoot, m_rightFootLock);

                if (count > 0) {
                    avgCorrection /= static_cast<float>(count);
                    float shift = glm::clamp(avgCorrection * 0.5f, -bodyRange, bodyRange);
                    skeleton.bones[m_ikHipBoneId].currentPosition.y += shift * m_footIKBlend;
                    animSystem.updateGlobalTransforms(skeleton);
                    invModel = glm::inverse(modelMatrix);
                }
            }

            // Apply 2-bone IK to each locked foot.
            // Target XZ follows the animated foot (leg swings forward naturally);
            // target Y is the locked step surface.
            auto applyLocked = [&](const FootIKBones& fb, const FootLockState& lock) {
                if (!lock.active || fb.footId < 0 || fb.footId >= (int)skeleton.bones.size()) return;
                glm::vec3 fw = footWorldPos(fb);
                float blend = m_footIKBlend * lock.lockBlend;
                applyTwoBoneIK(fb.upLegId, fb.legId, fb.footId,
                               invModel, {fw.x, lock.lockedY, fw.z}, blend);
            };

            if (hasViz) {
                // Cyan square at each locked foot position for debug
                auto drawLockMarker = [&](const FootIKBones& fb, const FootLockState& lock) {
                    if (!lock.active || fb.footId < 0) return;
                    glm::vec3 fw = footWorldPos(fb);
                    glm::vec3 p(fw.x, lock.lockedY, fw.z);
                    const float s = 0.07f;
                    const glm::vec3 cyan(0.0f, 0.9f, 0.9f);
                    m_raycastVisualizer->addLine(p - glm::vec3(s,0,0), p + glm::vec3(s,0,0), cyan);
                    m_raycastVisualizer->addLine(p - glm::vec3(0,0,s), p + glm::vec3(0,0,s), cyan);
                };
                drawLockMarker(m_leftFoot,  m_leftFootLock);
                drawLockMarker(m_rightFoot, m_rightFootLock);
            }

            applyLocked(m_leftFoot,  m_leftFootLock);
            applyLocked(m_rightFoot, m_rightFootLock);

            animSystem.updateGlobalTransforms(skeleton);
            return; // stair path complete — skip proximity IK below
        }

        // ------------------------------------------------------------------
        // NORMAL MODE — per-frame proximity IK (no persistent lock).
        // Range extended to 2/9 unit for rough terrain adaptation.
        // Swing-phase gating (via pre-IK foot velocity) prevents the IK
        // from pulling a lifted foot back to the ground mid-stride.
        // ------------------------------------------------------------------

        constexpr float k_terrainReach = 2.0f / 9.0f;  // max downward foot correction

        auto sampleCorrection = [&](const FootIKBones& fb, float& outGroundY) -> std::pair<float, bool> {
            outGroundY = -1e9f;
            if (fb.footId < 0 || fb.footId >= (int)skeleton.bones.size())
                return {0.0f, false};
            glm::vec3 fw = footWorldPos(fb);

            const float searchAbove = k_terrainReach + 0.1f;
            const float searchDepth = k_terrainReach * 2.0f + 0.5f;
            float groundY = voxelWorld->findGroundY(
                {fw.x, fw.y + searchAbove, fw.z}, 0.05f, searchDepth);
            outGroundY = groundY;

            if (hasViz) {
                const glm::vec3 rayEnd(fw.x, fw.y + searchAbove - searchDepth, fw.z);
                const float fm = 0.04f;
                const glm::vec3 yel(1.0f, 1.0f, 0.0f);
                m_raycastVisualizer->addLine(fw - glm::vec3(fm,0,0), fw + glm::vec3(fm,0,0), yel);
                m_raycastVisualizer->addLine(fw - glm::vec3(0,fm,0), fw + glm::vec3(0,fm,0), yel);
                m_raycastVisualizer->addLine(fw - glm::vec3(0,0,fm), fw + glm::vec3(0,0,fm), yel);
                if (groundY < -1e8f) {
                    m_raycastVisualizer->addLine({fw.x, fw.y + searchAbove, fw.z}, rayEnd, glm::vec3(1,0.2f,0.2f));
                } else {
                    float c = groundY - fw.y;
                    bool inRange = (std::abs(c) >= 0.005f && std::abs(c) <= k_terrainReach);
                    glm::vec3 hitPt(fw.x, groundY, fw.z);
                    m_raycastVisualizer->addLine({fw.x, fw.y + searchAbove, fw.z}, hitPt, glm::vec3(0.85f,0.85f,0.85f));
                    m_raycastVisualizer->addLine(hitPt, rayEnd, glm::vec3(0.25f,0.25f,0.25f));
                    const glm::vec3 hitColor = inRange ? glm::vec3(0,1,0.3f) : glm::vec3(1,0.55f,0);
                    const float cs = 0.06f;
                    m_raycastVisualizer->addLine(hitPt-glm::vec3(cs,0,0), hitPt+glm::vec3(cs,0,0), hitColor);
                    m_raycastVisualizer->addLine(hitPt-glm::vec3(0,0,cs), hitPt+glm::vec3(0,0,cs), hitColor);
                    m_raycastVisualizer->addLine(hitPt-glm::vec3(0,cs,0), hitPt+glm::vec3(0,cs,0), hitColor);
                }
            }

            if (groundY < -1e8f) return {0.0f, false};
            float correction = groundY - fw.y;
            // Accept both upward (foot too low for a bump) and downward (foot too high) corrections.
            if (std::abs(correction) < 0.005f || std::abs(correction) > k_terrainReach) return {0.0f, false};
            return {correction, true};
        };

        float leftGroundY = -1e9f, rightGroundY = -1e9f;
        // Gate each foot behind its swing flag — don't correct a foot that is lifting.
        auto [lc, lv] = leftSwing  ? std::make_pair(0.0f, false) : sampleCorrection(m_leftFoot,  leftGroundY);
        auto [rc, rv] = rightSwing ? std::make_pair(0.0f, false) : sampleCorrection(m_rightFoot, rightGroundY);

        // Only activate terrain IK when the two feet are on meaningfully different surface
        // heights.  On flat/uniform ground both probes return the same Y — skip IK entirely
        // so it never fights the walk animation with micro-corrections.
        // Threshold: 0.05 (≈ half a microcube).  A single microcube (1/9 ≈ 0.111) clears it;
        // floating-point noise on flat voxel surfaces does not.
        bool leftSampleValid  = !leftSwing  && (leftGroundY  > -1e8f);
        bool rightSampleValid = !rightSwing && (rightGroundY > -1e8f);
        // Terrain is only considered uneven when BOTH feet have valid samples AND
        // the height difference is meaningful.  If either foot is swinging (sample
        // unavailable), default to NOT uneven — the IK must not fire during a normal
        // stride on flat ground just because one foot is in the air.
        bool terrainIsUneven  = leftSampleValid && rightSampleValid &&
                                std::abs(leftGroundY - rightGroundY) >= 0.05f;

        // Ramp a separate blend in/out so the correction fades rather than snapping.
        constexpr float kTerrainBlendRate = 8.0f;
        if (terrainIsUneven)
            m_terrainIKBlend = std::min(1.0f, m_terrainIKBlend + deltaTime * kTerrainBlendRate);
        else
            m_terrainIKBlend = std::max(0.0f, m_terrainIKBlend - deltaTime * kTerrainBlendRate);

        if (m_terrainIKBlend > 0.001f) {
            // Pelvis shift only when BOTH feet have valid in-range corrections.
            // One foot on bump + one on flat: flat foot correction is in the dead zone
            // (lv/rv = false), so pelvis stays low — knee bends first, hip follows later.
            const float ikBlend = m_footIKBlend * m_terrainIKBlend;

            if (bodyRange > 0.0f && m_ikHipBoneId >= 0 &&
                m_ikHipBoneId < (int)skeleton.bones.size() && lv && rv)
            {
                float avg   = (lc + rc) * 0.5f;
                float shift = glm::clamp(avg * 0.5f, -bodyRange, bodyRange) * ikBlend;
                skeleton.bones[m_ikHipBoneId].currentPosition.y += shift;
                animSystem.updateGlobalTransforms(skeleton);
                invModel = glm::inverse(modelMatrix);
            }

            auto doFootIK = [&](const FootIKBones& fb, float groundY) {
                if (fb.footId < 0 || fb.footId >= (int)skeleton.bones.size() || groundY < -1e8f) return;
                glm::vec3 fw = footWorldPos(fb);
                float correction = groundY - fw.y;
                if (std::abs(correction) < 0.005f || std::abs(correction) > k_terrainReach) return;
                applyTwoBoneIK(fb.upLegId, fb.legId, fb.footId,
                               invModel, {fw.x, groundY, fw.z}, ikBlend);
            };

            // Diagnostic log — throttled to ~5/s, only fires when IK is active.
            {
                static float s_ikLogTimer = 0.0f;
                s_ikLogTimer += deltaTime;
                if (s_ikLogTimer >= 0.2f) {
                    s_ikLogTimer = 0.0f;
                    glm::vec3 lfw = (m_leftFoot.footId  >= 0) ? footWorldPos(m_leftFoot)  : glm::vec3(0);
                    glm::vec3 rfw = (m_rightFoot.footId >= 0) ? footWorldPos(m_rightFoot) : glm::vec3(0);
                    LOG_INFO_FMT("TerrainIK",
                        "L=" << lfw.y << " gnd=" << leftGroundY
                        << " corr=" << (leftGroundY - lfw.y) << " lv=" << lv
                        << " | R=" << rfw.y << " gnd=" << rightGroundY
                        << " corr=" << (rightGroundY - rfw.y) << " rv=" << rv);
                }
            }

            if (!leftSwing  && leftGroundY  > -1e8f) doFootIK(m_leftFoot,  leftGroundY);
            if (!rightSwing && rightGroundY > -1e8f) doFootIK(m_rightFoot, rightGroundY);
        }

        animSystem.updateGlobalTransforms(skeleton);
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

            m_segmentBoxes.push_back({ seg.name, boneId, glm::vec3(0.0f), halfExtents, glm::vec3(0.0f), seg.isArm, false });
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

            // AABB refit: transform the 8 local-space corners through the rotation
            // so worldHalfExtents reflects the bone's current orientation.
            glm::mat3 rotMat(finalTransform);
            const glm::vec3& he = seg.halfExtents;
            glm::vec3 refitHE(0.0f);
            for (int cx = -1; cx <= 1; cx += 2)
                for (int cy = -1; cy <= 1; cy += 2)
                    for (int cz = -1; cz <= 1; cz += 2) {
                        glm::vec3 corner = rotMat * glm::vec3(cx * he.x, cy * he.y, cz * he.z);
                        refitHE = glm::max(refitHE, glm::abs(corner));
                    }
            seg.worldHalfExtents = refitHE;
        }

        // Push updated segment boxes to voxel world as kinematic obstacles.
        // Pass m_kinVelocity so the solver generates speed-proportional push impulses.
        if (auto* vw = physicsWorld ? physicsWorld->getVoxelWorld() : nullptr) {
            std::vector<Physics::VoxelDynamicsWorld::KinematicObstacle> obstacles;
            obstacles.reserve(m_segmentBoxes.size());
            for (const auto& seg : m_segmentBoxes) {
                Physics::VoxelDynamicsWorld::KinematicObstacle ob;
                ob.center      = seg.center;
                ob.halfExtents = seg.worldHalfExtents;
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
            const glm::vec3& he = seg.worldHalfExtents;

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

            glm::vec3 he = seg.worldHalfExtents;
            glm::vec3 mn = center - he;
            glm::vec3 mx = center + he;
            addWireBox(mn, mx, color);

            // Draw a cross at the bone center for clear position reference
            float r = glm::max(he.x, glm::max(he.y, he.z)) * 0.4f;
            m_raycastVisualizer->addLine(center - glm::vec3(r,0,0), center + glm::vec3(r,0,0), color);
            m_raycastVisualizer->addLine(center - glm::vec3(0,r,0), center + glm::vec3(0,r,0), color);
            m_raycastVisualizer->addLine(center - glm::vec3(0,0,r), center + glm::vec3(0,0,r), color);
        }

        // --- Root bone: draw WHITE marker so it's unambiguously visible ---
        if (!skeleton.bones.empty() && skeleton.bones[0].parentId == -1) {
            glm::vec3 visualOrigin = worldPosition - glm::vec3(0.0f, skeletonFootOffset_, 0.0f);
            glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), visualOrigin);
            modelMatrix = glm::rotate(modelMatrix, currentYaw, glm::vec3(0, 1, 0));
            float animRot = 0.0f;
            if (currentClipIndex >= 0 && currentClipIndex < static_cast<int>(clips.size())) {
                auto rit = animationRotationOffsets.find(clips[currentClipIndex].name);
                if (rit != animationRotationOffsets.end()) animRot = rit->second;
            }
            if (animRot != 0.0f)
                modelMatrix = glm::rotate(modelMatrix, glm::radians(animRot), glm::vec3(0, 1, 0));

            const Phyxel::Bone& rootBone = skeleton.bones[0];
            glm::vec3 rootWorld = glm::vec3((modelMatrix * rootBone.globalTransform)[3]);
            const glm::vec3 white{1.0f, 1.0f, 1.0f};
            constexpr float cr = 0.15f;  // cross radius
            constexpr float ce = 0.08f;  // box half-extent
            m_raycastVisualizer->addLine(rootWorld - glm::vec3(cr,0,0), rootWorld + glm::vec3(cr,0,0), white);
            m_raycastVisualizer->addLine(rootWorld - glm::vec3(0,cr,0), rootWorld + glm::vec3(0,cr,0), white);
            m_raycastVisualizer->addLine(rootWorld - glm::vec3(0,0,cr), rootWorld + glm::vec3(0,0,cr), white);
            addWireBox(rootWorld - glm::vec3(ce), rootWorld + glm::vec3(ce), white);
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
