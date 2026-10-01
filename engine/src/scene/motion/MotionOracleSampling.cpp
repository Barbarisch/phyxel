#include "scene/motion/MotionOracleSampling.h"
#include "graphics/AnimationSystem.h"

#include <algorithm>
#include <cmath>

namespace Phyxel::Scene::Motion {

namespace {
std::string shortName(const std::string& name) {
    const auto colon = name.rfind(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}
bool endsWith(const std::string& s, const char* suffix) {
    const std::string suf(suffix);
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
char sideOf(const std::string& shortBone) {
    if (shortBone.rfind("Left", 0) == 0 || shortBone.rfind("L_", 0) == 0) return 'L';
    if (shortBone.rfind("Right", 0) == 0 || shortBone.rfind("R_", 0) == 0) return 'R';
    return 0;
}
} // namespace

std::vector<std::size_t> findFeet(const Phyxel::Skeleton& skeleton) {
    std::size_t left = skeleton.bones.size(), right = skeleton.bones.size();
    for (const char* suffix : {"ToeBase", "Foot"}) {
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
            const std::string s = shortName(skeleton.bones[i].name);
            if (!endsWith(s, suffix)) continue;
            const char side = sideOf(s);
            if (side == 'L' && left == skeleton.bones.size()) left = i;
            if (side == 'R' && right == skeleton.bones.size()) right = i;
        }
        if (left < skeleton.bones.size() && right < skeleton.bones.size()) break;
    }
    std::vector<std::size_t> feet;
    if (left < skeleton.bones.size()) feet.push_back(left);
    if (right < skeleton.bones.size()) feet.push_back(right);
    return feet;
}

std::vector<std::array<std::size_t, 3>> legChainsForFeet(const Phyxel::Skeleton& skeleton,
                                                         const std::vector<std::size_t>& feet) {
    std::vector<std::array<std::size_t, 3>> chains;
    for (std::size_t foot : feet) {
        if (foot >= skeleton.bones.size()) continue;
        // findFeet prefers the toe joint (the contact point). The KNEE chain is
        // hip → knee → ankle, so step from a toe up to the ankle first — otherwise
        // the "knee" would be the ankle and every heel-to-toe roll reads as an inversion.
        std::size_t ankle = foot;
        if (endsWith(shortName(skeleton.bones[foot].name), "ToeBase") && skeleton.bones[foot].parentId >= 0)
            ankle = static_cast<std::size_t>(skeleton.bones[foot].parentId);
        const int knee = skeleton.bones[ankle].parentId;
        if (knee < 0) continue;
        const int hip = skeleton.bones[static_cast<std::size_t>(knee)].parentId;
        if (hip < 0) continue;
        chains.push_back({static_cast<std::size_t>(hip), static_cast<std::size_t>(knee), ankle});
    }
    return chains;
}

SampledClip sampleClip(const Phyxel::Skeleton& bindSkeleton,
                       const Phyxel::AnimationClip& clip,
                       float hz) {
    SampledClip out;
    if (hz <= 0.0f || clip.duration <= 0.0f || bindSkeleton.bones.empty()) return out;
    out.secondsPerFrame = 1.0f / hz;
    Phyxel::Skeleton skeleton = bindSkeleton;
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
        if (skeleton.bones[i].parentId >= 0)
            out.chainEdges.emplace_back(static_cast<std::size_t>(skeleton.bones[i].parentId), i);

    AnimationSystem system;
    const int samples = std::max(3, static_cast<int>(clip.duration * hz) + 1);
    glm::vec3 prevRoot(0.0f);
    for (int i = 0; i < samples; ++i) {
        const float t = std::min(i * out.secondsPerFrame, clip.duration);
        system.updateAnimation(skeleton, clip, t, /*loop=*/false);
        system.updateGlobalTransforms(skeleton);
        OracleFrame frame;
        frame.localRotations.reserve(skeleton.bones.size());
        frame.worldJointPositions.reserve(skeleton.bones.size());
        for (const auto& bone : skeleton.bones) {
            frame.localRotations.push_back(bone.currentRotation);
            frame.worldJointPositions.push_back(glm::vec3(bone.globalTransform[3]));
        }
        const glm::vec3 root = frame.worldJointPositions.front();
        frame.generatedRootVelocity = (i == 0) ? glm::vec3(0.0f) : (root - prevRoot) / out.secondsPerFrame;
        frame.capsuleVelocity = glm::vec3(0.0f);   // headless: no controller
        frame.rootForward = glm::vec3(0.0f, 0.0f, 1.0f);
        prevRoot = root;
        out.frames.push_back(std::move(frame));
    }
    return out;
}

} // namespace Phyxel::Scene::Motion
