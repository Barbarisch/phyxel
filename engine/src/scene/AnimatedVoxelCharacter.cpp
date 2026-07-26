#include "scene/AnimatedVoxelCharacter.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "core/ChunkManager.h"
#include "core/DamageSystem.h"
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

    // Update-LOD viewer state (set once per frame by the host before character updates).
    glm::vec3 AnimatedVoxelCharacter::s_viewerPos  = glm::vec3(0.0f);
    bool      AnimatedVoxelCharacter::s_viewerValid = false;
    bool      AnimatedVoxelCharacter::s_lodEnabled  = true;

    AnimatedVoxelCharacter::AnimatedVoxelCharacter(Physics::PhysicsWorld* physicsWorld, const glm::vec3& position)
        : RagdollCharacter(physicsWorld, position), worldPosition(position) {
        createController(position);
        // Deterministic per-instance LOD phase so distant characters stagger
        // their reduced-rate ticks instead of all firing on the same frame.
        static uint32_t s_lodPhaseSeq = 0;
        m_lodJitter = static_cast<float>(s_lodPhaseSeq++ % 17) / 17.0f;
    }

    AnimatedVoxelCharacter::~AnimatedVoxelCharacter() {
        clearSegmentBoxes();
        // Drop our kinematic-obstacle slot so destroyed characters stop deflecting dynamic bodies.
        if (auto* vw = physicsWorld ? physicsWorld->getVoxelWorld() : nullptr)
            vw->removeKinematicObstacles(this);
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

        // --- Phase M5: ledge mantle override ---
        // When active, gravity, ground-snap, and horizontal input are all
        // suspended; the capsule moves along a fixed start→end timeline.
        // Y is eased so the character rises first, then arcs forward (the
        // classic mantle silhouette).
        if (m_mantleActive) {
            m_mantleTime += dt;
            float t = glm::clamp(m_mantleTime / std::max(1e-3f, m_mantleDuration),
                                 0.0f, 1.0f);
            // Easing: XZ linear; Y direction-aware. For ascent (end.y > start.y)
            // Y is front-loaded (rise then walk forward — classic mantle).
            // For descent (end.y < start.y) Y is back-loaded (step out then
            // drop — classic step-down silhouette).
            bool ascending = m_mantleEnd.y >= m_mantleStart.y;
            float ty;
            if (ascending) {
                ty = (t < 0.5f) ? (t * 2.0f) * (t * 2.0f) * 0.5f
                                : 0.5f + (1.0f - (1.0f - t) * (1.0f - t) * 2.0f) * 0.5f;
            } else {
                // Back-loaded: stays high for first half, drops smoothly in second.
                ty = (t < 0.5f) ? (1.0f - (1.0f - t * 2.0f) * (1.0f - t * 2.0f)) * 0.5f
                                : 0.5f + ((t - 0.5f) * 2.0f) * ((t - 0.5f) * 2.0f) * 0.5f;
            }
            ty = glm::clamp(ty, 0.0f, 1.0f);
            worldPosition.x = glm::mix(m_mantleStart.x, m_mantleEnd.x, t);
            worldPosition.z = glm::mix(m_mantleStart.z, m_mantleEnd.z, t);
            worldPosition.y = glm::mix(m_mantleStart.y, m_mantleEnd.y, ty);
            m_kinVelocity   = glm::vec3(0.0f);
            m_kinGrounded   = false;
            if (t >= 1.0f) {
                m_mantleActive = false;
                m_mantleTime   = 0.0f;
                m_kinGrounded  = true;
            }
            // Still refresh the contact snapshot below so consumers see the
            // new position. Skip the main integrator.
            if (voxelWorld) {
                glm::vec3 fwd = getForwardDirection();
                glm::vec3 half(halfW, halfH, halfW);
                m_lastContact = sampleVoxelContact(*voxelWorld, worldPosition,
                                                   fwd, half, {});
            } else {
                m_lastContact = CharacterContact{};
            }
            return;
        }

        // --- Phase M7: ladder climb override ---
        // When on a ladder, gravity and ground-snap are suspended; XZ is
        // locked to the rail centreline; m_ladderInput drives Y velocity.
        // Hitting the top auto-mantles forward by half a voxel; hitting the
        // bottom releases the climb.
        if (m_ladderActive) {
            worldPosition.x = m_ladderRailX;
            worldPosition.z = m_ladderRailZ;
            worldPosition.y += m_ladderInput * m_ladderSpeed * dt;

            if (worldPosition.y >= m_ladderTopY) {
                // Auto-mantle off the top, in current facing direction.
                glm::vec3 fwd = getForwardDirection();
                fwd.y = 0.0f;
                float fl = glm::length(fwd);
                if (fl > 1e-4f) fwd /= fl; else fwd = glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 mStart{m_ladderRailX, m_ladderTopY, m_ladderRailZ};
                glm::vec3 mEnd  = mStart + fwd * 0.6f + glm::vec3(0.0f, 0.1f, 0.0f);
                endLadderClimb();
                beginMantle(mStart, mEnd, 0.35f);
                // Refresh contact and return — mantle path will run next tick.
                if (voxelWorld) {
                    glm::vec3 half(halfW, halfH, halfW);
                    m_lastContact = sampleVoxelContact(*voxelWorld, worldPosition,
                                                       fwd, half, {});
                }
                return;
            }
            if (worldPosition.y <= m_ladderBottomY) {
                worldPosition.y = m_ladderBottomY;
                endLadderClimb();
                m_kinGrounded = true;
                // Fall through to the regular integrator below.
            } else {
                m_kinVelocity = glm::vec3(0.0f);
                m_kinGrounded = false;
                if (voxelWorld) {
                    glm::vec3 fwd = getForwardDirection();
                    glm::vec3 half(halfW, halfH, halfW);
                    m_lastContact = sampleVoxelContact(*voxelWorld, worldPosition,
                                                       fwd, half, {});
                } else {
                    m_lastContact = CharacterContact{};
                }
                return;
            }
        }

        if (voxelWorld) {
            // FAIL LOUD: a character grounds against this world's registered terrain
            // occupancy grids. If none are registered, every ground query returns
            // "no ground" and the character silently falls through the world forever.
            // Surface that immediately instead of debugging a -300k Y later. Logs once
            // per process; includes the world pointer so an instance mismatch (chunks
            // registering into a different VoxelDynamicsWorld) is also visible.
            if (voxelWorld->gridCount() == 0) {
                static bool s_warnedNoGrids = false;
                if (!s_warnedNoGrids) {
                    s_warnedNoGrids = true;
                    LOG_ERROR_FMT("AnimatedVoxelCharacter",
                        "No terrain occupancy grids registered in physics world (voxelWorld="
                        << static_cast<const void*>(voxelWorld) << ") — characters will fall "
                        "through all terrain. Check chunk grid registration on this load path.");
                }
            }
            // Gravity and ground-snap are suppressed when Y root motion owns vertical movement
            if (!m_yRootMotionActive) {

                // Feet position at the start of this frame's vertical integration.
                // Used below for a swept ground test so a fast fall or a frame-time
                // stutter cannot tunnel through a floor the character was above.
                const float prevFeetY = worldPosition.y;

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
                bool skipGroundSnap = // DescendStairs snap disabled to prevent falling through world
                    (currentState == AnimatedCharacterState::Preview && m_stairDriveActive);
                if (!skipGroundSnap) {
                    // Swept downward probe: search from the higher of this frame's
                    // start/end Y down through where the feet ended up, so a floor
                    // crossed mid-frame is still detected regardless of fall speed or
                    // frame time. Identical to a fixed-depth probe when not descending
                    // (prevFeetY == worldPosition.y). This is the anti-tunnel guard.
                    const float sweepTop   = std::max(prevFeetY, worldPosition.y);
                    const float sweepDepth = (sweepTop - worldPosition.y) + halfH + 1.0f;
                    glm::vec3 feetPos(worldPosition.x, sweepTop, worldPosition.z);
                    // Unified ground: static terrain + dynamic bodies (furniture),
                    // excluding kinematic obstacles (character segments) -> no self-grounding.
                    float groundY = voxelWorld->groundHeight(feetPos, halfW, sweepDepth);

                    if (groundY > -1e8f) {
                        if (worldPosition.y < groundY) {
                            // If a step-up glide is already targeting this surface, let the
                            // glide handle the ascent — don't snap and cancel it. This gives
                            // the IK more frames of non-zero correction.
                            if (m_stepGlideTargetY > -1.0e29f && m_stepGlideTargetY >= groundY - 0.01f) {
                                m_kinGrounded = true; // stay grounded, glide is in control
                            } else {
                            // Sank below — hard snap (covers edge cases where glide was inactive)
                            worldPosition.y    = groundY;
                            m_stepGlideTargetY = -1.0e30f;
                            if (m_kinVelocity.y < 0.0f) m_kinVelocity.y = 0.0f;
                            m_kinGrounded = true;
                            }
                        } else if (worldPosition.y < groundY + 0.05f) {
                            // Within grounding tolerance — stay grounded
                            m_kinGrounded = true;
                        } else if (m_kinGrounded &&
                                   m_stepGlideTargetY < -1.0e29f &&
                                   worldPosition.y <= groundY + m_maxStepHeight + 0.05f &&
                                   m_kinVelocity.y <= 0.0f) {
                            // Small step-down: ground dropped away by ≤ m_maxStepHeight.
                            // Smooth glide down instead of triggering free-fall / Fall state.
                            // Guard: do NOT arm while moving upward (would cancel a jump).
                            float stepH = worldPosition.y - groundY;
                            m_stepGlideTargetY = groundY;
                            m_kinVelocity.y    = 0.0f;
                            m_kinGrounded      = true;
                            // Arm step-down IK — hold stance foot at the upper surface level
                            // while the body descends. Timer is shortened to a settle window
                            // once the glide completes.
                            if (stepH >= 1.0f / 9.0f - 0.01f) {
                                m_stepIKObstacleY   = worldPosition.y;  // upper surface
                                m_stepIKTimer       = 0.4f;
                                m_stepIKOriginalH   = -stepH;           // negative = step-down
                                m_stepIKInitialized = false;
                            }
                        } else {
                            if (m_kinGrounded)
                                LOG_INFO_FMT("GroundLost", "Drop too large: pos.y=" << worldPosition.y
                                    << " groundY=" << groundY << " drop=" << (worldPosition.y - groundY)
                                    << " maxStep=" << m_maxStepHeight
                                    << " pos=(" << worldPosition.x << "," << worldPosition.z << ")");
                            m_kinGrounded = false;
                        }
                    } else {
                        if (m_kinGrounded)
                            LOG_INFO_FMT("GroundLost", "No ground found at pos=("
                                << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z
                                << ") halfW=" << halfW << " searchDepth=" << (halfH + 1.0f));
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
                m_stepGlideTargetY  = obstTopY;
                m_stepIKObstacleY   = obstTopY;
                m_stepIKTimer       = 0.4f;
                m_stepIKOriginalH   = obstTopY - savedY;  // positive = step-up
                m_stepIKInitialized = false;
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

        // --- Voxel contact probe (Phase M1) ---
        // One frame-coherent snapshot of ground / forward-face / climb feature
        // for downstream FSM consumers (edge teeter, climb-up, climb-down) and
        // the /api/character/contact debug route.
        if (voxelWorld) {
            glm::vec3 fwd = getForwardDirection();
            glm::vec3 half(halfW, halfH, halfW);
            m_lastContact = sampleVoxelContact(*voxelWorld, worldPosition,
                                               fwd, half, {});
        } else {
            m_lastContact = CharacterContact{};
        }

        // --- Edge teeter blend (Phase M2) ---
        // When opted in, idle, grounded, and near an edge, lerp a small pelvis
        // offset away from the edge direction. Pure cosmetic — touches only
        // m_teeterAmount / m_teeterDirXZ which the render path consumes.
        {
            const float k_target = (m_enableTeeterBlend
                                    && currentState == AnimatedCharacterState::Idle
                                    && m_lastContact.groundFound
                                    && m_lastContact.nearEdgeDir != Phyxel::Scene::EdgeDir::None)
                                       ? 1.0f : 0.0f;
            // Pick "away from edge" direction (opposite of nearEdgeDir).
            glm::vec3 fwd = getForwardDirection();
            glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
            glm::vec2 away{0.0f, 0.0f};
            switch (m_lastContact.nearEdgeDir) {
                case Phyxel::Scene::EdgeDir::Forward: away = {-fwd.x, -fwd.z}; break;
                case Phyxel::Scene::EdgeDir::Back:    away = { fwd.x,  fwd.z}; break;
                case Phyxel::Scene::EdgeDir::Left:    away = { right.x, right.z}; break;
                case Phyxel::Scene::EdgeDir::Right:   away = {-right.x,-right.z}; break;
                default: break;
            }
            if (k_target > 0.5f) m_teeterDirXZ = away;
            // Critically damped lerp toward target (1.5s settle).
            float k_rate = 4.0f;
            m_teeterAmount += (k_target - m_teeterAmount) * std::min(1.0f, k_rate * dt);
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

    // Apply per-clip tuning metadata from "# clip_meta:" header comment lines.
    // Robust to string-valued and unknown keys: a bad value skips that key only,
    // never aborts the load (a thrown stof here used to kill character creation).
    static void applyClipMetaFromFile(const std::string& animFile,
                                      std::vector<Phyxel::AnimationClip>& clips) {
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
                if (k == "castFamily" || k == "castRole") continue; // family metadata: resolved via spell_anim_families.json
                float v = 0.0f;
                try { v = std::stof(kv.substr(eq + 1)); }
                catch (const std::exception&) { continue; }
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
            applyClipMetaFromFile(animFile, clips);

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
            adoptBodyPlan();

            configureAnimationFixes();
            // Apply appearance proportions set before load (race presets etc.)
            // — same order as loadFromSkeleton()/rebuildWithAppearance().
            // Without this, buildBodiesFromModel scales the boxes but the
            // skeleton and animation keys stay at standard height, so a
            // proportioned character renders as scattered fragments.
            applySkeletonProportions();
            resizeController();
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
        adoptBodyPlan();

        configureAnimationFixes();
        applySkeletonProportions();
        resizeController();
        buildBodiesFromModel();
        return true;
    }

    void AnimatedVoxelCharacter::adoptBodyPlan() {
        auto& registry = Scene::BodyPlanRegistry::instance();
        registry.ensureLoaded();
        m_bodyPlan = registry.planForSkeleton(appearance_.morphology, skeleton);
        m_bodyPlanResolved = m_bodyPlan.resolveAgainst(skeleton);
        LOG_DEBUG("Character", "adopted body plan '{}' (root={}, legs={}, segments={})",
                  m_bodyPlan.id, m_bodyPlanResolved.rootBoneId,
                  m_bodyPlanResolved.legs.size(), m_bodyPlanResolved.segments.size());
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
            float lengthScale = 1.0f, thicknessScale = 1.0f;
            if (bone.parentId == -1) {
                // ROOT (Hips): scale its bind height by heightScale so the
                // whole model space is uniformly scaled. Leaving it unscaled
                // (the old skip) hung a shrunken skeleton from a
                // standard-height root: resizeController compensated with a
                // large skeletonFootOffset_ (0.61 for a halfling) so
                // STANDING looked right, but the seat anchor math does not
                // apply that offset while the renderer subtracts it — every
                // proportioned character sat sunk by exactly that offset
                // (validity gauntlet 2026-07-22: halfling pelvis error
                // 0.725, constant per preset across all seats).
                lengthScale = appearance_.heightScale;
            } else {
                std::string nameLower = bone.name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                getLimbScales(nameLower, appearance_, lengthScale, thicknessScale);
            }

            // Scale the bone's position relative to its parent (this is what determines
            // actual limb length / body proportions)
            bone.localPosition *= lengthScale;
            bone.currentPosition = bone.localPosition;
        }

        // Scale animation position keyframes to match the new skeleton proportions.
        // Otherwise animations would snap joints back to their original unscaled positions.
        //
        // The ROOT (Hips, bone 0) channel is scaled too, by heightScale: its
        // position keys carry the body's trajectory — seated hip heights,
        // locomotion bob, jump arcs. Leaving it unscaled (the old `boneId <= 0`
        // skip) left every proportioned character animating around a
        // STANDARD-sized trajectory; the sit anchoring samples its Hips
        // references from these keys, which sank every small preset through
        // the floor when seated (validity gauntlet, 2026-07-22: constant
        // per-preset pelvis error — halfling 0.725, gnome 0.653, dwarf 0.480).
        for (auto& clip : clips) {
            for (auto& channel : clip.channels) {
                if (channel.boneId < 0 ||
                    channel.boneId >= static_cast<int>(skeleton.bones.size()))
                    continue;
                if (channel.positionKeys.empty()) continue;

                float lengthScale = 1.0f, thicknessScale = 1.0f;
                if (channel.boneId == 0) {
                    lengthScale = appearance_.heightScale;
                } else {
                    std::string nameLower = skeleton.bones[channel.boneId].name;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    getLimbScales(nameLower, appearance_, lengthScale, thicknessScale);
                }

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

    // X-span of a skeleton's torso at bind pose (joint positions). Forearm,
    // hand, and finger bones are excluded — the bind pose extends arms
    // sideways (T-pose), which would inflate the span to wingspan. The
    // upper-arm joint origin IS the shoulder socket, so "arm" bones stay
    // included and the span captures shoulderWidthScale.
    static float computeTorsoSpanX(const Skeleton& skel) {
        std::vector<glm::mat4> transforms(skel.bones.size(), glm::mat4(1.0f));
        float minX = 0.0f, maxX = 0.0f;
        bool first = true;
        for (size_t i = 0; i < skel.bones.size(); ++i) {
            const auto& bone = skel.bones[i];
            glm::mat4 local = glm::translate(glm::mat4(1.0f), bone.localPosition);
            local = local * glm::mat4_cast(bone.localRotation);
            if (bone.parentId == -1)
                transforms[i] = local;
            else if (bone.parentId >= 0 && bone.parentId < static_cast<int>(skel.bones.size()))
                transforms[i] = transforms[bone.parentId] * local;

            std::string nameLower = bone.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find("forearm") != std::string::npos ||
                nameLower.find("hand") != std::string::npos ||
                nameLower.find("thumb") != std::string::npos ||
                nameLower.find("index") != std::string::npos ||
                nameLower.find("middle") != std::string::npos ||
                nameLower.find("ring") != std::string::npos ||
                nameLower.find("pinky") != std::string::npos ||
                nameLower.find("end") != std::string::npos) {
                continue;
            }

            float x = transforms[i][3][0];
            if (first) { minX = maxX = x; first = false; }
            else { minX = std::min(minX, x); maxX = std::max(maxX, x); }
        }
        return maxX - minX;
    }

    void AnimatedVoxelCharacter::applyPostureLean() {
        if (appearance_.postureLeanDeg == 0.0f) return;

        // Collect the spine chain (Spine, Spine1, Spine2 on Mixamo rigs) and
        // distribute the total lean across it so the hunch curves naturally.
        std::vector<Bone*> spineBones;
        for (auto& bone : skeleton.bones) {
            std::string nameLower = bone.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find("spine") != std::string::npos) spineBones.push_back(&bone);
        }
        if (spineBones.empty()) return;

        const float perBoneRad =
            glm::radians(appearance_.postureLeanDeg) / static_cast<float>(spineBones.size());
        const glm::quat pitch = glm::angleAxis(perBoneRad, glm::vec3(1.0f, 0.0f, 0.0f));
        for (Bone* bone : spineBones) {
            bone->currentRotation = bone->currentRotation * pitch;
        }
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

        if (m_bodyPlan.capsule.mode == Scene::BodyPlan::Capsule::Mode::XZExtent) {
            // Long-bodied plans (quadruped/arachnid/dragon): the biped
            // torso-span ratio under-sizes a body that is long, not tall.
            // The capsule RADIUS is the body's LATERAL half-width (across the
            // spine), NOT its fore-aft length. Taking the larger horizontal
            // extent grabbed nose->tail length and produced a barrel-wide
            // capsule (wolf 0.89) that snagged the navgrid, triggering the
            // 1.5s stuck-timer and freezing patrol motion. Bind pose is
            // axis-aligned, so the width axis is whichever horizontal axis has
            // the SMALLER extent; use it (robust to facing along X or Z).
            float maxAbsX = 0.0f, maxAbsZ = 0.0f;
            for (size_t i = 0; i < skeleton.bones.size(); ++i) {
                maxAbsX = std::max(maxAbsX, std::abs(globalTransforms[i][3][0]));
                maxAbsZ = std::max(maxAbsZ, std::abs(globalTransforms[i][3][2]));
            }
            m_originalHalfWidth = glm::clamp(std::min(maxAbsX, maxAbsZ),
                                             m_bodyPlan.capsule.minHalfWidth,
                                             m_bodyPlan.capsule.maxHalfWidth);
        } else {
            // Legacy (biped) path — byte-for-byte the pre-body-plan behavior
            // (golden layer 2 pins it): capsule width scales with the
            // proportioned skeleton via the torso X-span ratio applied to the
            // legacy 0.25 base. Standard (ratio 1.0) keeps exactly the legacy
            // capsule; wide presets widen, small races narrow.
            float widthRatio = 1.0f;
            if (hasOriginalTemplate_) {
                float templateSpan = computeTorsoSpanX(originalSkeleton_);
                float scaledSpan   = computeTorsoSpanX(skeleton);
                if (templateSpan > 0.01f) widthRatio = scaledSpan / templateSpan;
            }
            m_originalHalfWidth = glm::clamp(0.25f * widthRatio, 0.12f, 0.60f);
        }

        // Step height scales with leg length (longer legs step higher).
        // Floored just above the 1/3-voxel subcube riser so every race can
        // climb generated stairs (L3 navigability contract — structures are
        // built on the subcube-step grid); capped so giants don't glide over
        // half-walls. Standard (factor 1.0) keeps the legacy 4/9 exactly.
        // The debug-UI setMaxStepHeight slider still overrides after load.
        float legFactor = appearance_.heightScale * appearance_.legLengthScale;
        m_maxStepHeight = glm::clamp((4.0f / 9.0f) * legFactor, 0.34f, 0.70f);

        LOG_INFO_FMT("Character", "resizeController: height=" << characterHeight
                      << " halfWidth=" << m_originalHalfWidth
                      << " maxStep=" << m_maxStepHeight
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

                // Belly shaping: extra depth (+half as much width) on the
                // lower torso only — hips/spine/spine1, not the chest — so
                // a gut reads as a gut instead of uniform thickness.
                const bool isBellyBone =
                    (nameLower.find("spine") != std::string::npos &&
                     nameLower.find("spine2") == std::string::npos) ||
                    nameLower.find("hip") != std::string::npos;
                const float bellyZ = isBellyBone ? appearance_.bellyScale : 1.0f;
                const float bellyX = isBellyBone
                    ? 1.0f + (appearance_.bellyScale - 1.0f) * 0.5f : 1.0f;

                if (nameLower.find("head") != std::string::npos) {
                    totalSize *= appearance_.headScale;
                    centerOffset *= appearance_.headScale;
                } else {
                    totalSize.x *= limbThickness * bellyX;
                    totalSize.y *= limbLength;
                    totalSize.z *= limbThickness * bellyZ;
                    centerOffset.y *= limbLength;
                }

                addVoxelBone(boneName, totalSize, centerOffset, glm::vec4(0,0,0,0));

                if (shapes.size() > 1) {
                    if (!parts.empty()) parts.pop_back();

                    for (const auto& shape : shapes) {
                        glm::vec3 relativeOffset = shape.offset - centerOffset;
                        // Explicit per-box color (alpha > 0) wins over the
                        // bone's appearance region color.
                        glm::vec4 color = (shape.color.a > 0.0f)
                            ? shape.color
                            : appearance_.getColorForBone(boneName);

                        glm::vec3 scaledSize = shape.size;
                        if (nameLower.find("head") != std::string::npos) {
                            scaledSize *= appearance_.headScale;
                            relativeOffset *= appearance_.headScale;
                        } else {
                            scaledSize.x *= limbThickness * bellyX;
                            scaledSize.y *= limbLength;
                            scaledSize.z *= limbThickness * bellyZ;
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
        markPartGroupsDirty();
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

    glm::vec3 AnimatedVoxelCharacter::getAttackOrigin() const {
        const glm::vec3 chest = worldPosition + glm::vec3(0.0f, 1.0f, 0.0f);
        // Visual front is +Z at yaw 0.
        const glm::vec3 fwd(std::sin(currentYaw), 0.0f, std::cos(currentYaw));
        float bestProj = -1.0e9f;
        glm::vec3 best = chest;
        bool found = false;
        for (const auto& seg : m_segmentBoxes) {
            if (seg.boneName.find("ForeArm") == std::string::npos &&
                seg.boneName.find("Hand")    == std::string::npos)
                continue;
            const float proj = glm::dot(seg.center - chest, fwd);  // how far forward the limb is
            if (proj > bestProj) { bestProj = proj; best = seg.center; found = true; }
        }
        return found ? best : chest;
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

    std::vector<AnimatedVoxelCharacter::BoneAABB> AnimatedVoxelCharacter::sampleBoneAABBsAtTime(
        const std::string& clipName, float normalizedTime, const glm::vec3& overrideWorldPos)
    {
        int clipIdx = -1;
        for (int i = 0; i < (int)clips.size(); ++i) {
            if (clips[i].name == clipName) { clipIdx = i; break; }
        }
        if (clipIdx < 0) return {};

        const AnimationClip& clip = clips[clipIdx];
        float evalTime = std::clamp(normalizedTime, 0.0f, 1.0f) * clip.duration;

        // Work on a temporary skeleton copy so the live skeleton and animSystem state
        // are never touched. Multiple back-to-back calls are safe and fully independent.
        Skeleton tempSkel = skeleton;
        animSystem.updateAnimation(tempSkel, clip, evalTime, false);
        animSystem.updateGlobalTransforms(tempSkel);

        // Swap into the live skeleton only long enough to compute segment boxes,
        // then immediately swap back. worldPosition and currentClipIndex are also
        // temporarily overridden for updateSegmentBoxes (uses currentYaw which is
        // already correct for the seated facing direction).
        auto savedBones    = skeleton.bones;
        auto savedSegBoxes = m_segmentBoxes;
        glm::vec3 savedWorldPos = worldPosition;
        int       savedClipIdx  = currentClipIndex;

        skeleton.bones   = std::move(tempSkel.bones);
        worldPosition    = overrideWorldPos;
        currentClipIndex = clipIdx;
        updateSegmentBoxes();
        auto result = getBoneAABBs();

        skeleton.bones   = std::move(savedBones);
        m_segmentBoxes   = savedSegBoxes;
        worldPosition    = savedWorldPos;
        currentClipIndex = savedClipIdx;

        return result;
    }

    int AnimatedVoxelCharacter::resolveBoneId(const std::string& boneName) const {
        // Exact match first, then common aliases: "right_hand"/"RightHand" must
        // find Mixamo's "mixamorig:RightHand". Case-insensitive, separator-blind.
        auto exact = skeleton.boneMap.find(boneName);
        if (exact != skeleton.boneMap.end()) return exact->second;

        auto canon = [](const std::string& s) {
            std::string out;
            for (char c : s) {
                if (c == '_' || c == ' ' || c == ':') continue;
                out += (char)std::tolower((unsigned char)c);
            }
            return out;
        };
        const std::string want = canon(boneName);
        for (const auto& [name, id] : skeleton.boneMap) {
            std::string have = canon(name);
            // strip the mixamorig prefix if present
            const std::string prefix = "mixamorig";
            if (have.rfind(prefix, 0) == 0) have = have.substr(prefix.size());
            if (have == want) return id;
        }
        return -1;
    }

    int AnimatedVoxelCharacter::attachToBone(const std::string& boneName, const glm::vec3& size,
                                              const glm::vec3& offset, const glm::vec4& color,
                                              const std::string& label) {
        int boneId = resolveBoneId(boneName);
        if (boneId < 0) {
            LOG_WARN("AnimatedVoxelCharacter", "Cannot attach to bone '{}' — not found", boneName);
            return -1;
        }
        int attachId = m_nextAttachmentId++;

        m_attachments.push_back({attachId, boneId, size, offset, color, label,
                                  worldPosition, glm::quat(1, 0, 0, 0)});

        // alpha == 0 marks a transform-only anchor (e.g. the held-item grip):
        // it must not create a render part — the part renderer ignores alpha
        // and would draw a tiny black box floating at the anchor position.
        if (color.a > 0.0f) {
            RagdollPart part;
            part.useDirectTransform = true;
            part.boneGroupId        = attachId + 1000;
            part.worldPos           = worldPosition;
            part.worldRot           = glm::quat(1, 0, 0, 0);
            part.scale              = size;
            part.color              = color;
            part.name               = label.empty() ? "attachment" : label;
            parts.push_back(part);
        }

        LOG_INFO("AnimatedVoxelCharacter", "Attached '{}' to bone '{}' (id {})", label, boneName, attachId);
        return attachId;
    }

    bool AnimatedVoxelCharacter::getAttachmentTransform(int attachmentId,
                                                         glm::vec3& outPos, glm::quat& outRot) const {
        for (const auto& att : m_attachments) {
            if (att.id == attachmentId) {
                outPos = att.worldPos;
                outRot = att.worldRot;
                return true;
            }
        }
        return false;
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

        // Recompute global bone transforms so segment AABBs and any downstream
        // measurement reflect the rebuilt rig's bind pose immediately
        // (without this, `updateSegmentBoxes()` reads stale `bone.globalTransform`
        // from the previous skeleton and `getBoneAABBs()` returns nonsense
        // proportions until the next animation tick).
        animSystem.updateGlobalTransforms(skeleton);

        // Refresh segment AABBs so post-rebuild measurement (used by the
        // interaction pipeline's `spawn_for_test` flow) returns the new
        // morphology immediately rather than stale boxes from before the
        // rebuild. Without this the next `getBoneAABBs()` call sees empty
        // or pre-scaling segment data until the update tick runs.
        updateSegmentBoxes();
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
        if (animTime >= dur) animTime = dur - 0.0001f;
        isBlending = false;
        blendFactor = 1.0f;
        m_prevAnimTime = animTime;
    }

    void AnimatedVoxelCharacter::seekToClip(int clipIndex, float normalizedTime) {
        if (clipIndex < 0 || clipIndex >= (int)clips.size()) return;
        currentClipIndex = clipIndex;
        float dur = clips[clipIndex].duration;
        if (dur <= 0.0f) return;
        animTime = std::clamp(normalizedTime, 0.0f, 1.0f) * dur;
        if (animTime >= dur) animTime = dur - 0.0001f;
        isBlending = false;
        blendFactor = 1.0f;
        m_prevAnimTime = animTime;
        // Force-evaluate bones immediately so the very next render shows the correct pose
        animSystem.updateAnimation(skeleton, clips[clipIndex], animTime, false);
        animSystem.updateGlobalTransforms(skeleton);
        updateSegmentBoxes();
    }

    void AnimatedVoxelCharacter::setAnimationState(AnimatedCharacterState state) {
        if (state == currentState) return;
        // The updateStateMachine tracer only sees INTERNAL transitions; external
        // aborts of a swing (any caller yanking the FSM out of Attack) were
        // invisible — log them so a dying swing names its killer.
        if (currentState == AnimatedCharacterState::Attack &&
            state != AnimatedCharacterState::Attack) {
            LOG_INFO("Character", "External Attack exit -> {} (t={} hitFired={})",
                     stateToString(state), stateTimer, m_hitFrameFired);
        }
        currentState = state;
        stateTimer = 0.0f;
        // Forcing Attack directly (API / scripted trigger) must set up a real swing
        // the SAME way the attackRequested transition does — pick the swing clip
        // from the equipped moveset and reset the hit-frame latch. Without this the
        // Attack case sees an empty m_currentAttackClip (scaledDur == 0), decides the
        // swing is "already over", and falls back to Idle in a single frame — so the
        // swing never plays and onHitFrame never fires. This makes
        // set_animation_state("Attack") a usable agent-driven attack trigger.
        if (state == AnimatedCharacterState::Attack) {
            m_hitFrameFired = false;
            m_attackQueued  = false;
            m_attackIsHeavy = false;
            if (!m_moveset.lightChain.empty()) {
                m_chainIdx = 0;
                m_currentAttackClip = m_moveset.lightChain[0];
                m_chainIdx = 1;
            } else {
                m_currentAttackClip = "attack";
            }
        }
    }

    bool AnimatedVoxelCharacter::reloadAnimations(const std::string& animFile) {
        Phyxel::Skeleton tempSkel;
        std::vector<Phyxel::AnimationClip> tempClips;
        Phyxel::VoxelModel tempModel;

        if (!animSystem.loadFromFile(animFile, tempSkel, tempClips, tempModel)) {
            return false;
        }
        applyClipMetaFromFile(animFile, tempClips); // keep hitFrameFraction/warp tuning on hot reload

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
        // Body-plan-driven: the plan's resolved root IS the hip/pelvis bone
        // (exact name, then alias fallback — same algorithm the old substring
        // scan approximated). Humanoid resolves the identical bone (index 0,
        // pinned in BodyPlanTest); wolf now finds "pelvis" instead of falling
        // through to the 0.8 default.
        m_hipBoneIndex = m_bodyPlanResolved.rootBoneId;
        m_bindPoseHipHeight = 0.8f;  // sensible fallback
        if (m_hipBoneIndex >= 0 && m_hipBoneIndex < (int)skeleton.bones.size()) {
            std::vector<glm::mat4> globalT(skeleton.bones.size(), glm::mat4(1.0f));
            for (size_t i = 0; i < skeleton.bones.size(); ++i) {
                const auto& bone = skeleton.bones[i];
                glm::mat4 local = glm::translate(glm::mat4(1.0f), bone.localPosition)
                                * glm::mat4_cast(bone.localRotation);
                globalT[i] = (bone.parentId < 0) ? local : globalT[bone.parentId] * local;
            }
            float h = globalT[m_hipBoneIndex][3][1] - skeletonFootOffset_;
            if (h >= 0.1f) m_bindPoseHipHeight = h;
        }

        // Store seat anchor and apply height offset
        m_seatSurfacePos = seatAnchorPos;
        m_seatSurfacePos.y += seatHeightOffset;
        m_seatFacingYaw   = facingYaw;
        m_isSitting       = true;
        m_lastSeatHeightOffset = seatHeightOffset;

        // Store per-state foot snap offsets
        m_sitDownOffset      = sitDownOffset;
        m_sittingIdleOffset  = sittingIdleOffset;
        m_sitStandUpOffset   = sitStandUpOffset;
        m_sitBlendDuration   = sitBlendDur;

        // ---- Cache per-clip Hips reference offsets ----
        // Sample bone 0 (Hips) at each sit clip's reference frame. The reference
        // frame is the frame where we want Hips to coincide with the seat anchor
        // in world space (i.e. the frame where the character is most clearly
        // seated on the chair):
        //   stand_to_sit:  END of clip   (character has just settled on seat)
        //   sitting_idle:  START of clip (basically constant across the loop)
        //   sit_to_stand:  START of clip (character is still seated before push)
        // Per-frame snap will subtract these from the seat anchor so that for
        // every state, the visible Hips lands at the seat at the reference frame,
        // and animates around it elsewhere in the clip.
        auto findClipByName = [&](const std::string& name) -> int {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            for (size_t i = 0; i < clips.size(); ++i) {
                std::string n = clips[i].name;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n == lower) return (int)i;
            }
            return -1;
        };
        // THE FIX for the quarantined tall-seat hop: the anchor sampling now
        // resolves the SAME clips the render path will play (mapping -> plan
        // -> legacy), instead of hardcoded names. A "SitDown" ->
        // "hop_onto_seat" mapping (or a wolf plan's "SittingIdle" ->
        // "Wolf_seat_") reaches the hips-ref sampling and the seated pose
        // anchors from the correct clip.
        int sitDownIdx     = findClipByName(clipForState(AnimatedCharacterState::SitDown, false));
        int sittingIdleIdx = findClipByName(clipForState(AnimatedCharacterState::SittingIdle, false));
        int sitStandUpIdx  = findClipByName(clipForState(AnimatedCharacterState::SitStandUp, false));
        // Sample the plan's root bone (== bone 0 on Mixamo rigs, pinned in
        // BodyPlanTest), not a literal 0 — a rig whose root isn't first in
        // the bone list would otherwise anchor sitting off a random bone.
        const int hipsBone = (m_bodyPlanResolved.rootBoneId >= 0)
                                 ? m_bodyPlanResolved.rootBoneId : 0;
        m_hipsRef_sitDown     = (sitDownIdx >= 0)
            ? sampleClipBonePos(sitDownIdx, hipsBone, clips[sitDownIdx].duration)
            : glm::vec3(0.0f);
        m_hipsRef_sittingIdle = (sittingIdleIdx >= 0)
            ? sampleClipBonePos(sittingIdleIdx, hipsBone, 0.0f)
            : glm::vec3(0.0f);
        m_hipsRef_sitStandUp  = (sitStandUpIdx >= 0)
            ? sampleClipBonePos(sitStandUpIdx, hipsBone, 0.0f)
            : glm::vec3(0.0f);
        // Keep Y component too: it represents the Hips height in model space at
        // the seated reference frame (typically ~0.45m for a humanoid). Including
        // Y in the snap places the character's pelvis bone exactly on the seat
        // surface in world space (Hips_world.y = seat.y), instead of relying on
        // a hand-tuned m_sitStandUpOffset.y.

        // Snap feet to the SitDown-anchored seated position so the very first
        // frame is already at the right spot (per-frame snap takes over after).
        float cy0 = cosf(facingYaw), sy0 = sinf(facingYaw);
        glm::vec3 anchorWorld;
        anchorWorld.x = m_hipsRef_sitDown.x * cy0 - m_hipsRef_sitDown.z * sy0;
        anchorWorld.z = m_hipsRef_sitDown.x * sy0 + m_hipsRef_sitDown.z * cy0;
        glm::vec3 initialPos = m_seatSurfacePos + m_sitStandUpOffset;
        initialPos.x -= anchorWorld.x;
        initialPos.y -= m_hipsRef_sitDown.y;
        initialPos.z -= anchorWorld.z;
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

    void AnimatedVoxelCharacter::refreshSitOffsets(const glm::vec3& sitDownOffset,
                                                    const glm::vec3& sittingIdleOffset,
                                                    const glm::vec3& sitStandUpOffset,
                                                    float sitBlendDuration,
                                                    float seatHeightOffset) {
        // Replace the cached offsets so the per-frame snap (update() reads
        // m_seatSurfacePos + m_sitStandUpOffset etc.) reflects the new values
        // on the very next tick. Only re-applies the height offset relative to
        // the previously stored value to avoid double-adding.
        m_sitDownOffset      = sitDownOffset;
        m_sittingIdleOffset  = sittingIdleOffset;
        m_sitStandUpOffset   = sitStandUpOffset;
        m_sitBlendDuration   = sitBlendDuration;
        m_seatSurfacePos.y  += (seatHeightOffset - m_lastSeatHeightOffset);
        m_lastSeatHeightOffset = seatHeightOffset;
    }

    glm::vec3 AnimatedVoxelCharacter::sampleClipBonePos(int clipIndex, int boneIndex, float time) const {
        if (clipIndex < 0 || clipIndex >= (int)clips.size()) return glm::vec3(0.0f);
        const auto& clip = clips[clipIndex];
        for (const auto& ch : clip.channels) {
            if (ch.boneId != boneIndex) continue;
            if (ch.positionKeys.empty()) return glm::vec3(0.0f);
            if (time <= ch.positionKeys.front().time) return ch.positionKeys.front().value;
            if (time >= ch.positionKeys.back().time)  return ch.positionKeys.back().value;
            for (size_t i = 1; i < ch.positionKeys.size(); ++i) {
                if (time <= ch.positionKeys[i].time) {
                    float t0 = ch.positionKeys[i-1].time;
                    float t1 = ch.positionKeys[i].time;
                    float f  = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
                    return ch.positionKeys[i-1].value * (1.0f - f) + ch.positionKeys[i].value * f;
                }
            }
            return ch.positionKeys.back().value;
        }
        // Fallback: bone's bind-pose local position
        if (boneIndex >= 0 && boneIndex < (int)skeleton.bones.size()) {
            return skeleton.bones[boneIndex].localPosition;
        }
        return glm::vec3(0.0f);
    }

    glm::vec3 AnimatedVoxelCharacter::getCameraTrackPosition() const {
        // Normal path: camera tracks worldPosition (feet) directly.
        if (!m_isSitting) return worldPosition;
        // While sitting, worldPosition snaps by the per-clip Hips anchor at each
        // sit/idle/stand transition. The rendered character does NOT visibly jump
        // (its Hips bone stays at the seat), but a camera following worldPosition
        // would lurch ~0.5m at each boundary. Track the visible Hips XZ instead:
        //   hips_world = worldPosition + rotateByYaw(currentHipsLocal)
        // Keep Y at worldPosition.y so camera height (feet+0.5) stays sane.
        if (m_hipBoneIndex < 0 || m_hipBoneIndex >= (int)skeleton.bones.size()) {
            return worldPosition;
        }
        const glm::vec3& h = skeleton.bones[m_hipBoneIndex].currentPosition;
        float cy = cosf(currentYaw), sy = sinf(currentYaw);
        glm::vec3 r = worldPosition;
        r.x += h.x * cy - h.z * sy;
        r.z += h.x * sy + h.z * cy;
        return r;
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
        if (str == "Cast") return AnimatedCharacterState::Cast;
        if (str == "Block") return AnimatedCharacterState::Block;
        if (str == "Dodge") return AnimatedCharacterState::Dodge;
        if (str == "HitReact") return AnimatedCharacterState::HitReact;
        if (str == "Death") return AnimatedCharacterState::Death;
        if (str == "KnockedOut") return AnimatedCharacterState::KnockedOut;
        if (str == "GetUp") return AnimatedCharacterState::GetUp;
        if (str == "Celebrate") return AnimatedCharacterState::Celebrate;
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
        m_kinGrounded      = false;
        m_stepGlideTargetY = -1.0e30f;
        m_footPosValid     = false;
        m_leftStep         = {};
        m_rightStep        = {};
        m_stepIKObstacleY   = -1.0e30f;
        m_stepIKTimer       = 0.0f;
        m_stepIKOriginalH   = 0.0f;
        m_stepIKInitialized = false;
        // Snap spring to new position so there's no lag after a teleport.
        m_visualBodyY    = pos.y;
        m_visualBodyVel  = 0.0f;
        m_visualBodyInit = true;
    }

    glm::vec3 AnimatedVoxelCharacter::getPosition() const {
        return worldPosition;
    }

    bool AnimatedVoxelCharacter::beginMantle(const glm::vec3& start,
                                             const glm::vec3& end,
                                             float durationSec) {
        if (m_mantleActive) return false;
        m_mantleActive   = true;
        m_mantleTime     = 0.0f;
        m_mantleDuration = std::max(0.05f, durationSec);
        m_mantleStart    = start;
        m_mantleEnd      = end;
        // Snap visual spring so the body doesn't lag below feet during the
        // mantle (the spring normally smooths step-ups, but the mantle drives
        // the body explicitly).
        m_visualBodyY    = start.y;
        m_visualBodyVel  = 0.0f;
        m_visualBodyInit = true;
        m_kinVelocity    = glm::vec3(0.0f);
        return true;
    }

    // --- Phase M7: ladder climb ---
    bool AnimatedVoxelCharacter::beginLadderClimb(const glm::vec3& railXZ_topY,
                                                  float bottomY,
                                                  float climbSpeed) {
        if (m_ladderActive || m_mantleActive) return false;
        m_ladderActive  = true;
        m_ladderRailX   = railXZ_topY.x;
        m_ladderRailZ   = railXZ_topY.z;
        m_ladderTopY    = railXZ_topY.y;
        m_ladderBottomY = bottomY;
        m_ladderSpeed   = std::max(0.1f, climbSpeed);
        m_ladderInput   = 0.0f;
        // Snap XZ onto the rail, clamp Y into the rail extent with a small
        // inset so the boundary check doesn't immediately detach on tick 0.
        worldPosition.x = m_ladderRailX;
        worldPosition.z = m_ladderRailZ;
        const float kInset = 0.02f;
        float yMin = m_ladderBottomY + kInset;
        float yMax = m_ladderTopY    - kInset;
        if (yMax < yMin) yMax = yMin;
        worldPosition.y = glm::clamp(worldPosition.y, yMin, yMax);
        m_kinVelocity   = glm::vec3(0.0f);
        m_kinGrounded   = false;
        m_visualBodyY    = worldPosition.y;
        m_visualBodyVel  = 0.0f;
        m_visualBodyInit = true;
        return true;
    }

    void AnimatedVoxelCharacter::setLadderInput(float vertical) {
        m_ladderInput = glm::clamp(vertical, -1.0f, 1.0f);
    }

    void AnimatedVoxelCharacter::endLadderClimb() {
        if (!m_ladderActive) return;
        m_ladderActive = false;
        m_ladderInput  = 0.0f;
        m_kinVelocity  = glm::vec3(0.0f);
    }

    void AnimatedVoxelCharacter::setMoveVelocity(const glm::vec3& velocity) {
        externalVelocity = velocity;
        hasExternalVelocity = true;
    }

    void AnimatedVoxelCharacter::jump() {
        jumpRequested = true;
    }

    void AnimatedVoxelCharacter::attack() {
        lightAttack();
    }

    void AnimatedVoxelCharacter::lightAttack() {
        // Mid-swing: buffer the input; the Attack state consumes it at the
        // chain window and plays the next link.
        if (currentState == AnimatedCharacterState::Attack && !m_attackIsHeavy) {
            m_attackQueued = true;
            return;
        }
        attackRequested = true;
    }

    void AnimatedVoxelCharacter::heavyAttack() {
        if (currentState == AnimatedCharacterState::Attack) return;  // committed; no heavy chains v1
        m_heavyRequested = true;
    }

    // ---- Dodge (souls-style directional roll with i-frames) ----

    void AnimatedVoxelCharacter::dodge(const glm::vec2& dirXZ) {
        if (m_isSitting || m_isAnchoredAnim || isDerezzing()) return;
        if (!m_kinGrounded) return;  // no air-dodge in v1
        switch (currentState) {
            case AnimatedCharacterState::Jump:
            case AnimatedCharacterState::Fall:
            case AnimatedCharacterState::Land:
            case AnimatedCharacterState::Cast:
            case AnimatedCharacterState::Dodge:
            case AnimatedCharacterState::SitDown:
            case AnimatedCharacterState::SittingIdle:
            case AnimatedCharacterState::SitStandUp:
                return;  // committed / not dodge-able right now
            default: break;
        }
        m_dodgeReqDirXZ  = dirXZ;
        m_dodgeRequested = true;  // consumed by the state machine (enterDodge)
    }

    void AnimatedVoxelCharacter::dodgeFromInput() {
        // Resolve the movement input into a world XZ direction exactly the way the
        // locomotion integrator does (so a dodge goes where a walk would).
        glm::vec3 forwardDir(-std::sin(currentYaw), 0.0f, -std::cos(currentYaw));
        glm::vec3 rightDir = glm::normalize(glm::cross(forwardDir, glm::vec3(0, 1, 0)));
        float inputDir  = (currentForwardInput > 0.01f) ? 1.0f
                        : (currentForwardInput < -0.01f ? -1.0f : 0.0f);
        float strafeDir = (currentStrafeInput  > 0.01f) ? 1.0f
                        : (currentStrafeInput  < -0.01f ? -1.0f : 0.0f);
        glm::vec3 moveDir = forwardDir * inputDir - rightDir * strafeDir;
        dodge(glm::vec2(moveDir.x, moveDir.z));  // zero → dodge() backsteps
    }

    bool AnimatedVoxelCharacter::isDodgeInvulnerable() const {
        if (currentState != AnimatedCharacterState::Dodge) return false;
        const float T = std::max(0.05f, m_dodgeDuration);
        const float frac = stateTimer / T;
        return frac >= m_dodgeIFrameStartFrac && frac <= m_dodgeIFrameEndFrac;
    }

    bool AnimatedVoxelCharacter::hasClip(const std::string& name) const {
        for (const auto& c : clips) if (c.name == name) return true;
        return false;
    }

    std::string AnimatedVoxelCharacter::selectDodgeClip(const glm::vec2& worldDirXZ) const {
        // Decompose the world dodge direction into front/right relative to the
        // body facing (+Z is the visual front at yaw 0 — see anim conventions).
        const glm::vec2 front(std::sin(currentYaw), std::cos(currentYaw));
        const glm::vec2 right(std::cos(currentYaw), -std::sin(currentYaw));
        const float f = glm::dot(worldDirXZ, front);
        const float r = glm::dot(worldDirXZ, right);
        std::string name;
        if (std::abs(f) >= std::abs(r)) name = (f >= 0.0f) ? "roll_forward" : "roll_back";
        else                            name = (r >= 0.0f) ? "roll_right"   : "roll_left";
        if (hasClip(name)) return name;
        // Phase 1 fallback: the authored roll set may not exist yet — use any
        // available clip so the lunge + i-frames are still testable.
        for (const char* fb : {"roll_forward", "jump", "landing", "jump_down",
                               "boxing", "idle"}) {
            if (hasClip(fb)) return fb;
        }
        return name;  // animation system tolerates an unknown name (keeps last clip)
    }

    void AnimatedVoxelCharacter::enterDodge() {
        m_dodgeRequested = false;
        glm::vec2 dir = m_dodgeReqDirXZ;
        if (glm::length(dir) < 0.01f) {
            // Neutral input → backstep (opposite of the visual front).
            dir = glm::vec2(-std::sin(currentYaw), -std::cos(currentYaw));
        }
        const float len = glm::length(dir);
        m_dodgeDirXZ = (len > 1e-4f) ? dir / len : glm::vec2(0.0f, -1.0f);
        // Face the dodge direction so the (forward) roll clip reads correctly for
        // any direction — the no-lock-on souls behaviour (you turn into the roll).
        // The visual front is +Z at yaw 0, so yaw = atan2(dirX, dirZ). The host
        // controller suspends its camera-facing coupling while dodging.
        currentYaw = std::atan2(m_dodgeDirXZ.x, m_dodgeDirXZ.y);
        currentState = AnimatedCharacterState::Dodge;
        stateTimer = 0.0f;
        m_hitFrameFired = false;
        m_currentDodgeClip = selectDodgeClip(m_dodgeDirXZ);
        LOG_DEBUG("Character", "Dodge: dir=({:.2f},{:.2f}) clip='{}'",
                  m_dodgeDirXZ.x, m_dodgeDirXZ.y, m_currentDodgeClip);
    }

    // ---- Hit reaction ----

    std::string AnimatedVoxelCharacter::selectHitClip() {
        static const char* kHitClips[] = {"hit_head", "hit_stomach", "hit_rib"};
        // Cycle for variety, skipping any that aren't loaded.
        for (int i = 0; i < 3; ++i) {
            const char* name = kHitClips[(m_hitClipRotate + i) % 3];
            if (hasClip(name)) { m_hitClipRotate = (m_hitClipRotate + i + 1) % 3; return name; }
        }
        return "";  // none authored — HitReact will just be a brief pause
    }

    void AnimatedVoxelCharacter::hitReact(bool heavy) {
        if (m_hitReactCdTimer > 0.0f) return;                 // re-stun immunity
        if (m_isSitting || m_isAnchoredAnim || isDerezzing()) return;
        if (currentState == AnimatedCharacterState::Dodge) return;  // i-frames: not hit
        if (isIncapacitated()) return;                         // dead / down: no flinch
        m_hitReactRequested = true;
        m_hitReactHeavy     = heavy;
    }

    // ---- Death / knock-out ----

    void AnimatedVoxelCharacter::die(bool backward) {
        if (currentState == AnimatedCharacterState::Death) return;
        m_deathClip = (backward && hasClip("death_back")) ? "death_back"
                    : (hasClip("death_front") ? "death_front"
                       : (hasClip("death_back") ? "death_back" : ""));
        currentState = AnimatedCharacterState::Death;
        stateTimer = 0.0f;
        m_hitReactRequested = false;
        m_attackQueued = false;
        LOG_DEBUG("Character", "die: clip='{}'", m_deathClip);
    }

    void AnimatedVoxelCharacter::knockOut(float layDurationSec) {
        if (isIncapacitated()) return;                 // already down / dead
        if (m_isSitting || isDerezzing()) return;
        m_koLayDuration = std::max(0.5f, layDurationSec);
        currentState = AnimatedCharacterState::KnockedOut;
        stateTimer = 0.0f;
        m_hitReactRequested = false;
        m_attackQueued = false;
        LOG_DEBUG("Character", "knockOut: {:.1f}s", m_koLayDuration);
    }

    void AnimatedVoxelCharacter::reviveToIdle() {
        if (!isIncapacitated() && currentState != AnimatedCharacterState::Celebrate) return;
        currentState = AnimatedCharacterState::Idle;
        stateTimer = 0.0f;
    }

    void AnimatedVoxelCharacter::celebrate(const std::string& clip) {
        if (isIncapacitated()) return;                 // dead / down can't celebrate
        if (currentState == AnimatedCharacterState::Celebrate) return;
        m_celebrateClip = (!clip.empty() && hasClip(clip)) ? clip
                        : (hasClip("taunt") ? "taunt" : (hasClip("wave") ? "wave" : ""));
        currentState = AnimatedCharacterState::Celebrate;
        stateTimer = 0.0f;
        LOG_DEBUG("Character", "celebrate: clip='{}'", m_celebrateClip);
    }

    /// Playback rate so the long "get up" clip plays in ~3 s.
    float AnimatedVoxelCharacter::currentGetUpRate() const {
        if (currentState != AnimatedCharacterState::GetUp) return 1.0f;
        if (currentClipIndex < 0 || currentClipIndex >= (int)clips.size()) return 1.0f;
        float d = clips[currentClipIndex].duration;
        return d > 3.0f ? d / 3.0f : 1.0f;
    }

    bool AnimatedVoxelCharacter::castSpell(const std::vector<CastSegment>& segments) {
        if (segments.empty() || segments.front().clip.empty()) return false;
        if (m_isSitting || m_isAnchoredAnim || isDerezzing()) return false;
        switch (currentState) {
            case AnimatedCharacterState::Jump:
            case AnimatedCharacterState::Fall:
            case AnimatedCharacterState::Land:
            case AnimatedCharacterState::Cast:
            case AnimatedCharacterState::SitDown:
            case AnimatedCharacterState::SittingIdle:
            case AnimatedCharacterState::SitStandUp:
                return false;
            default: break;
        }

        // Validate the first clip exists; sample the FINAL clip's authored
        // release frame (hitFrameFraction carries the clip_meta releaseFrame).
        auto findClip = [this](const std::string& name) -> const Phyxel::AnimationClip* {
            for (const auto& c : clips)
                if (c.name == name) return &c;
            return nullptr;
        };
        if (!findClip(segments.front().clip)) {
            LOG_WARN("Character", "castSpell: unknown clip '{}'", segments.front().clip);
            return false;
        }
        m_castReleaseFrac = 0.5f;
        if (const auto* finalClip = findClip(segments.back().clip))
            m_castReleaseFrac = finalClip->hitFrameFraction;

        m_castSegments    = segments;
        m_castSegIdx      = 0;
        m_castLoopsLeft   = std::max(1, segments.front().loops);
        m_castReleaseFired = false;
        currentState = AnimatedCharacterState::Cast;
        stateTimer = 0.0f;
        LOG_DEBUG("Character", "castSpell: {} segment(s), first '{}', releaseFrac {:.2f}",
                  segments.size(), segments.front().clip, m_castReleaseFrac);
        return true;
    }

    void AnimatedVoxelCharacter::setCrouch(bool crouch) {
        isCrouching = crouch;
    }

    void AnimatedVoxelCharacter::setAnimationMapping(const std::string& stateName, const std::string& animName) {
        animationMapping[stateName] = animName;
    }

    void AnimatedVoxelCharacter::removeAnimationMapping(const std::string& stateName) {
        animationMapping.erase(stateName);
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
            case AnimatedCharacterState::Cast: return "Cast";
            case AnimatedCharacterState::Block: return "Block";
            case AnimatedCharacterState::Dodge: return "Dodge";
            case AnimatedCharacterState::HitReact: return "HitReact";
            case AnimatedCharacterState::Death: return "Death";
            case AnimatedCharacterState::KnockedOut: return "KnockedOut";
            case AnimatedCharacterState::GetUp: return "GetUp";
            case AnimatedCharacterState::Celebrate: return "Celebrate";
            case AnimatedCharacterState::Preview: return "Preview";
            default: return "Unknown";
        }
    }

    std::string AnimatedVoxelCharacter::clipForState(AnimatedCharacterState state,
                                                     bool isSprinting) const {
        // Layer 1: per-character animationMapping (user/race/NPC-def override).
        const std::string stateKey = stateToString(state);
        auto mapIt = animationMapping.find(stateKey);
        if (mapIt != animationMapping.end()) return mapIt->second;

        // Layer 2: body-plan clip vocabulary (creature rigs). EMPTY on the
        // humanoid plan by design — humanoids fall through to layer 3 so the
        // sprint variants and member-driven states behave exactly as before.
        auto planIt = m_bodyPlan.clipDefaults.find(stateKey);
        if (planIt != m_bodyPlan.clipDefaults.end()) return planIt->second;

        // Layer 3: the legacy humanoid switch, verbatim (pinned state-by-state
        // in ClipSelectionTest — edit that table if you edit this).
        switch (state) {
            case AnimatedCharacterState::Idle: return "idle";
            case AnimatedCharacterState::StartWalk: return "start_walking";
            case AnimatedCharacterState::Walk: return "walk";
            case AnimatedCharacterState::Run: return isSprinting ? "fast_run" : "run";
            case AnimatedCharacterState::Jump: return "jump";
            case AnimatedCharacterState::Fall: return "jump_down";
            case AnimatedCharacterState::Land: return "landing";
            case AnimatedCharacterState::Crouch: return "standing_to_crouched";
            case AnimatedCharacterState::CrouchIdle: return "crouch_idle";
            case AnimatedCharacterState::CrouchWalk: return "crouched_walking";
            case AnimatedCharacterState::StandUp: return "crouch_to_stand";
            case AnimatedCharacterState::Attack: return m_currentAttackClip;
            case AnimatedCharacterState::Block:
                return m_moveset.block.empty() ? "body_block" : m_moveset.block;
            case AnimatedCharacterState::Cast:
                return (m_castSegIdx < m_castSegments.size())
                           ? m_castSegments[m_castSegIdx].clip : "idle";
            case AnimatedCharacterState::Dodge:
                return m_currentDodgeClip.empty() ? "roll_forward" : m_currentDodgeClip;
            case AnimatedCharacterState::HitReact:
                return m_currentHitClip.empty() ? "idle" : m_currentHitClip;
            case AnimatedCharacterState::Death:
                return m_deathClip.empty() ? "idle" : m_deathClip;
            case AnimatedCharacterState::KnockedOut: return "ko_lay";
            case AnimatedCharacterState::GetUp: return "get_up";
            case AnimatedCharacterState::Celebrate:
                return m_celebrateClip.empty() ? "taunt" : m_celebrateClip;
            case AnimatedCharacterState::TurnLeft: return "left_turn";
            case AnimatedCharacterState::TurnRight: return "right_turn";
            case AnimatedCharacterState::StrafeLeft:
            case AnimatedCharacterState::WalkStrafeLeft:
                return isSprinting ? "left_strafe" : "left_strafe_walk";
            case AnimatedCharacterState::StrafeRight:
            case AnimatedCharacterState::WalkStrafeRight:
                return isSprinting ? "right_strafe" : "right_strafe_walk";
            case AnimatedCharacterState::BackwardWalk: return "walking_backward";
            case AnimatedCharacterState::StopWalk: return "female_stop_walking";
            case AnimatedCharacterState::StopRun: return "run_to_stop";
            case AnimatedCharacterState::ClimbStairs: return "stair_up";
            case AnimatedCharacterState::DescendStairs: return "stair_down";
            case AnimatedCharacterState::SitDown: return "stand_to_sit";
            case AnimatedCharacterState::SittingIdle: return "sitting_idle";
            case AnimatedCharacterState::SitStandUp: return "sit_to_stand";
            case AnimatedCharacterState::Preview: return "";
            default: return "idle";
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

        // Death / knock-out own the FSM until they finish or are revived.
        if (currentState == AnimatedCharacterState::Death) {
            return;  // terminal; pose frozen at the death clip's end (animTime clamp below)
        }
        if (currentState == AnimatedCharacterState::KnockedOut) {
            if (stateTimer >= m_koLayDuration) {     // lay-down/idle held long enough
                currentState = AnimatedCharacterState::GetUp;
                stateTimer = 0.0f;
            }
            return;
        }
        if (currentState == AnimatedCharacterState::GetUp) {
            if (stateTimer >= 3.0f) {                // get_up is rate-scaled to ~3s
                currentState = AnimatedCharacterState::Idle;
                stateTimer = 0.0f;
            }
            return;
        }
        if (currentState == AnimatedCharacterState::Celebrate) {
            return;  // loop the victory emote until reviveToIdle()
        }

        if (m_hitReactCdTimer > 0.0f) m_hitReactCdTimer -= deltaTime;

        // Hit reaction interrupts any non-dodge action (getting hit cancels your
        // swing/locomotion). Highest priority after the dodge i-frame guard.
        if (m_hitReactRequested &&
            currentState != AnimatedCharacterState::Dodge &&
            currentState != AnimatedCharacterState::HitReact) {
            m_hitReactRequested = false;
            currentState = AnimatedCharacterState::HitReact;
            stateTimer = 0.0f;
            m_attackQueued  = false;
            m_hitFrameFired = false;
            m_attackIsHeavy = false;
            m_currentHitClip = selectHitClip();
            m_hitReactCdTimer = 0.55f;  // brief immunity so rapid hits don't stun-lock
            return;
        }

        // Check vertical velocity for falling
        float verticalVel = m_kinVelocity.y;

        // --- Auto-mantle on jump near a LedgeUp (Phase M5 integration) ---
        // When the player presses jump while grounded and the contact probe
        // reports a reachable ledge in front, swap the plain jump impulse
        // for a mantle. This makes the new system player-visible without
        // touching every jump-trigger branch below.
        if (jumpRequested && m_kinGrounded && !m_mantleActive && !m_ladderActive &&
            m_lastContact.climb == ClimbFeature::LedgeUp)
        {
            glm::vec3 fwd = getForwardDirection();
            fwd.y = 0.0f;
            float fl = glm::length(fwd);
            if (fl > 1e-4f) fwd /= fl;
            float depthInward = std::max(0.25f, m_lastContact.forwardDepthClear * 0.5f);
            glm::vec3 mStart = worldPosition;
            glm::vec3 mEnd{
                m_lastContact.climbAnchorXZ.x + fwd.x * depthInward,
                m_lastContact.climbTopY,
                m_lastContact.climbAnchorXZ.z + fwd.z * depthInward,
            };
            if (beginMantle(mStart, mEnd, 0.7f)) {
                jumpRequested = false;
                currentState  = AnimatedCharacterState::Preview;  // owned by mantle
                stateTimer    = 0.0f;
                m_previewOwnedByOverride = true;
                playAnimation("climb_ladder_start");
                LOG_INFO_FMT("AutoMantle",
                    "Jump consumed by LedgeUp mantle: start=(" << mStart.x << "," << mStart.y << "," << mStart.z
                    << ") end=(" << mEnd.x << "," << mEnd.y << "," << mEnd.z << ")");
                return;  // FSM stays in Preview until mantle completes
            }
        }

        // --- Auto step-down on walking off a LedgeDown (Phase M6 integration) ---
        // When the player walks forward off a small ledge (drop fits within a
        // mantle), trigger the step-down mantle instead of a free-fall. Skip
        // for jumps (player intent is to fly), running (would look slow), and
        // very tall drops (Fall handles those naturally).
        const float k_walkOffMaxDrop = 1.6f;  // matches ContactProbeParams.maxClimbHeight
        const float k_walkOffMinDrop = 0.6f;  // matches ContactProbeParams.minLedgeDrop
        if (!jumpRequested && m_kinGrounded && !m_mantleActive && !m_ladderActive &&
            currentForwardInput < -0.1f && currentForwardInput > -0.6f &&  // walking, not running
            m_lastContact.climb == ClimbFeature::LedgeDown)
        {
            float drop = worldPosition.y - m_lastContact.climbGrabY;
            if (drop >= k_walkOffMinDrop && drop <= k_walkOffMaxDrop) {
                glm::vec3 fwd = getForwardDirection();
                fwd.y = 0.0f;
                float fl = glm::length(fwd);
                if (fl > 1e-4f) fwd /= fl;
                float depthInward = std::max(0.25f, m_lastContact.forwardDepthClear * 0.5f);
                glm::vec3 mStart = worldPosition;
                glm::vec3 mEnd{
                    m_lastContact.climbAnchorXZ.x + fwd.x * depthInward,
                    m_lastContact.climbGrabY,
                    m_lastContact.climbAnchorXZ.z + fwd.z * depthInward,
                };
                if (beginMantle(mStart, mEnd, 0.5f)) {
                    currentState = AnimatedCharacterState::Preview;
                    stateTimer   = 0.0f;
                    m_previewOwnedByOverride = true;
                    playAnimation("step_down");
                    LOG_INFO_FMT("AutoStepDown",
                        "Walk-off LedgeDown mantle: drop=" << drop
                        << " start=(" << mStart.x << "," << mStart.y << "," << mStart.z
                        << ") end=(" << mEnd.x << "," << mEnd.y << "," << mEnd.z << ")");
                    return;
                }
            }
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
                if (m_dodgeRequested) {
                    // Dodge interrupts locomotion — highest priority ground action.
                    enterDodge();
                } else if (jumpRequested) {
                    std::cout << "DEBUG: Jump requested, switching state." << std::endl;
                    currentState = AnimatedCharacterState::Jump;
                    stateTimer = 0.0f;
                    jumpRequested = false;
                    // Apply physics impulse
                    m_kinVelocity.y = 7.0f;
                } else if (m_heavyRequested || attackRequested) {
                    const bool heavy = m_heavyRequested && !m_moveset.heavy.empty();
                    currentState = AnimatedCharacterState::Attack;
                    stateTimer = 0.0f;
                    attackRequested = false;
                    m_heavyRequested = false;
                    m_attackQueued = false;
                    m_hitFrameFired = false;
                    m_attackIsHeavy = heavy;
                    if (heavy) {
                        m_currentAttackClip = m_moveset.heavy;
                    } else if (!m_moveset.lightChain.empty()) {
                        // Start the light chain from the first link.
                        m_chainIdx = 0;
                        m_currentAttackClip = m_moveset.lightChain[0];
                        m_chainIdx = 1;
                    } else {
                        m_currentAttackClip = "attack";
                    }
                } else if (m_blockHeld && !m_moveset.block.empty()) {
                    currentState = AnimatedCharacterState::Block;
                    stateTimer = 0.0f;
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
                        // Stair snap disabled: always use Walk when moving forward.
                        // DescendStairs/ClimbStairs triggered the stair drive which could
                        // teleport the character underground when walking off ledges.
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

            case AnimatedCharacterState::Attack: {
                // Attacks play at the moveset's rate: the clip's wall-clock
                // duration is duration / rate (the animTime tick is scaled too).
                const float rate = currentAttackRate();
                const float scaledDur = currentAnimDuration > 0.0f
                                            ? currentAnimDuration / rate : 0.0f;

                // Check hit frame trigger — per-clip hitFrameFraction (from
                // clip_meta) wins over the legacy character-level default.
                float hitFrac = m_hitFrameFraction;
                if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size())
                    hitFrac = clips[currentClipIndex].hitFrameFraction;
                if (!m_hitFrameFired && scaledDur > 0.0f &&
                    stateTimer / scaledDur >= hitFrac) {
                    m_hitFrameFired = true;
                    // Swing-feel diagnosis: a fire at t far below the clip's real
                    // hit time means scaledDur was stale (wrong clip duration) and
                    // the bite is happening with the arm still at windup.
                    LOG_INFO("Character", "hit frame: clip='{}' t={} scaledDur={} frac={}",
                             m_currentAttackClip, stateTimer, scaledDur, stateTimer / scaledDur);
                    if (m_onHitFrame) m_onHitFrame();
                }

                // Buffered light input chains to the next link once the swing
                // reaches its chain window (the souls-style combo feel).
                if (!m_attackIsHeavy && m_attackQueued && scaledDur > 0.0f &&
                    !m_moveset.lightChain.empty() &&
                    stateTimer / scaledDur >= (1.0f - m_moveset.chainWindowFrac)) {
                    m_attackQueued = false;
                    m_currentAttackClip =
                        m_moveset.lightChain[m_chainIdx % m_moveset.lightChain.size()];
                    ++m_chainIdx;
                    stateTimer = 0.0f;
                    animTime = 0.0f;       // same-clip chains restart the clip
                    m_hitFrameFired = false;
                    break;
                }

                // Attack is a one-shot animation; ending without a buffered
                // input resets the chain.
                if (scaledDur > 0.0f && stateTimer >= scaledDur) {
                    currentState = AnimatedCharacterState::Idle;
                    m_hitFrameFired = false;
                    m_chainIdx = 0;
                    m_attackQueued = false;
                    m_attackIsHeavy = false;
                }
                // Movement input can cancel attack early once past the interruptAfter point
                else if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
                    const auto& clip = clips[currentClipIndex];
                    if (clip.interruptible && scaledDur > 0.0f &&
                        stateTimer / scaledDur >= clip.interruptAfter &&
                        (glm::abs(currentForwardInput) > 0.1f || glm::abs(currentStrafeInput) > 0.1f)) {
                        currentState = AnimatedCharacterState::Walk;
                        stateTimer = 0.0f;
                        m_hitFrameFired = false;
                        m_chainIdx = 0;
                        m_attackQueued = false;
                        m_attackIsHeavy = false;
                    }
                }
                break;
            }

            case AnimatedCharacterState::Block:
                // A dodge cancels the guard (souls-style roll out of block).
                if (m_dodgeRequested) {
                    enterDodge();
                    break;
                }
                // Held stance: stay while the input is held, release to Idle.
                if (!m_blockHeld) {
                    currentState = AnimatedCharacterState::Idle;
                    stateTimer = 0.0f;
                }
                break;

            case AnimatedCharacterState::Dodge:
                // Committed scripted roll. The horizontal lunge is applied in the
                // movement block; here we just time out back to Idle. (Movement
                // input is buffered, not consumed, so it resumes on exit.)
                if (stateTimer >= m_dodgeDuration) {
                    currentState = AnimatedCharacterState::Idle;
                    stateTimer = 0.0f;
                    jumpRequested = false;  // don't pop a buffered jump out of a roll
                }
                break;

            case AnimatedCharacterState::HitReact: {
                // Heavy = play the full stagger; light = a brief flinch (early-out).
                float reactDur = currentAnimDuration > 0.0f ? currentAnimDuration : 0.4f;
                if (!m_hitReactHeavy) reactDur = std::min(reactDur, 0.4f);
                if (stateTimer >= reactDur) {
                    currentState = AnimatedCharacterState::Idle;
                    stateTimer = 0.0f;
                }
                break;
            }

            case AnimatedCharacterState::Cast: {
                if (m_castSegments.empty() || m_castSegIdx >= m_castSegments.size()) {
                    currentState = AnimatedCharacterState::Idle;
                    break;
                }
                // The clip plays at the segment's rate (see the animTime tick),
                // so the wall-clock duration of one play is duration / speed.
                const float castSpeed = currentCastSpeed();
                const float scaledDur = currentAnimDuration > 0.0f
                                            ? currentAnimDuration / castSpeed : 0.0f;
                const bool finalSeg = (m_castSegIdx + 1 == m_castSegments.size());

                // Release fires once, at the final segment's authored release frame.
                if (finalSeg && !m_castReleaseFired && scaledDur > 0.0f &&
                    stateTimer / scaledDur >= m_castReleaseFrac) {
                    m_castReleaseFired = true;
                    if (m_onCastRelease) m_onCastRelease();
                }

                if (scaledDur > 0.0f && stateTimer >= scaledDur) {
                    if (m_castLoopsLeft > 1) {
                        --m_castLoopsLeft;            // replay this segment (ritual loop)
                        stateTimer = 0.0f;
                        animTime = 0.0f;
                    } else if (!finalSeg) {
                        ++m_castSegIdx;               // advance to the next segment
                        m_castLoopsLeft = std::max(1, m_castSegments[m_castSegIdx].loops);
                        stateTimer = 0.0f;
                    } else {
                        // Cast complete. Safety: never swallow the release.
                        if (!m_castReleaseFired) {
                            m_castReleaseFired = true;
                            if (m_onCastRelease) m_onCastRelease();
                        }
                        m_castSegments.clear();
                        m_castSegIdx = 0;
                        currentState = AnimatedCharacterState::Idle;
                        stateTimer = 0.0f;
                    }
                }
                break;
            }

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
                    // ---- Transfer Hips delta into worldPosition ----
                    // While seated, worldPosition was anchored so that the sit_to_stand
                    // clip's t=0 Hips coincides with the seat. The clip ends with Hips
                    // ~0.5m forward of that anchor (character stood up and stepped
                    // forward). When we release the seat anchor, the idle clip's t=0
                    // Hips would render back at the seat — a visible snap. Compensate
                    // by shifting worldPosition by (currentHips - idleStartHips)
                    // rotated into world space, so idle's Hips lands at the SAME
                    // world spot the stand-up animation ended at.
                    if (!skeleton.bones.empty()) {
                        int idleIdx = -1;
                        for (size_t i = 0; i < clips.size(); ++i) {
                            std::string n = clips[i].name;
                            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                            if (n == "idle") { idleIdx = (int)i; break; }
                        }
                        // Plan root == Hips (bone 0 on Mixamo rigs, pinned in
                        // BodyPlanTest); guard for rigs with no resolvable root.
                        const int hipsBone = (m_bodyPlanResolved.rootBoneId >= 0)
                                                 ? m_bodyPlanResolved.rootBoneId : 0;
                        glm::vec3 idleStartHips = (idleIdx >= 0)
                            ? sampleClipBonePos(idleIdx, hipsBone, 0.0f)
                            : skeleton.bones[hipsBone].localPosition;
                        glm::vec3 currentHips = skeleton.bones[hipsBone].currentPosition;
                        float dx = currentHips.x - idleStartHips.x;
                        float dy = currentHips.y - idleStartHips.y;
                        float dz = currentHips.z - idleStartHips.z;
                        float cy = cosf(currentYaw), sy = sinf(currentYaw);
                        worldPosition.x += dx * cy - dz * sy;
                        worldPosition.y += dy;
                        worldPosition.z += dx * sy + dz * cy;
                    }
                    m_isSitting = false;
                    currentState = AnimatedCharacterState::Idle;
                    stateTimer = 0.0f;
                    // Defense-in-depth: if the next clip switch goes through the
                    // normal-path (non-sit) animation selection, hard-cut into
                    // idle so the bone blend doesn't smear our worldPosition
                    // compensation over the blend window.
                    m_pendingClipSnap = true;
                }
                break;

            case AnimatedCharacterState::Preview:
                // M5/M6/M7 integration: when a mantle/ladder owns this Preview
                // (started by the auto-mantle path or the ladder routes) and
                // the override has finished, return to Idle. m_previewOwnedByOverride
                // is set when we transition INTO Preview from the override and
                // cleared here when the override completes.
                if (m_previewOwnedByOverride && !m_mantleActive && !m_ladderActive) {
                    m_previewOwnedByOverride = false;
                    currentState = AnimatedCharacterState::Idle;
                    stateTimer   = 0.0f;
                    break;
                }
                // Exit preview if any input is detected
                if (glm::abs(currentForwardInput) > 0.01f ||
                    glm::abs(currentStrafeInput) > 0.01f ||
                    glm::abs(currentTurnInput) > 0.01f ||
                    jumpRequested || attackRequested || isCrouching) {
                    currentState = AnimatedCharacterState::Idle;
                    m_previewOwnedByOverride = false;
                }
                break;

            default:
                currentState = AnimatedCharacterState::Idle;
                break;
        }

        if (currentState != previousState) {
            LOG_DEBUG_FMT("Character", "State Transition: " << stateToString(previousState) << " -> " << stateToString(currentState) 
                << " (Inputs: Fwd=" << currentForwardInput << ", Strafe=" << currentStrafeInput << ")");

            // Arm the stair drive whenever entering DescendStairs or ClimbStairs so it
            // captures the current worldPosition as the start reference.
            // Without this, m_stairDriveWorldStart stays at {0,0,0} (default) and the
            // drive computes worldPosition.y = 0 - t*stairStepHeight, teleporting the
            // character underground on the first stair descent.
            if (currentState == AnimatedCharacterState::DescendStairs ||
                currentState == AnimatedCharacterState::ClimbStairs) {
                m_stairDriveNeedsInit = true;
            }
        }
    }

    void AnimatedVoxelCharacter::detectAndApplyStepUp(const glm::vec3&, float) {
        // Disabled: will be re-implemented using VoxelOccupancyGrid queries
    }

    void AnimatedVoxelCharacter::update(float deltaTime) {
        // --- Update LOD: defer distant characters to a reduced tick rate.
        // Skipped-frame delta time is banked and folded into the next real tick,
        // so movement/root-motion advance the same distance — only granularity
        // drops. Near characters (incl. the camera-anchored player) run every
        // frame. Never throttle while sitting/derezzing (one-shot sequences that
        // must observe every frame to advance cleanly).
        if (s_lodEnabled && s_viewerValid && !m_isSitting &&
            !(m_derezState && m_derezState->active)) {
            const glm::vec3 d = worldPosition - s_viewerPos;
            const float distSq = glm::dot(d, d);
            float period = 0.0f;
            if      (distSq > k_lodFarDistSq) period = k_lodFarPeriod;
            else if (distSq > k_lodMidDistSq) period = k_lodMidPeriod;
            if (period > 0.0f) {
                // Perturb the period ±20% per instance so co-located characters
                // tick on different frames (staggered, not synchronized spikes).
                period *= (0.8f + 0.4f * m_lodJitter);
                m_lodAccum += deltaTime;
                if (m_lodAccum < period) return;  // defer this frame
                deltaTime  = m_lodAccum;          // fold banked time into this tick
                m_lodAccum = 0.0f;
            }
        }

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
                sp.angularVel  = glm::vec3(
                    ((rand() % 1000) / 500.f - 1.f) * 3.0f,
                    ((rand() % 1000) / 500.f - 1.f) * 3.0f,
                    ((rand() % 1000) / 500.f - 1.f) * 3.0f);
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
        // Per-clip Hips-anchored model: for each sit state we shift worldPosition
        // by minus the clip's reference Hips offset (rotated by yaw) so that the
        // Hips bone in world space coincides with the seat anchor at every clip's
        // reference frame. This is required because Mixamo authored the three sit
        // clips with different model-space origins — e.g. stand_to_sit's end Hips
        // is at z=-0.471 while sitting_idle's start Hips is at z=+0.035. Without
        // per-clip compensation, switching between them would teleport the Hips
        // ~0.5m even though worldPosition stays still. (See sitAt() for how the
        // three m_hipsRef_* fields are sampled from the clip data.)
        //
        // Within a single clip, the Hips bone animates freely from its reference
        // frame — e.g. sit_to_stand ends 0.5m forward of its start, which renders
        // as the character standing up and stepping forward. At the SitStandUp
        // end transition (in updateStateMachine below) we transfer that forward
        // motion into worldPosition so the standing idle clip continues from the
        // visible end pose instead of snapping back to the seat.
        if (m_isSitting) {
            // (Snap deferred until AFTER updateStateMachine + clip selection so
            // it uses the post-transition state. Doing it before would cause a
            // 1-frame visible spike at every state change: the snap uses the
            // OLD state's hipsRef while the new clip's Hips pose is rendered.)
            m_kinVelocity = glm::vec3(0.0f);
            currentYaw = m_seatFacingYaw;

            // Run FSM — transitions SitDown→SittingIdle→SitStandUp→Idle
            // Guard with m_animPaused: when scrubbing via seek_ie_animation we do NOT want
            // stateTimer to accumulate and auto-transition out of the seeked state.
            if (!m_animPaused)
                updateStateMachine(deltaTime);

            // Animation selection for sitting states (this block is normally skipped by the
            // movement path, so we must do it explicitly here for clips to actually switch)
            {
                std::string targetAnim;
                // Layered selection (mapping -> plan -> legacy); sit states
                // resolve identically to the old switch-plus-override.
                targetAnim = clipForState(currentState, false);

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
                    // Sit clips (stand_to_sit, sitting_idle, sit_to_stand) all have
                    // significantly different Hips bone translations at their start
                    // and end frames (e.g. stand_to_sit ends at Hips z=-0.47 deep
                    // in the chair, sitting_idle starts at z=+0.04 sitting upright).
                    // Crossfading two such poses linearly interpolates the Hips
                    // position over the blend window, which renders as a smooth
                    // ~0.5m visual slide of the character in front of / out of
                    // the chair. Hard-cut between sit clips instead. The seat
                    // anchor (m_seatSurfacePos + m_sitStandUpOffset) keeps
                    // worldPosition stable, so the only visible change is the
                    // one-frame pose snap — much less perceptible than the
                    // 16-frame slide that the crossfade would produce.
                    if (isBlending) {
                        blendDuration = 0.0f;
                    }
                }
            }

            // ---- Post-FSM Hips-anchored snap ----
            // Now that updateStateMachine and clip selection have run, snap
            // worldPosition using the CURRENT (post-transition) state's hipsRef.
            // This guarantees that the bone evaluation at animate_and_render
            // (which uses the new clip) lines up with the seat in world space.
            //
            // Skip the snap if updateStateMachine just cleared m_isSitting (the
            // SitStandUp end transfer set worldPosition itself; re-snapping
            // would clobber it). In that case we already left the sit cycle and
            // the next frame will be on the normal physics path.
            if (m_isSitting) {
                glm::vec3 hipsRef;
                switch (currentState) {
                    case AnimatedCharacterState::SitDown:
                        hipsRef = m_hipsRef_sitDown; break;
                    case AnimatedCharacterState::SittingIdle:
                        hipsRef = m_hipsRef_sittingIdle; break;
                    case AnimatedCharacterState::SitStandUp:
                        hipsRef = m_hipsRef_sitStandUp; break;
                    default:
                        hipsRef = m_hipsRef_sittingIdle; break;
                }
                float cy = cosf(m_seatFacingYaw), sy = sinf(m_seatFacingYaw);
                float wox = hipsRef.x * cy - hipsRef.z * sy;
                float woz = hipsRef.x * sy + hipsRef.z * cy;
                glm::vec3 snapPos = m_seatSurfacePos + m_sitStandUpOffset;
                snapPos.x -= wox;
                snapPos.y -= hipsRef.y;
                snapPos.z -= woz;
                worldPosition = snapPos;
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

                // Play walk or idle animation based on speed — but NOT while an
                // action state owns the clip. External velocity (e.g. combat
                // knockback via setMoveVelocity) must not stomp a mid-swing
                // attack/cast/block back to walk/idle and reset animTime; the
                // main animation block below already drives those states. (This
                // was the attack-animation "stutter": a hit-frame knockback
                // reset the swing to idle, then back to the attack clip.)
                const bool actionStateOwnsClip =
                    currentState == AnimatedCharacterState::Attack ||
                    currentState == AnimatedCharacterState::Cast   ||
                    currentState == AnimatedCharacterState::Block;
                if (!actionStateOwnsClip) {
                    // The user/race animationMapping override wins here too —
                    // NPCs move on this external-velocity path, so without
                    // this a halfling's scamper_walk / ogre's ogre_walk
                    // mapping would only apply to player-controlled movement.
                    std::vector<std::string> candidates;
                    auto pushLayered = [&](const char* stateKey) {
                        auto mIt = animationMapping.find(stateKey);
                        if (mIt != animationMapping.end()) candidates.push_back(mIt->second);
                        // Body-plan vocabulary (wolf Walk_cycle etc.) sits
                        // between the mapping and the legacy literal list.
                        auto pIt = m_bodyPlan.clipDefaults.find(stateKey);
                        if (pIt != m_bodyPlan.clipDefaults.end()) candidates.push_back(pIt->second);
                    };
                    if (speed > 0.1f) {
                        pushLayered("Walk");
                        candidates.insert(candidates.end(),
                            {"walk", "walking", "Walk", "Walking", "unarmed_walk"});
                    } else {
                        pushLayered("Idle");
                        candidates.insert(candidates.end(),
                            {"idle", "Idle", "Standing", "standing"});
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

            // Use animation speed if available. This is the authoritative
            // source: when an .anim clip declares `Speed N`, the engine uses
            // it directly so visual stride matches translation. If we ever
            // fall through to the state-based fallbacks below, that means
            // the clip is missing its Speed line (root-motion never extracted,
            // or stripped during a re-import) — log once per clip so it's
            // noisy at runtime instead of silently wrong.
            if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
                float animSpeed = clips[currentClipIndex].speed;
                if (animSpeed > 0.1f) {
                    moveSpeed = animSpeed;
                }
            }

            // State-based fallbacks. These only take effect when the active
            // clip provided no usable speed. Each is a *visual best-guess* and
            // will probably look wrong — they exist so the character doesn't
            // freeze in place, not as a target for tuning.
            auto warnFallback = [&](const char* state, float v) {
                if (currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
                    const auto& clip = clips[currentClipIndex];
                    if (!m_warnedSpeedFallback.count(clip.name)) {
                        m_warnedSpeedFallback.insert(clip.name);
                        LOG_ERROR("Character",
                                  "Clip '{}' (state={}) has no Speed line; using fallback {} m/s. "
                                  "Add `Speed <val>` to the clip in the .anim file.",
                                  clip.name, state, v);
                    }
                }
            };

            if (currentState == AnimatedCharacterState::Walk && moveSpeed < 0.1f) {
                moveSpeed = 2.0f; warnFallback("Walk", moveSpeed);
            }
            if (currentState == AnimatedCharacterState::StartWalk && moveSpeed < 0.1f) {
                moveSpeed = 1.5f; warnFallback("StartWalk", moveSpeed);
            }
            if (currentState == AnimatedCharacterState::Run && moveSpeed < 0.1f) {
                moveSpeed = 5.0f;
                if (glm::abs(currentForwardInput) > 0.9f) moveSpeed = 8.0f;
                warnFallback("Run", moveSpeed);
            }
            if (currentState == AnimatedCharacterState::StrafeLeft || currentState == AnimatedCharacterState::StrafeRight ||
                currentState == AnimatedCharacterState::WalkStrafeLeft || currentState == AnimatedCharacterState::WalkStrafeRight) {
                if (moveSpeed < 0.1f) {
                    moveSpeed = 2.0f;
                    if (glm::abs(currentStrafeInput) > 0.6f) moveSpeed = 4.0f;
                    warnFallback("Strafe", moveSpeed);
                }
            }
            if (currentState == AnimatedCharacterState::CrouchWalk && moveSpeed < 0.1f) {
                moveSpeed = 1.5f; warnFallback("CrouchWalk", moveSpeed);
            }
            if (currentState == AnimatedCharacterState::BackwardWalk && moveSpeed < 0.1f) {
                moveSpeed = 1.5f; warnFallback("BackwardWalk", moveSpeed);
            }
            if (currentState == AnimatedCharacterState::ClimbStairs && moveSpeed < 0.1f) {
                moveSpeed = 1.5f; warnFallback("ClimbStairs", moveSpeed);
            }
            if (currentState == AnimatedCharacterState::DescendStairs && moveSpeed < 0.1f) {
                moveSpeed = 1.5f; warnFallback("DescendStairs", moveSpeed);
            }
            if (currentState == AnimatedCharacterState::StopWalk || currentState == AnimatedCharacterState::StopRun) moveSpeed = 0.5f;
            if (currentState == AnimatedCharacterState::Idle || currentState == AnimatedCharacterState::Attack ||
                currentState == AnimatedCharacterState::Cast || currentState == AnimatedCharacterState::Block ||
                currentState == AnimatedCharacterState::HitReact ||
                currentState == AnimatedCharacterState::Death || currentState == AnimatedCharacterState::KnockedOut ||
                currentState == AnimatedCharacterState::GetUp || currentState == AnimatedCharacterState::Celebrate ||
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

            // Dodge overrides input-driven movement with a scripted lunge along
            // the locked dodge direction. Triangular ease-out (fast start → slow
            // settle): v(t)=v0*(1-t), whose integral over [0,T] is v0*T/2, so
            // v0 = 2*distance/T gives exactly the authored travel distance.
            // Collision + gravity still apply via resolveKinematicMovement.
            if (currentState == AnimatedCharacterState::Dodge) {
                const float T  = std::max(0.05f, m_dodgeDuration);
                const float t  = glm::clamp(stateTimer / T, 0.0f, 1.0f);
                const float v0 = 2.0f * m_dodgeDistance / T;
                const float v  = v0 * (1.0f - t);
                moveVel = glm::vec3(m_dodgeDirXZ.x, 0.0f, m_dodgeDirXZ.y) * v;
            }

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

            // Layered selection: mapping -> body-plan clipDefaults -> legacy
            // switch (all moved verbatim into clipForState).
            targetAnim = clipForState(currentState, isSprinting);

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

                    // Consume pending-snap flag set when leaving the sit cycle.
                    // See header for rationale: sit_to_stand's end Hips differs
                    // from idle's start Hips by ~0.5m, and a smooth blend renders
                    // as a visible slide.
                    if (m_pendingClipSnap && isBlending) {
                        isBlending = false;
                        blendFactor = 0.0f;
                    }
                    m_pendingClipSnap = false;
                }
            }
            } // end !usedExternalVelocity
        }
        } // end movement block

        animate_and_render:
        float evalTime = animTime; // may be remapped for warp preview
        if (currentClipIndex >= 0 && currentClipIndex < clips.size()) {
            if (!m_animPaused)
                animTime += deltaTime * m_playbackSpeed * currentCastSpeed() * currentAttackRate() * currentDodgeRate() * currentGetUpRate();
            // Death/GetUp play once and hold their final pose (no loop-wrap).
            if ((currentState == AnimatedCharacterState::Death ||
                 currentState == AnimatedCharacterState::GetUp) &&
                currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
                const float d = clips[currentClipIndex].duration;
                if (d > 0.0f && animTime > d) animTime = d;
            }

            // Held guard: freeze partway into the block clip (the clip's end
            // usually returns to rest, which is not a guard pose).
            if (currentState == AnimatedCharacterState::Block) {
                const float holdAt = clips[currentClipIndex].duration *
                                     std::clamp(m_moveset.blockHoldFrac, 0.05f, 1.0f);
                if (animTime > holdAt) animTime = holdAt;
            }

            // Determine looping for current animation
            // (Cast segments are one-shots; ritual loops are re-armed by the
            //  Cast state machine resetting animTime per play.)
            bool loop = (currentState != AnimatedCharacterState::Attack &&
                         currentState != AnimatedCharacterState::Cast &&
                         currentState != AnimatedCharacterState::Block &&
                         currentState != AnimatedCharacterState::Jump &&
                         currentState != AnimatedCharacterState::Crouch &&
                         currentState != AnimatedCharacterState::CrouchIdle &&
                         currentState != AnimatedCharacterState::Death &&    // freeze on the ground
                         currentState != AnimatedCharacterState::GetUp &&    // play once, rising
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

            // Root motion extraction.
            //
            // The bone-stripping step (clearing the animated root translation off the
            // root bone so it doesn't double-count visually) must run for ANY clip with
            // `useRootMotion=true`, including while seated. Otherwise sit/stand clips
            // that animate the root bone forward to push off the chair produce a
            // visible model slide even though worldPosition is anchored to the seat.
            //
            // The worldPosition application step (translating the character through the
            // world) is gated by `!m_isSitting`: while seated, the seat anchor owns
            // worldPosition and the root delta must be discarded, not applied.
            if (!isBlending && currentClipIndex >= 0 && currentClipIndex < (int)clips.size()) {
                const AnimationClip& clip = clips[currentClipIndex];

                // Update Y root motion flag so resolveKinematicMovement suppresses gravity.
                // In Preview the drive only fires when explicitly armed (Test Step Down button),
                // never during passive playback — otherwise selecting the clip kills ground snap.
                bool isActiveStairDrive = !m_animPaused && clip.stairStepHeight > 0.0f &&
                    (currentState == AnimatedCharacterState::DescendStairs ||
                     currentState == AnimatedCharacterState::ClimbStairs   ||
                     (currentState == AnimatedCharacterState::Preview && m_stairDriveActive));
                m_yRootMotionActive = !m_isSitting &&
                    ((!m_animPaused && clip.useRootMotion && clip.rootMotionAxes.y)
                     || isActiveStairDrive);

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

                    // Only translate the world while not seated. The delta still has to be
                    // computed so we can keep m_prevRootPos in sync, but seated motion is
                    // discarded — the seat anchor (in the sitting branch above) owns
                    // worldPosition.
                    if (!loopWrapped && !m_isSitting) {
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

                    // Strip extracted axes from root bone so the visual doesn't double-count.
                    // This MUST run even while seated — see header comment above.
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

        applyPostureLean();
        animSystem.updateGlobalTransforms(skeleton);

        // Hook for subclass IK corrections (e.g. HybridCharacter)
        applyIKCorrections(deltaTime);

        // Compute model-to-world base matrix (shared by all bones).
        // Uses m_visualBodyY (spring-smoothed) so the visual skeleton follows the
        // spring, not the raw capsule snap. +0.05 visual lift so foot block pivots
        // sit at the bone center rather than sinking into the floor.
        static constexpr float k_modelVisualLift = 0.05f;
        glm::vec3 visualOrigin = glm::vec3(worldPosition.x, m_visualBodyY, worldPosition.z)
                               - glm::vec3(0.0f, skeletonFootOffset_, 0.0f)
                               + glm::vec3(0.0f, k_modelVisualLift, 0.0f);
        // Phase M2 — teeter pose offset (matches IK render path above).
        if (m_teeterAmount > 0.001f) {
            constexpr float k_teeterMaxOffset = 0.08f;
            visualOrigin.x += m_teeterDirXZ.x * m_teeterAmount * k_teeterMaxOffset;
            visualOrigin.z += m_teeterDirXZ.y * m_teeterAmount * k_teeterMaxOffset;
        }
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

        // Update worldPos/worldRot for every direct-transform part. Iterate the
        // cached bone-group→part-indices map (rebuilt only when parts change)
        // instead of scanning all parts per bone group every frame. Attachment
        // groups (boneGroupId >= 1000, not in boneOffsets) are transformed by the
        // attachments loop below, so they are skipped here.
        for (const auto& grp : getPartGroups()) {
            int boneId = grp.boneGroupId;
            if (boneId < 0 || boneId >= static_cast<int>(skeleton.bones.size())) continue;
            auto offIt = boneOffsets.find(boneId);
            if (offIt == boneOffsets.end()) continue;
            const Phyxel::Bone& bone = skeleton.bones[boneId];

            glm::mat4 finalTransform = glm::translate(modelMatrix * bone.globalTransform, offIt->second);
            glm::vec3 pos = glm::vec3(finalTransform[3]);
            glm::quat rot = glm::quat_cast(finalTransform);

            for (int pi : grp.partIndices) {
                parts[pi].worldPos = pos;
                parts[pi].worldRot = rot;
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

        // ---- Motion trace recording ----
        // Capture per-frame state for slide/teleport detection by the interaction pipeline.
        // worldPosition has been settled by every code path above (sitting branch snap,
        // normal physics resolve, derez, anchored anim, etc.) at this point.
        if (m_motionTraceCapacity > 0) {
            MotionTraceEntry e;
            e.totalTime  = m_totalTime;
            e.deltaTime  = deltaTime;
            e.worldPos   = worldPosition;
            e.hipsLocal  = skeleton.bones.empty() ? glm::vec3(0.0f) : skeleton.bones[0].currentPosition;
            e.state      = static_cast<int>(currentState);
            e.isSitting  = m_isSitting;
            e.isBlending = isBlending;
            e.clipIndex  = currentClipIndex;
            e.animTime   = animTime;
            if (m_motionTrace.size() >= m_motionTraceCapacity) {
                // Drop oldest by shifting — capacity is small (default 1024) so the
                // amortized cost is negligible compared to the per-frame animation work.
                m_motionTrace.erase(m_motionTrace.begin());
            }
            m_motionTrace.push_back(e);
        }

        // ---- Gameplay-event edge detection ----
        // One central place (every FSM/kinematic path funnels through update) so
        // hosts can emit player_jumped / player_landed without duplicating the
        // transition logic. Flags are sticky until consumed (consumeJustJumped /
        // consumeJustLanded), so a host polling once per frame never misses one.
        if (currentState == AnimatedCharacterState::Jump &&
            m_prevStateForEvents != AnimatedCharacterState::Jump) {
            m_justJumped = true;
        }
        if (m_kinGrounded && !m_prevGroundedForEvents) {
            m_justLanded = true;
        }
        m_prevStateForEvents    = currentState;
        m_prevGroundedForEvents = m_kinGrounded;
    }

    void AnimatedVoxelCharacter::render(Graphics::RenderCoordinator* renderer) {
        // Rendering is handled by RenderCoordinator iterating over 'parts'
    }

    std::vector<AnimatedVoxelCharacter::SegmentBoxInfo> AnimatedVoxelCharacter::getSegmentBoxInfo() const {
        std::vector<SegmentBoxInfo> result;
        for (const auto& seg : m_segmentBoxes) {
            SegmentBoxInfo info;
            info.boneName         = seg.boneName;
            info.halfExtents      = seg.halfExtents;
            info.worldHalfExtents = seg.worldHalfExtents;
            info.isArm            = seg.isArm;
            info.colliding        = seg.colliding;
            info.position         = seg.center;
            result.push_back(info);
        }
        return result;
    }

    // =========================================================================
    // Foot IK
    // =========================================================================

    void AnimatedVoxelCharacter::resolveFootBoneIds() {
        // Body-plan-driven: leg/hip ids come from the resolved plan. The
        // humanoid plan's legs are the byte-exact legacy mixamorig:* names,
        // so a biped resolves the same ids the old hardcoded lookups did
        // (pinned by CharacterGoldenPoseTest.FootIKBoneIdentity*). Non-biped
        // plans (footIK=false on every leg) leave the cache not-ready, which
        // matches the old behavior where creature rigs failed the name
        // lookups — the two-bone IK below is biped-only until P3.
        m_leftFoot  = {};
        m_rightFoot = {};
        const auto& legs = m_bodyPlanResolved.legs;
        if (legs.size() == 2 && legs[0].footIK && legs[1].footIK) {
            m_leftFoot.upLegId  = legs[0].upperId;
            m_leftFoot.legId    = legs[0].midId;
            m_leftFoot.footId   = legs[0].footId;
            m_rightFoot.upLegId = legs[1].upperId;
            m_rightFoot.legId   = legs[1].midId;
            m_rightFoot.footId  = legs[1].footId;
        }

        // Pelvis bone for body-adjustment during IK (also the sit anchor).
        // resolveAgainst already applied the exact-name -> hip-alias fallback.
        m_ikHipBoneId = m_bodyPlanResolved.rootBoneId;

        m_footIKCacheReady =
            (m_leftFoot.upLegId  >= 0 && m_leftFoot.legId  >= 0 && m_leftFoot.footId  >= 0) ||
            (m_rightFoot.upLegId >= 0 && m_rightFoot.legId >= 0 && m_rightFoot.footId >= 0);
    }

    AnimatedVoxelCharacter::FootIKResolutionInfo AnimatedVoxelCharacter::resolveFootIKForTest() {
        resolveFootBoneIds();
        FootIKResolutionInfo info;
        info.leftUpLeg  = m_leftFoot.upLegId;
        info.leftLeg    = m_leftFoot.legId;
        info.leftFoot   = m_leftFoot.footId;
        info.rightUpLeg = m_rightFoot.upLegId;
        info.rightLeg   = m_rightFoot.legId;
        info.rightFoot  = m_rightFoot.footId;
        info.hipBoneId  = m_ikHipBoneId;
        info.cacheReady = m_footIKCacheReady;
        return info;
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
        float rawD = glm::length(T - A);
        float D  = glm::clamp(rawD,
                              std::abs(L1 - L2) + 0.001f,
                              L1 + L2 - 0.001f);
        if (L1 < 0.001f || L2 < 0.001f) return;
        LOG_INFO_FMT("IK_geo",
            "A.y=" << A.y << " B.y=" << B.y << " C.y=" << C.y << " T.y=" << T.y
            << " L1=" << L1 << " L2=" << L2 << " rawD=" << rawD << " clampD=" << D
            << " maxReach=" << (L1+L2));

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
        // Foot IK and body spring must not run during sitting — the seat anchor fully
        // owns worldPosition and bone positions during these states. IK would fight the
        // anchor and the spring would diverge m_visualBodyY from the snapped worldPosition.
        if (m_isSitting) {
            m_visualBodyY   = worldPosition.y;
            m_visualBodyVel = 0.0f;
            m_footIKBlend   = 0.0f;
            m_leftFootLock  = {};
            m_rightFootLock = {};
            return;
        }
        // Foot IK off during a dodge roll — the feet leave the ground mid-roll,
        // so planting them to terrain reads as broken. Reset locks/blend so the
        // pose doesn't snap when the roll ends.
        if (currentState == AnimatedCharacterState::Dodge) {
            m_footIKBlend   = 0.0f;
            m_leftFootLock  = {};
            m_rightFootLock = {};
            return;
        }
        if (!m_footIKEnabled) {
            // Foot IK off: keep the visual body snapped to the capsule so the model still
            // renders at the right height (no terrain foot-planting / body spring).
            m_visualBodyY    = worldPosition.y;
            m_visualBodyVel  = 0.0f;
            m_visualBodyInit = true;
            m_footIKBlend    = 0.0f;
            m_leftFootLock   = {};
            m_rightFootLock  = {};
            return;
        }
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

        // ---------------------------------------------------------------
        // Visual body spring  (upward-only smoothing)
        // ---------------------------------------------------------------
        // Only smooth UPWARD snaps (step-up capsule jumps).
        // When the capsule is at or below the visual position, snap down immediately —
        // downward movement is already smoothed by the step-glide, and lagging on
        // falls/landings creates visible float.
        {
            constexpr float k_omega = 12.0f;          // rad/s — 95% settled in ~250ms
            constexpr float k_damp  = 2.0f * k_omega; // critically damped

            bool inAir = !m_kinGrounded                             ||
                         currentState == AnimatedCharacterState::Jump ||
                         currentState == AnimatedCharacterState::Fall ||
                         currentState == AnimatedCharacterState::Land;

            if (!m_visualBodyInit) {
                m_visualBodyY    = worldPosition.y;
                m_visualBodyVel  = 0.0f;
                m_visualBodyInit = true;
            } else if (inAir || worldPosition.y <= m_visualBodyY + 0.001f) {
                // Snap downward (or flat) immediately — no spring lag on falls/descents.
                m_visualBodyY   = worldPosition.y;
                m_visualBodyVel = 0.0f;
            } else {
                // Capsule snapped UP — spring-chase so the visual body rises smoothly.
                float err        = worldPosition.y - m_visualBodyY;
                m_visualBodyVel += (k_omega * k_omega * err - k_damp * m_visualBodyVel) * deltaTime;
                m_visualBodyY   += m_visualBodyVel * deltaTime;
                m_visualBodyY    = std::min(m_visualBodyY, worldPosition.y); // never overshoot

                // TRACE, not INFO: this fires every frame the body spring is chasing upward —
                // an INFO here is a per-frame log-spam footgun (cf. the old 1.1GB TerrainIK spam).
                LOG_TRACE_FMT("BodySpring",
                    "capsY=" << worldPosition.y
                    << " visY=" << m_visualBodyY
                    << " vel=" << m_visualBodyVel
                    << " lag=" << (worldPosition.y - m_visualBodyY));
            }
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

        // Model matrix — uses m_visualBodyY (spring) so IK targets are in the same
        // coordinate frame as the render matrix. Must include visual lift.
        static constexpr float k_ikModelLift = 0.05f;
        glm::vec3 visualOrigin = glm::vec3(worldPosition.x, m_visualBodyY, worldPosition.z)
                               - glm::vec3(0.0f, skeletonFootOffset_, 0.0f)
                               + glm::vec3(0.0f, k_ikModelLift, 0.0f);
        // Phase M2 — teeter pose offset (small visual shift away from a nearby
        // edge while idle). Default off; lerped in resolveKinematicMovement.
        if (m_teeterAmount > 0.001f) {
            constexpr float k_teeterMaxOffset = 0.08f;   // ~8 cm pelvis shift
            visualOrigin.x += m_teeterDirXZ.x * m_teeterAmount * k_teeterMaxOffset;
            visualOrigin.z += m_teeterDirXZ.y * m_teeterAmount * k_teeterMaxOffset;
        }
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
                // Subtract body vertical velocity so step-glide doesn't falsely trigger
                // the swing gate. m_kinVelocity.y is zeroed during step-glide, so we
                // reconstruct the glide contribution separately.
                float bodyVelY = m_kinVelocity.y;
                if (m_stepGlideTargetY > -1.0e29f)
                    bodyVelY += (m_stepGlideTargetY >= worldPosition.y) ? m_stepGlideSpeed : -m_stepGlideSpeed;
                leftSwing  = ((lVelY - bodyVelY) > 0.4f);
                rightSwing = ((rVelY - bodyVelY) > 0.4f);
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
        // NORMAL MODE -- spring-driven step-up IK
        //
        // Body spring lag drives foot IK. When the capsule snaps onto a higher
        // surface, worldPosition.y > m_visualBodyY. We push feet toward
        // worldPosition.y (the step surface) with positive-only corrections.
        // ------------------------------------------------------------------

        constexpr float k_maxFootCorr = 4.0f / 9.0f + 0.05f;
        constexpr float k_stepLagMin  = 0.01f;

        // Use body spring lag as the IK signal. When capsule snapped onto a
        // higher surface, worldPosition.y > m_visualBodyY. Push feet up toward
        // the step surface — positive-only so swing-phase feet (already above)
        // are left alone. Corrections decay naturally as visY chases capsY.
        float springLag = worldPosition.y - m_visualBodyY;

        if (springLag > k_stepLagMin && voxelWorld) {
            float stepSurface = worldPosition.y;

            glm::vec3 lfw = footWorldPos(m_leftFoot);
            glm::vec3 rfw = footWorldPos(m_rightFoot);

            float lCorr = glm::clamp(stepSurface - lfw.y, 0.0f, k_maxFootCorr);
            float rCorr = glm::clamp(stepSurface - rfw.y, 0.0f, k_maxFootCorr);

            // If a terrain foot lock is active (foot planted on obstacle pre-step),
            // keep the foot at the locked surface Y while the body rises to meet it.
            float lBlend = m_footIKBlend, rBlend = m_footIKBlend;
            if (m_leftFootLock.active) {
                m_leftFootLock.lockBlend = std::min(1.0f, m_leftFootLock.lockBlend + deltaTime * blendSpeed);
                lCorr  = m_leftFootLock.lockedY - lfw.y;
                lBlend = m_footIKBlend * m_leftFootLock.lockBlend;
            }
            if (m_rightFootLock.active) {
                m_rightFootLock.lockBlend = std::min(1.0f, m_rightFootLock.lockBlend + deltaTime * blendSpeed);
                rCorr  = m_rightFootLock.lockedY - rfw.y;
                rBlend = m_footIKBlend * m_rightFootLock.lockBlend;
            }

            bool lActive = lCorr > 0.01f || m_leftFootLock.active;
            bool rActive = rCorr > 0.01f || m_rightFootLock.active;

            LOG_INFO_FMT("StepIK",
                "lag=" << springLag
                << " stepSurf=" << stepSurface
                << " Lfw.y=" << lfw.y << " Rfw.y=" << rfw.y
                << " Lcorr=" << lCorr << " Rcorr=" << rCorr
                << " Llock=" << m_leftFootLock.active << " Rlock=" << m_rightFootLock.active
                << " Lactive=" << lActive << " Ractive=" << rActive);

            if (lActive || rActive) {
                float lTargetY = lfw.y + lCorr;
                float rTargetY = rfw.y + rCorr;

                // Pelvis: shift by average of positive corrections x 0.5.
                if (bodyRange > 0.0f && m_ikHipBoneId >= 0 &&
                    m_ikHipBoneId < (int)skeleton.bones.size())
                {
                    float sum = 0.0f; int n = 0;
                    if (lActive && lCorr > 0.0f) { sum += lCorr; ++n; }
                    if (rActive && rCorr > 0.0f) { sum += rCorr; ++n; }
                    if (n > 0) {
                        float shift = glm::clamp((sum / n) * 0.5f, -bodyRange, bodyRange);
                        skeleton.bones[m_ikHipBoneId].currentPosition.y += shift * m_footIKBlend;
                        animSystem.updateGlobalTransforms(skeleton);
                        invModel = glm::inverse(modelMatrix);
                    }
                }

                if (lActive && m_leftFoot.footId >= 0 &&
                    m_leftFoot.footId < (int)skeleton.bones.size())
                {
                    glm::vec3 fw = footWorldPos(m_leftFoot);
                    applyTwoBoneIK(m_leftFoot.upLegId, m_leftFoot.legId, m_leftFoot.footId,
                                   invModel, {fw.x, lTargetY, fw.z}, lBlend);
                    animSystem.updateGlobalTransforms(skeleton);
                    invModel = glm::inverse(modelMatrix);
                }
                if (rActive && m_rightFoot.footId >= 0 &&
                    m_rightFoot.footId < (int)skeleton.bones.size())
                {
                    glm::vec3 fw = footWorldPos(m_rightFoot);
                    applyTwoBoneIK(m_rightFoot.upLegId, m_rightFoot.legId, m_rightFoot.footId,
                                   invModel, {fw.x, rTargetY, fw.z}, rBlend);
                }
            }
        } else if (voxelWorld) {
            // ------------------------------------------------------------------
            // TERRAIN FOLLOW — per-foot surface detection, bidirectional
            //
            // Correction = terrain height difference relative to where the
            // character is standing (worldPosition.y). This preserves the
            // ankle's natural hover above the ground: on flat terrain lCorr=0
            // regardless of animation phase; on lower terrain lCorr is negative
            // (foot reaches down); on a raised surface lCorr is positive.
            //
            // Swinging feet (actively rising) are excluded to avoid fighting
            // the animation on the lifting phase.
            // ------------------------------------------------------------------
            auto findFootSurf = [&](const FootIKBones& fb) -> float {
                if (fb.footId < 0 || fb.footId >= (int)skeleton.bones.size()) return -1.0e30f;
                glm::vec3 fw = footWorldPos(fb);
                return voxelWorld->findGroundY({fw.x, fw.y + 0.4f, fw.z}, m_originalHalfWidth, 0.8f);
            };

            glm::vec3 lfw  = footWorldPos(m_leftFoot);
            glm::vec3 rfw  = footWorldPos(m_rightFoot);
            float     lSurf = findFootSurf(m_leftFoot);
            float     rSurf = findFootSurf(m_rightFoot);
            float     bodySurf = voxelWorld->findGroundY(
                {worldPosition.x, worldPosition.y + 0.4f, worldPosition.z},
                m_originalHalfWidth, 0.8f);

            // Correction = terrain height difference vs. character standing Y.
            // Swing gate is directional: only block DOWNWARD corrections (foot reaching
            // to lower terrain) on a swinging foot. UPWARD corrections (foot stepping
            // onto a raised surface) are allowed through mid-swing — this is stage 3
            // of the stepping sequence: leading foot lifts before the body rises.
            auto applySwingGate = [](float corr, bool swing) -> float {
                if (corr < 0.0f && swing) return 0.0f;  // don't pull rising foot down
                return corr;
            };
            float lCorrRaw = (lSurf > -1.0e29f)
                             ? glm::clamp(lSurf - worldPosition.y, -k_maxFootCorr, 0.0f) : 0.0f;
            float rCorrRaw = (rSurf > -1.0e29f)
                             ? glm::clamp(rSurf - worldPosition.y, -k_maxFootCorr, 0.0f) : 0.0f;
            float lCorr = applySwingGate(lCorrRaw, leftSwing);
            float rCorr = applySwingGate(rCorrRaw, rightSwing);

            // Symmetric drop: both feet over the same lower surface means the
            // character is walking off a ledge, not straddling. Don't correct.
            if (lCorr < -0.01f && rCorr < -0.01f &&
                std::abs(lCorr - rCorr) < 0.08f)
            {
                lCorr = 0.0f;
                rCorr = 0.0f;
            }

            // Ignore corrections smaller than 0.5 microcube — just noise.
            constexpr float k_minCorr = 1.0f / 18.0f;

            // Terrain foot lock — engage when a foot in stance detects a surface
            // above the body's current floor (the foot is approaching an obstacle).
            // Locks the ankle to the obstacle surface Y and holds it there while
            // the body steps up, producing a visible "foot planted on ledge" pose.
            // Persists through the StepIK glide phase; releases once the body has
            // risen to the obstacle level or the foot enters swing.
            auto updateTerrainLock = [&](FootLockState& lock, float corrRaw,
                                         float surf, bool swing) {
                if (lock.active) {
                    bool bodyUp = corrRaw < k_minCorr * 0.5f;
                    if (swing || bodyUp || surf <= -1.0e29f)
                        lock = {};
                    else
                        lock.lockBlend = std::min(1.0f, lock.lockBlend + deltaTime * blendSpeed);
                } else if (!swing && corrRaw > k_minCorr && surf > bodySurf + k_minCorr) {
                    lock.active    = true;
                    lock.lockedY   = surf;
                    lock.lockBlend = 0.0f;
                }
            };
            updateTerrainLock(m_leftFootLock,  lCorrRaw, lSurf,  leftSwing);
            updateTerrainLock(m_rightFootLock, rCorrRaw, rSurf, rightSwing);

            bool lActive = std::abs(lCorr) > k_minCorr || m_leftFootLock.active;
            bool rActive = std::abs(rCorr) > k_minCorr || m_rightFootLock.active;

            // When locked, drive the ankle directly to the locked surface Y.
            // Locked feet use a per-foot blend that eases in from 0 at lock time.
            float lTargetY = m_leftFootLock.active  ? m_leftFootLock.lockedY  : (lfw.y + lCorr);
            float rTargetY = m_rightFootLock.active ? m_rightFootLock.lockedY : (rfw.y + rCorr);
            float lBlend   = m_leftFootLock.active
                               ? (m_footIKBlend * m_leftFootLock.lockBlend) : m_footIKBlend;
            float rBlend   = m_rightFootLock.active
                               ? (m_footIKBlend * m_rightFootLock.lockBlend) : m_footIKBlend;

            LOG_TRACE_FMT("TerrainIK",
                "cap=(" << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z << ")"
                << " L=(" << lfw.x << "," << lfw.y << "," << lfw.z << ")"
                << " R=(" << rfw.x << "," << rfw.y << "," << rfw.z << ")"
                << " Lsurf=" << (lSurf > -1.0e29f ? lSurf : -999.f)
                << " Rsurf=" << (rSurf > -1.0e29f ? rSurf : -999.f)
                << " Bsurf=" << (bodySurf > -1.0e29f ? bodySurf : -999.f)
                << " Lcorr=" << lCorr << " Rcorr=" << rCorr
                << " Llock=" << m_leftFootLock.active << " Rlock=" << m_rightFootLock.active
                << " Lactive=" << lActive << " Ractive=" << rActive);

            if (lActive || rActive) {
                // Pelvis: shift up by average of positive corrections so the knees
                // absorb the terrain height difference rather than over-extending.
                if (bodyRange > 0.0f && m_ikHipBoneId >= 0 &&
                    m_ikHipBoneId < (int)skeleton.bones.size())
                {
                    float pelvisL = m_leftFootLock.active  ? (lTargetY - lfw.y) : lCorr;
                    float pelvisR = m_rightFootLock.active ? (rTargetY - rfw.y) : rCorr;
                    float sum = 0.0f; int n = 0;
                    if (lActive && pelvisL > 0.0f) { sum += pelvisL; ++n; }
                    if (rActive && pelvisR > 0.0f) { sum += pelvisR; ++n; }
                    if (n > 0) {
                        float shift = glm::clamp((sum / n) * 0.5f, 0.0f, bodyRange);
                        skeleton.bones[m_ikHipBoneId].currentPosition.y += shift * m_footIKBlend;
                        animSystem.updateGlobalTransforms(skeleton);
                        invModel = glm::inverse(modelMatrix);
                    }
                }

                if (lActive && m_leftFoot.footId >= 0 &&
                    m_leftFoot.footId < (int)skeleton.bones.size())
                {
                    glm::vec3 fw = footWorldPos(m_leftFoot);
                    applyTwoBoneIK(m_leftFoot.upLegId, m_leftFoot.legId, m_leftFoot.footId,
                                   invModel, {fw.x, lTargetY, fw.z}, lBlend);
                    animSystem.updateGlobalTransforms(skeleton);
                    invModel = glm::inverse(modelMatrix);
                    LOG_TRACE_FMT("TerrainIK_post",
                        "Ltarget=" << lTargetY
                        << " Lpost=" << footWorldPos(m_leftFoot).y
                        << " blend=" << lBlend
                        << " locked=" << m_leftFootLock.active);
                }
                if (rActive && m_rightFoot.footId >= 0 &&
                    m_rightFoot.footId < (int)skeleton.bones.size())
                {
                    glm::vec3 fw = footWorldPos(m_rightFoot);
                    applyTwoBoneIK(m_rightFoot.upLegId, m_rightFoot.legId, m_rightFoot.footId,
                                   invModel, {fw.x, rTargetY, fw.z}, rBlend);
                }

                // Debug viz: cyan cross at each locked foot surface (visible in F5 mode).
                if (hasViz) {
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
            }
        }

        animSystem.updateGlobalTransforms(skeleton);
    }

    // =========================================================================
    // 8-Segment Collision Boxes
    // =========================================================================

    void AnimatedVoxelCharacter::buildSegmentBoxes() {
        clearSegmentBoxes();

        // Body-plan-driven: the segment table (names, isArm flags, ORDER —
        // order is contract for getSegmentBoxInfo/getAttackOrigin/MCP
        // get_bone_positions) comes from the plan. The humanoid plan carries
        // the legacy 12-entry mixamorig table verbatim, so humanoid output is
        // byte-identical (golden layer 2 pins it); creature plans finally get
        // real segment boxes instead of 12 warn-and-skips.
        const auto& planSegments = m_bodyPlan.segments;

        // Build children map for skeleton-based size fallback
        std::map<int, std::vector<int>> childrenMap;
        for (const auto& b : skeleton.bones) {
            if (b.parentId != -1) childrenMap[b.parentId].push_back(b.id);
        }

        for (const auto& seg : planSegments) {
            auto boneIt = skeleton.boneMap.find(seg.bone);
            if (boneIt == skeleton.boneMap.end()) {
                LOG_WARN_FMT("Character", "Segment box: bone not in skeleton: " << seg.bone);
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

            LOG_INFO_FMT("Character", "  Segment [" << seg.bone << "] source=" << source
                << " he=(" << halfExtents.x << "," << halfExtents.y << "," << halfExtents.z << ")");

            m_segmentBoxes.push_back({ seg.bone, boneId, glm::vec3(0.0f), halfExtents, glm::vec3(0.0f), seg.isArm, false });
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
            vw->setKinematicObstacles(this, std::move(obstacles));
        }
    }

    void AnimatedVoxelCharacter::checkSegmentVoxelOverlap() {
        if (!m_chunkManager || m_segmentBoxes.empty()) return;

        // TREE MATTER (Log*/Leaf*) never blocks a limb: chopping REQUIRES the
        // arm to pass into the trunk/canopy, and the limb-block Attack cancel
        // silently killed every point-blank swing before its hit frame (task
        // #8 — the user could stand close enough that no swing ever landed).
        // Walls/stone still block: the exemption is per overlapped CELL.
        auto treeMatterCell = [&](const glm::ivec3& wp) -> bool {
            if (Cube* c = m_chunkManager->getCubeAt(wp)) {
                const std::string& m = c->getMaterialName();
                return m.rfind("Log", 0) == 0 || m.rfind("Leaf", 0) == 0;
            }
            // subdivided cell (flare/notch): wood at any granularity counts
            return Phyxel::DamageSystem::isWoodCellAny(m_chunkManager, wp);
        };

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
                    for (int z = zMin; z <= zMax && !hit; ++z) {
                        const glm::ivec3 wp(x, y, z);
                        if (m_chunkManager->hasVoxelAt(wp) && !treeMatterCell(wp))
                            hit = true;
                    }

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
