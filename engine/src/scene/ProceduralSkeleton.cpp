#include "scene/ProceduralSkeleton.h"
#include <cmath>
#include <algorithm>
#include <glm/gtc/quaternion.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Phyxel {
namespace Scene {

// ============================================================================
// Helper: add a bone shape to VoxelModel
// ============================================================================

static void addShape(VoxelModel& model, int boneId,
                     const glm::vec3& size, const glm::vec3& offset = glm::vec3(0.0f)) {
    model.shapes.push_back({boneId, size, offset});
}

// ============================================================================
// Humanoid Skeleton
// ============================================================================

CharacterSkeleton ProceduralSkeleton::humanoid(const HumanoidParams& params) {
    CharacterSkeleton cs;
    auto& skel = cs.skeleton;
    auto& model = cs.voxelModel;
    const glm::quat identity(1, 0, 0, 0);
    const glm::vec3 one(1.0f);

    float h = params.height;
    float b = params.bulk;

    // Vertical proportions
    float legH = h * params.legRatio;
    float torsoH = h * params.torsoRatio;
    float headH = h * params.headRatio;
    float neckH = h - legH - torsoH - headH; // remainder
    float upperLeg = legH * 0.55f;
    float lowerLeg = legH * 0.45f;
    float upperArm = h * params.armRatio * 0.5f;
    float forearm = h * params.armRatio * 0.5f;
    float handLen = 0.08f * h;
    float footLen = 0.12f * h;
    float shoulderW = params.shoulderWidth * b;
    float hipW = params.hipWidth * b;
    float spineH = torsoH * 0.5f;
    float chestH = torsoH * 0.5f;

    // Widths
    float torsoW = 0.12f * b;
    float limbW  = 0.05f * b;
    float headW  = headH * 0.7f;

    // Bone 0: Hips (root, at pelvis height = legH)
    skel.addBone("Hips", -1, glm::vec3(0, legH, 0), identity, one);
    addShape(model, 0, glm::vec3(hipW * 2, 0.1f * h, torsoW));

    // Bone 1: Spine
    skel.addBone("Spine", 0, glm::vec3(0, spineH, 0), identity, one);
    addShape(model, 1, glm::vec3(shoulderW * 1.5f, spineH, torsoW));

    // Bone 2: Chest
    skel.addBone("Chest", 1, glm::vec3(0, chestH, 0), identity, one);
    addShape(model, 2, glm::vec3(shoulderW * 2, chestH, torsoW * 1.1f));

    // Bone 3: Neck
    skel.addBone("Neck", 2, glm::vec3(0, neckH, 0), identity, one);
    addShape(model, 3, glm::vec3(limbW, neckH, limbW));

    // Bone 4: Head
    skel.addBone("Head", 3, glm::vec3(0, headH * 0.5f, 0), identity, one);
    addShape(model, 4, glm::vec3(headW, headH, headW));

    // Left arm chain
    // Bone 5: LeftShoulder (at top of chest, offset left)
    skel.addBone("LeftShoulder", 2, glm::vec3(-shoulderW, chestH * 0.8f, 0), identity, one);
    addShape(model, 5, glm::vec3(0.06f * b, limbW * 1.2f, limbW * 1.2f));

    // Bone 6: LeftArm
    skel.addBone("LeftArm", 5, glm::vec3(-0.05f * b, -upperArm, 0), identity, one);
    addShape(model, 6, glm::vec3(limbW, upperArm, limbW), glm::vec3(0, -upperArm * 0.5f, 0));

    // Bone 7: LeftForeArm
    skel.addBone("LeftForeArm", 6, glm::vec3(0, -upperArm, 0), identity, one);
    addShape(model, 7, glm::vec3(limbW * 0.9f, forearm, limbW * 0.9f), glm::vec3(0, -forearm * 0.5f, 0));

    // Bone 8: LeftHand
    skel.addBone("LeftHand", 7, glm::vec3(0, -forearm, 0), identity, one);
    addShape(model, 8, glm::vec3(limbW * 1.2f, handLen, limbW * 0.5f), glm::vec3(0, -handLen * 0.5f, 0));

    // Right arm chain (mirror of left)
    // Bone 9: RightShoulder
    skel.addBone("RightShoulder", 2, glm::vec3(shoulderW, chestH * 0.8f, 0), identity, one);
    addShape(model, 9, glm::vec3(0.06f * b, limbW * 1.2f, limbW * 1.2f));

    // Bone 10: RightArm
    skel.addBone("RightArm", 9, glm::vec3(0.05f * b, -upperArm, 0), identity, one);
    addShape(model, 10, glm::vec3(limbW, upperArm, limbW), glm::vec3(0, -upperArm * 0.5f, 0));

    // Bone 11: RightForeArm
    skel.addBone("RightForeArm", 10, glm::vec3(0, -upperArm, 0), identity, one);
    addShape(model, 11, glm::vec3(limbW * 0.9f, forearm, limbW * 0.9f), glm::vec3(0, -forearm * 0.5f, 0));

    // Bone 12: RightHand
    skel.addBone("RightHand", 11, glm::vec3(0, -forearm, 0), identity, one);
    addShape(model, 12, glm::vec3(limbW * 1.2f, handLen, limbW * 0.5f), glm::vec3(0, -handLen * 0.5f, 0));

    // Left leg chain
    // Bone 13: LeftUpLeg
    skel.addBone("LeftUpLeg", 0, glm::vec3(-hipW, 0, 0), identity, one);
    addShape(model, 13, glm::vec3(limbW * 1.3f, upperLeg, limbW * 1.3f), glm::vec3(0, -upperLeg * 0.5f, 0));

    // Bone 14: LeftLeg (lower)
    skel.addBone("LeftLeg", 13, glm::vec3(0, -upperLeg, 0), identity, one);
    addShape(model, 14, glm::vec3(limbW * 1.1f, lowerLeg, limbW * 1.1f), glm::vec3(0, -lowerLeg * 0.5f, 0));

    // Bone 15: LeftFoot
    skel.addBone("LeftFoot", 14, glm::vec3(0, -lowerLeg, 0), identity, one);
    addShape(model, 15, glm::vec3(limbW * 1.3f, 0.04f * h, footLen), glm::vec3(0, 0, footLen * 0.3f));

    // Right leg chain (mirror)
    // Bone 16: RightUpLeg
    skel.addBone("RightUpLeg", 0, glm::vec3(hipW, 0, 0), identity, one);
    addShape(model, 16, glm::vec3(limbW * 1.3f, upperLeg, limbW * 1.3f), glm::vec3(0, -upperLeg * 0.5f, 0));

    // Bone 17: RightLeg (lower)
    skel.addBone("RightLeg", 16, glm::vec3(0, -upperLeg, 0), identity, one);
    addShape(model, 17, glm::vec3(limbW * 1.1f, lowerLeg, limbW * 1.1f), glm::vec3(0, -lowerLeg * 0.5f, 0));

    // Bone 18: RightFoot
    skel.addBone("RightFoot", 17, glm::vec3(0, -lowerLeg, 0), identity, one);
    addShape(model, 18, glm::vec3(limbW * 1.3f, 0.04f * h, footLen), glm::vec3(0, 0, footLen * 0.3f));

    cs.computeBoneSizes();
    cs.generateJointDefs();
    return cs;
}

// ============================================================================
// Quadruped Skeleton
// ============================================================================

CharacterSkeleton ProceduralSkeleton::quadruped(const QuadrupedParams& params) {
    CharacterSkeleton cs;
    auto& skel = cs.skeleton;
    auto& model = cs.voxelModel;
    const glm::quat identity(1, 0, 0, 0);
    const glm::vec3 one(1.0f);

    float bLen = params.bodyLength;
    float legH = params.legHeight;
    float bW = params.bodyWidth;
    float upperLeg = legH * 0.55f;
    float lowerLeg = legH * 0.45f;
    float limbW = bW * 0.3f;

    // Bone 0: Hips (root, at hip joint height = legH)
    skel.addBone("Hips", -1, glm::vec3(0, legH, 0), identity, one);
    addShape(model, 0, glm::vec3(bW * 2, bW, bW), glm::vec3(0, bW * 0.5f, 0));

    // Bone 1: Spine (forward from hips)
    skel.addBone("Spine", 0, glm::vec3(0, 0, -bLen * 0.5f), identity, one);
    addShape(model, 1, glm::vec3(bW * 2, bW * 1.1f, bLen * 0.5f));

    // Bone 2: Chest (front of body)
    skel.addBone("Chest", 1, glm::vec3(0, 0, -bLen * 0.5f), identity, one);
    addShape(model, 2, glm::vec3(bW * 2, bW * 1.2f, bLen * 0.15f));

    // Bone 3: Neck
    skel.addBone("Neck", 2, glm::vec3(0, params.neckLength * 0.5f, -bLen * 0.1f), identity, one);
    addShape(model, 3, glm::vec3(limbW * 1.5f, params.neckLength, limbW * 1.5f));

    // Bone 4: Head
    skel.addBone("Head", 3, glm::vec3(0, params.neckLength * 0.5f, -params.headSize), identity, one);
    addShape(model, 4, glm::vec3(params.headSize, params.headSize, params.headSize * 1.5f));

    // Front left leg (attached to Chest)
    skel.addBone("FrontLeftUpLeg", 2, glm::vec3(-bW, -bW * 0.3f, 0), identity, one);
    addShape(model, 5, glm::vec3(limbW, upperLeg, limbW), glm::vec3(0, -upperLeg * 0.5f, 0));

    skel.addBone("FrontLeftLeg", 5, glm::vec3(0, -upperLeg, 0), identity, one);
    addShape(model, 6, glm::vec3(limbW * 0.9f, lowerLeg, limbW * 0.9f), glm::vec3(0, -lowerLeg * 0.5f, 0));

    // Front right leg
    skel.addBone("FrontRightUpLeg", 2, glm::vec3(bW, -bW * 0.3f, 0), identity, one);
    addShape(model, 7, glm::vec3(limbW, upperLeg, limbW), glm::vec3(0, -upperLeg * 0.5f, 0));

    skel.addBone("FrontRightLeg", 7, glm::vec3(0, -upperLeg, 0), identity, one);
    addShape(model, 8, glm::vec3(limbW * 0.9f, lowerLeg, limbW * 0.9f), glm::vec3(0, -lowerLeg * 0.5f, 0));

    // Rear left leg (attached to Hips)
    skel.addBone("RearLeftUpLeg", 0, glm::vec3(-bW, 0, bLen * 0.1f), identity, one);
    addShape(model, 9, glm::vec3(limbW, upperLeg, limbW), glm::vec3(0, -upperLeg * 0.5f, 0));

    skel.addBone("RearLeftLeg", 9, glm::vec3(0, -upperLeg, 0), identity, one);
    addShape(model, 10, glm::vec3(limbW * 0.9f, lowerLeg, limbW * 0.9f), glm::vec3(0, -lowerLeg * 0.5f, 0));

    // Rear right leg
    skel.addBone("RearRightUpLeg", 0, glm::vec3(bW, 0, bLen * 0.1f), identity, one);
    addShape(model, 11, glm::vec3(limbW, upperLeg, limbW), glm::vec3(0, -upperLeg * 0.5f, 0));

    skel.addBone("RearRightLeg", 11, glm::vec3(0, -upperLeg, 0), identity, one);
    addShape(model, 12, glm::vec3(limbW * 0.9f, lowerLeg, limbW * 0.9f), glm::vec3(0, -lowerLeg * 0.5f, 0));

    // Tail (if length > 0)
    if (params.tailLength > 0.01f) {
        skel.addBone("Tail", 0, glm::vec3(0, bW * 0.3f, bLen * 0.15f), identity, one);
        addShape(model, static_cast<int>(skel.bones.size()) - 1,
                 glm::vec3(limbW * 0.5f, limbW * 0.5f, params.tailLength),
                 glm::vec3(0, 0, params.tailLength * 0.5f));
    }

    cs.computeBoneSizes();
    cs.generateJointDefs();
    return cs;
}

// ============================================================================
// Arachnid Skeleton
// ============================================================================

CharacterSkeleton ProceduralSkeleton::arachnid(const ArachnidParams& params) {
    CharacterSkeleton cs;
    auto& skel = cs.skeleton;
    auto& model = cs.voxelModel;
    const glm::quat identity(1, 0, 0, 0);
    const glm::vec3 one(1.0f);

    int legCount = std::clamp(params.legCount, 4, 12);
    if (legCount % 2 != 0) legCount++; // ensure even

    float r = params.bodyRadius;
    float legLen = params.legLength;
    float upperLeg = legLen * 0.55f;
    float lowerLeg = legLen * 0.45f;
    float limbW = r * 0.2f;

    // Bone 0: Body (root, centered at body height)
    float bodyHeight = legLen * std::cos(params.legSpread) * 0.6f;
    skel.addBone("Body", -1, glm::vec3(0, bodyHeight, 0), identity, one);
    addShape(model, 0, glm::vec3(r * 2, r, r * 2.5f));

    int pairsCount = legCount / 2;
    float zSpacing = (pairsCount > 1) ? (r * 2.0f / static_cast<float>(pairsCount - 1)) : 0.0f;
    float zStart = -r;

    for (int pair = 0; pair < pairsCount; ++pair) {
        float zOffset = (pairsCount > 1) ? (zStart + pair * zSpacing) : 0.0f;
        float splayAngle = params.legSpread;

        for (int side = 0; side < 2; ++side) {
            float xSign = (side == 0) ? -1.0f : 1.0f;
            std::string sideStr = (side == 0) ? "Left" : "Right";
            std::string legName = sideStr + "Leg" + std::to_string(pair);

            // Upper leg: extends outward and downward
            float dx = xSign * std::sin(splayAngle) * upperLeg;
            float dy = -std::cos(splayAngle) * upperLeg;

            skel.addBone(legName + "Upper", 0,
                         glm::vec3(xSign * r, 0, zOffset), identity, one);
            int upperIdx = static_cast<int>(skel.bones.size()) - 1;
            addShape(model, upperIdx, glm::vec3(limbW, limbW, upperLeg),
                     glm::vec3(dx * 0.5f, dy * 0.5f, 0));

            // Lower leg: continues outward and down
            skel.addBone(legName + "Lower", upperIdx,
                         glm::vec3(dx, dy, 0), identity, one);
            int lowerIdx = static_cast<int>(skel.bones.size()) - 1;
            float dx2 = xSign * std::sin(splayAngle * 0.5f) * lowerLeg;
            float dy2 = -std::cos(splayAngle * 0.5f) * lowerLeg;
            addShape(model, lowerIdx, glm::vec3(limbW * 0.8f, limbW * 0.8f, lowerLeg),
                     glm::vec3(dx2 * 0.5f, dy2 * 0.5f, 0));
        }
    }

    cs.computeBoneSizes();
    cs.generateJointDefs();
    return cs;
}

// ============================================================================
// Animation Helpers
// ============================================================================

static void addPosKey(AnimationChannel& ch, float time, const glm::vec3& pos) {
    ch.positionKeys.push_back({time, pos});
}

static void addRotKey(AnimationChannel& ch, float time, const glm::quat& rot) {
    ch.rotationKeys.push_back({time, rot});
}

static glm::quat rotX(float radians) {
    return glm::angleAxis(radians, glm::vec3(1, 0, 0));
}

static glm::quat rotZ(float radians) {
    return glm::angleAxis(radians, glm::vec3(0, 0, 1));
}

// ============================================================================
// Humanoid Walk Cycle
// ============================================================================

AnimationClip ProceduralSkeleton::humanoidWalkCycle(const CharacterSkeleton& skeleton,
                                                     float speed) {
    AnimationClip clip;
    clip.name = "walk";
    clip.duration = 1.0f; // 1 second per cycle
    clip.ticksPerSecond = 1.0f;
    clip.speed = speed;

    const auto& bones = skeleton.skeleton.bones;
    const auto& boneMap = skeleton.skeleton.boneMap;

    // Find bone IDs
    auto findBone = [&](const std::string& name) -> int {
        auto it = boneMap.find(name);
        return (it != boneMap.end()) ? it->second : -1;
    };

    float swingAngle = 0.35f;  // Hip/shoulder swing ~20 degrees
    float kneeAngle = 0.6f;    // Knee bend
    float armSwing = 0.3f;     // Arm swing
    float bobAmount = 0.02f;   // Vertical bounce

    // Hips: subtle vertical bob
    int hipsId = findBone("Hips");
    if (hipsId >= 0) {
        AnimationChannel ch;
        ch.boneId = hipsId;
        addPosKey(ch, 0.0f, bones[hipsId].localPosition + glm::vec3(0, bobAmount, 0));
        addPosKey(ch, 0.25f, bones[hipsId].localPosition);
        addPosKey(ch, 0.5f, bones[hipsId].localPosition + glm::vec3(0, bobAmount, 0));
        addPosKey(ch, 0.75f, bones[hipsId].localPosition);
        addPosKey(ch, 1.0f, bones[hipsId].localPosition + glm::vec3(0, bobAmount, 0));
        clip.channels.push_back(ch);
    }

    // Left upper leg: swing forward then back
    int leftUpLeg = findBone("LeftUpLeg");
    if (leftUpLeg >= 0) {
        AnimationChannel ch;
        ch.boneId = leftUpLeg;
        addRotKey(ch, 0.0f, rotX(swingAngle));   // forward
        addRotKey(ch, 0.5f, rotX(-swingAngle));   // back
        addRotKey(ch, 1.0f, rotX(swingAngle));    // forward
        clip.channels.push_back(ch);
    }

    // Right upper leg: opposite phase
    int rightUpLeg = findBone("RightUpLeg");
    if (rightUpLeg >= 0) {
        AnimationChannel ch;
        ch.boneId = rightUpLeg;
        addRotKey(ch, 0.0f, rotX(-swingAngle));
        addRotKey(ch, 0.5f, rotX(swingAngle));
        addRotKey(ch, 1.0f, rotX(-swingAngle));
        clip.channels.push_back(ch);
    }

    // Left lower leg: knee bend during leg pull phase
    int leftLeg = findBone("LeftLeg");
    if (leftLeg >= 0) {
        AnimationChannel ch;
        ch.boneId = leftLeg;
        addRotKey(ch, 0.0f, rotX(0));
        addRotKey(ch, 0.25f, rotX(kneeAngle));    // bend at mid-swing
        addRotKey(ch, 0.5f, rotX(0));
        addRotKey(ch, 0.75f, rotX(kneeAngle * 0.3f));
        addRotKey(ch, 1.0f, rotX(0));
        clip.channels.push_back(ch);
    }

    // Right lower leg: opposite phase
    int rightLeg = findBone("RightLeg");
    if (rightLeg >= 0) {
        AnimationChannel ch;
        ch.boneId = rightLeg;
        addRotKey(ch, 0.0f, rotX(0));
        addRotKey(ch, 0.25f, rotX(kneeAngle * 0.3f));
        addRotKey(ch, 0.5f, rotX(0));
        addRotKey(ch, 0.75f, rotX(kneeAngle));
        addRotKey(ch, 1.0f, rotX(0));
        clip.channels.push_back(ch);
    }

    // Left arm: opposite to left leg (counter-swing)
    int leftArm = findBone("LeftArm");
    if (leftArm >= 0) {
        AnimationChannel ch;
        ch.boneId = leftArm;
        addRotKey(ch, 0.0f, rotX(-armSwing));
        addRotKey(ch, 0.5f, rotX(armSwing));
        addRotKey(ch, 1.0f, rotX(-armSwing));
        clip.channels.push_back(ch);
    }

    // Right arm
    int rightArm = findBone("RightArm");
    if (rightArm >= 0) {
        AnimationChannel ch;
        ch.boneId = rightArm;
        addRotKey(ch, 0.0f, rotX(armSwing));
        addRotKey(ch, 0.5f, rotX(-armSwing));
        addRotKey(ch, 1.0f, rotX(armSwing));
        clip.channels.push_back(ch);
    }

    // Spine: subtle lateral sway
    int spineId = findBone("Spine");
    if (spineId >= 0) {
        AnimationChannel ch;
        ch.boneId = spineId;
        addRotKey(ch, 0.0f, rotZ(0.03f));
        addRotKey(ch, 0.5f, rotZ(-0.03f));
        addRotKey(ch, 1.0f, rotZ(0.03f));
        clip.channels.push_back(ch);
    }

    return clip;
}

// ============================================================================
// Humanoid Idle Cycle
// ============================================================================

AnimationClip ProceduralSkeleton::humanoidIdleCycle(const CharacterSkeleton& skeleton) {
    AnimationClip clip;
    clip.name = "idle";
    clip.duration = 3.0f; // 3 second breathing cycle
    clip.ticksPerSecond = 1.0f;
    clip.speed = 0.0f;

    const auto& bones = skeleton.skeleton.bones;
    const auto& boneMap = skeleton.skeleton.boneMap;

    auto findBone = [&](const std::string& name) -> int {
        auto it = boneMap.find(name);
        return (it != boneMap.end()) ? it->second : -1;
    };

    // Chest: subtle breathing motion
    int chestId = findBone("Chest");
    if (chestId >= 0) {
        AnimationChannel ch;
        ch.boneId = chestId;
        addRotKey(ch, 0.0f, rotX(0.0f));
        addRotKey(ch, 1.5f, rotX(0.02f));  // inhale
        addRotKey(ch, 3.0f, rotX(0.0f));   // exhale
        clip.channels.push_back(ch);
    }

    // Head: very subtle nod
    int headId = findBone("Head");
    if (headId >= 0) {
        AnimationChannel ch;
        ch.boneId = headId;
        addRotKey(ch, 0.0f, rotX(0.0f));
        addRotKey(ch, 1.5f, rotX(-0.01f));
        addRotKey(ch, 3.0f, rotX(0.0f));
        clip.channels.push_back(ch);
    }

    return clip;
}

// ============================================================================
// Quadruped Walk Cycle
// ============================================================================

AnimationClip ProceduralSkeleton::quadrupedWalkCycle(const CharacterSkeleton& skeleton,
                                                      float speed) {
    AnimationClip clip;
    clip.name = "walk";
    clip.duration = 1.0f;
    clip.ticksPerSecond = 1.0f;
    clip.speed = speed;

    const auto& boneMap = skeleton.skeleton.boneMap;

    auto findBone = [&](const std::string& name) -> int {
        auto it = boneMap.find(name);
        return (it != boneMap.end()) ? it->second : -1;
    };

    float swingAngle = 0.3f;
    float kneeAngle = 0.4f;

    // Diagonal gait: front-left + rear-right together, then front-right + rear-left
    struct LegPhase {
        std::string upper;
        std::string lower;
        float phase; // 0.0 or 0.5
    };

    std::vector<LegPhase> legs = {
        {"FrontLeftUpLeg",  "FrontLeftLeg",  0.0f},
        {"FrontRightUpLeg", "FrontRightLeg", 0.5f},
        {"RearLeftUpLeg",   "RearLeftLeg",   0.5f},
        {"RearRightUpLeg",  "RearRightLeg",  0.0f},
    };

    for (auto& leg : legs) {
        int upperId = findBone(leg.upper);
        int lowerId = findBone(leg.lower);

        if (upperId >= 0) {
            AnimationChannel ch;
            ch.boneId = upperId;
            float p = leg.phase;
            addRotKey(ch, 0.0f, rotX(swingAngle * std::cos(p * 2.0f * static_cast<float>(M_PI))));
            addRotKey(ch, 0.25f, rotX(swingAngle * std::cos((p + 0.25f) * 2.0f * static_cast<float>(M_PI))));
            addRotKey(ch, 0.5f, rotX(swingAngle * std::cos((p + 0.5f) * 2.0f * static_cast<float>(M_PI))));
            addRotKey(ch, 0.75f, rotX(swingAngle * std::cos((p + 0.75f) * 2.0f * static_cast<float>(M_PI))));
            addRotKey(ch, 1.0f, rotX(swingAngle * std::cos(p * 2.0f * static_cast<float>(M_PI))));
            clip.channels.push_back(ch);
        }

        if (lowerId >= 0) {
            AnimationChannel ch;
            ch.boneId = lowerId;
            float p = leg.phase;
            // Knee bends during forward swing
            addRotKey(ch, 0.0f, rotX(kneeAngle * std::max(0.0f, std::sin(p * 2.0f * static_cast<float>(M_PI)))));
            addRotKey(ch, 0.25f, rotX(kneeAngle * std::max(0.0f, std::sin((p + 0.25f) * 2.0f * static_cast<float>(M_PI)))));
            addRotKey(ch, 0.5f, rotX(kneeAngle * std::max(0.0f, std::sin((p + 0.5f) * 2.0f * static_cast<float>(M_PI)))));
            addRotKey(ch, 0.75f, rotX(kneeAngle * std::max(0.0f, std::sin((p + 0.75f) * 2.0f * static_cast<float>(M_PI)))));
            addRotKey(ch, 1.0f, rotX(kneeAngle * std::max(0.0f, std::sin(p * 2.0f * static_cast<float>(M_PI)))));
            clip.channels.push_back(ch);
        }
    }

    return clip;
}

// ============================================================================
// Arachnid Walk Cycle
// ============================================================================

AnimationClip ProceduralSkeleton::arachnidWalkCycle(const CharacterSkeleton& skeleton,
                                                     float speed) {
    AnimationClip clip;
    clip.name = "walk";
    clip.duration = 1.0f;
    clip.ticksPerSecond = 1.0f;
    clip.speed = speed;

    const auto& skel = skeleton.skeleton;
    float swingAngle = 0.25f;

    // Alternate pairs: even-numbered pairs move together, odd-numbered pairs move together
    for (size_t i = 1; i < skel.bones.size(); ++i) {
        const auto& bone = skel.bones[i];
        if (bone.parentId < 0) continue;

        // Only animate upper leg segments (direct children of Body)
        if (bone.parentId != 0) continue;

        // Determine phase from pair index
        // Parse the pair number from the bone name
        float phase = (i % 4 < 2) ? 0.0f : 0.5f;

        AnimationChannel ch;
        ch.boneId = static_cast<int>(i);
        addRotKey(ch, 0.0f, rotX(swingAngle * std::cos(phase * 2.0f * static_cast<float>(M_PI))));
        addRotKey(ch, 0.25f, rotX(swingAngle * std::cos((phase + 0.25f) * 2.0f * static_cast<float>(M_PI))));
        addRotKey(ch, 0.5f, rotX(swingAngle * std::cos((phase + 0.5f) * 2.0f * static_cast<float>(M_PI))));
        addRotKey(ch, 0.75f, rotX(swingAngle * std::cos((phase + 0.75f) * 2.0f * static_cast<float>(M_PI))));
        addRotKey(ch, 1.0f, rotX(swingAngle * std::cos(phase * 2.0f * static_cast<float>(M_PI))));
        clip.channels.push_back(ch);
    }

    return clip;
}

} // namespace Scene
} // namespace Phyxel
