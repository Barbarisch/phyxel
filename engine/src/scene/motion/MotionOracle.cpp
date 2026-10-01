#include "scene/motion/MotionOracle.h"
#include "scene/motion/HumanoidRetargeter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Phyxel::Scene::Motion {

namespace {

float rotationDelta(const glm::quat& a, const glm::quat& b) {
    const float dot = std::clamp(std::abs(glm::dot(glm::normalize(a), glm::normalize(b))),
                                 0.0f, 1.0f);
    return 2.0f * std::acos(dot);
}

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float overlapVolume(const std::pair<glm::vec3, glm::vec3>& a,
                    const std::pair<glm::vec3, glm::vec3>& b) {
    float v = 1.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const float lo = std::max(a.first[axis], b.first[axis]);
        const float hi = std::min(a.second[axis], b.second[axis]);
        if (hi <= lo) return 0.0f;
        v *= (hi - lo);
    }
    return v;
}

} // namespace

void derivePlantedJoints(std::vector<OracleFrame>& frames,
                         const std::vector<std::size_t>& footJoints,
                         float window) {
    if (frames.empty()) return;
    const std::size_t joints = frames.front().worldJointPositions.size();
    for (auto& f : frames) f.plantedJoints.assign(joints, false);
    for (const std::size_t foot : footJoints) {
        if (foot >= joints) continue;
        float minY = std::numeric_limits<float>::infinity();
        for (const auto& f : frames)
            if (foot < f.worldJointPositions.size())
                minY = std::min(minY, f.worldJointPositions[foot].y);
        for (auto& f : frames)
            if (foot < f.worldJointPositions.size())
                f.plantedJoints[foot] = f.worldJointPositions[foot].y < minY + window;
    }
}

void derivePlantedJointsOnTerrain(std::vector<OracleFrame>& frames,
                                  const std::vector<std::size_t>& footJoints,
                                  float window, const OracleTerrain& ground) {
    if (frames.empty()) return;
    const std::size_t joints = frames.front().worldJointPositions.size();
    for (auto& f : frames) f.plantedJoints.assign(joints, false);
    for (const std::size_t foot : footJoints) {
        if (foot >= joints) continue;
        float minClearance = std::numeric_limits<float>::infinity();
        for (const auto& f : frames)
            if (foot < f.worldJointPositions.size()) {
                const auto& p = f.worldJointPositions[foot];
                minClearance = std::min(minClearance, p.y - ground(p.x, p.z));
            }
        for (auto& f : frames)
            if (foot < f.worldJointPositions.size()) {
                const auto& p = f.worldJointPositions[foot];
                f.plantedJoints[foot] = (p.y - ground(p.x, p.z)) < minClearance + window;
            }
    }
}

MotionOracleMetrics evaluateMotion(
    const std::vector<OracleFrame>& frames, float secondsPerFrame,
    const std::vector<std::pair<std::size_t, std::size_t>>& chainEdges) {
    return evaluateMotion(frames, secondsPerFrame, chainEdges, OracleOptions{});
}

MotionOracleMetrics evaluateMotion(
    const std::vector<OracleFrame>& input, float secondsPerFrame,
    const std::vector<std::pair<std::size_t, std::size_t>>& chainEdges,
    const OracleOptions& options) {
    MotionOracleMetrics result;
    if (input.size() < 2 || !std::isfinite(secondsPerFrame) || secondsPerFrame <= 0.0f)
        return result;
    const std::size_t joints = input.front().localRotations.size();
    if (joints == 0) return result;

    // Planted flags: derived from foot height when feet are named, else as given.
    std::vector<OracleFrame> derived;
    const std::vector<OracleFrame>* framesPtr = &input;
    if (!options.footJoints.empty()) {
        derived = input;
        if (options.ground) derivePlantedJointsOnTerrain(derived, options.footJoints, options.plantedWindow, options.ground);
        else                derivePlantedJoints(derived, options.footJoints, options.plantedWindow);
        framesPtr = &derived;
    }
    const auto& frames = *framesPtr;

    std::vector<float> previousVelocity(joints, 0.0f);
    std::vector<float> referenceLengths;
    for (const auto [parent, child] : chainEdges) {
        if (parent >= frames.front().worldJointPositions.size() ||
            child >= frames.front().worldJointPositions.size()) return result;
        referenceLengths.push_back(glm::distance(frames.front().worldJointPositions[parent],
                                                 frames.front().worldJointPositions[child]));
    }

    // Knee reference bend sign per chain, from the first frame with a non-degenerate bend.
    std::vector<float> kneeRefSign(options.kneeChains.size(), 0.0f);

    std::vector<glm::vec2> stanceVelocities;
    result.terrainEvaluated = static_cast<bool>(options.ground);

    for (std::size_t f = 0; f < frames.size(); ++f) {
        const auto& frame = frames[f];
        if (frame.localRotations.size() != joints || !finite(frame.generatedRootVelocity) ||
            !finite(frame.capsuleVelocity)) return result;
        for (const auto& rotation : frame.localRotations)
            if (!isFiniteQuaternion(rotation) || glm::dot(rotation, rotation) < 1.0e-12f) return result;

        result.maxRootVelocityError = std::max(result.maxRootVelocityError,
            glm::length(frame.generatedRootVelocity - frame.capsuleVelocity));

        for (std::size_t edge = 0; edge < chainEdges.size(); ++edge) {
            const auto [parent, child] = chainEdges[edge];
            if (parent >= frame.worldJointPositions.size() || child >= frame.worldJointPositions.size())
                return result;
            result.maxChainLengthError = std::max(result.maxChainLengthError,
                std::abs(glm::distance(frame.worldJointPositions[parent],
                                       frame.worldJointPositions[child]) - referenceLengths[edge]));
        }

        // --- terrain: penetration for every foot, float for planted feet ---
        if (options.ground) {
            for (const std::size_t foot : options.footJoints) {
                if (foot >= frame.worldJointPositions.size()) continue;
                const glm::vec3& p = frame.worldJointPositions[foot];
                float groundUnderFoot = options.ground(p.x, p.z);
                if (options.footHalfLength > 0.0f) {
                    const glm::vec3 fwd = glm::length(frame.rootForward) > 1e-6f ? glm::normalize(frame.rootForward) : glm::vec3(0, 0, 1);
                    const float h = options.footHalfLength;
                    float v[3] = { groundUnderFoot,                                                   // toe
                                   options.ground(p.x - fwd.x * h,        p.z - fwd.z * h),           // mid-sole
                                   options.ground(p.x - fwd.x * 2.0f * h, p.z - fwd.z * 2.0f * h) };  // heel
                    std::sort(v, v + 3);
                    groundUnderFoot = v[1];          // median: two of three samples on a level
                }
                // penetration: the joint against its OWN column (a toe over the lower step cannot
                // be "below" the upper one its heel rests on); float: against the support level
                const float pointClearance = p.y - options.ground(p.x, p.z) - options.footClearanceRef;
                if (pointClearance < 0.0f && -pointClearance > result.maxPenetration) { result.maxPenetration = -pointClearance; result.maxPenetrationFrame = (int)f; }
                const float clearance = p.y - groundUnderFoot - options.footClearanceRef;
                bool stance = foot < frame.plantedJoints.size() && frame.plantedJoints[foot];
                if (options.stanceSpeedMax > 0.0f && f > 0) {
                    const auto& pp = frames[f - 1].worldJointPositions;
                    const float sdt0 = (frame.dt > 0.0f && std::isfinite(frame.dt)) ? frame.dt : secondsPerFrame;
                    if (foot < pp.size())
                        stance = glm::length(glm::vec2(p.x - pp[foot].x, p.z - pp[foot].z)) / sdt0 < options.stanceSpeedMax;
                }
                if (stance && clearance > result.maxStanceFloat) { result.maxStanceFloat = clearance; result.maxStanceFloatFrame = (int)f; }
            }
        }

        // --- knee inversion: knee must stay on its reference side of the hip–ankle line ---
        for (std::size_t c = 0; c < options.kneeChains.size(); ++c) {
            const auto [hip, knee, ankle] = options.kneeChains[c];
            if (hip >= frame.worldJointPositions.size() || knee >= frame.worldJointPositions.size() ||
                ankle >= frame.worldJointPositions.size()) continue;
            const glm::vec3 mid = 0.5f * (frame.worldJointPositions[hip] + frame.worldJointPositions[ankle]);
            const glm::vec3 fwd = glm::length(frame.rootForward) > 1e-6f
                                ? glm::normalize(frame.rootForward) : glm::vec3(0, 0, 1);
            const float bend = glm::dot(frame.worldJointPositions[knee] - mid, fwd);
            if (kneeRefSign[c] == 0.0f && std::abs(bend) > 1e-3f) kneeRefSign[c] = bend > 0.0f ? 1.0f : -1.0f;
            if (kneeRefSign[c] != 0.0f) {
                const float signedBend = bend * kneeRefSign[c];
                if (signedBend < 0.0f) result.maxKneeInversion = std::max(result.maxKneeInversion, -signedBend);
            }
        }

        // --- self-intersection: non-adjacent segment boxes must not overlap ---
        for (std::size_t i = 0; i < frame.boxes.size(); ++i)
            for (std::size_t j = i + 1; j < frame.boxes.size(); ++j) {
                bool adjacent = false;
                for (const auto& [a, b] : options.boxAdjacency)
                    if ((a == i && b == j) || (a == j && b == i)) { adjacent = true; break; }
                if (adjacent) continue;
                result.maxBoxOverlap = std::max(result.maxBoxOverlap, overlapVolume(frame.boxes[i], frame.boxes[j]));
            }

        if (f == 0) continue;
        const auto& prev = frames[f - 1];
        // Live captures carry their own frame time; clip samples are uniform.
        const float sdt = (frame.dt > 0.0f && std::isfinite(frame.dt)) ? frame.dt : secondsPerFrame;

        for (std::size_t joint = 0; joint < joints; ++joint) {
            const float delta = rotationDelta(prev.localRotations[joint], frame.localRotations[joint]);
            const float velocity = delta / sdt;
            result.maxPoseDeltaRadians = std::max(result.maxPoseDeltaRadians, delta);
            result.maxAngularVelocity = std::max(result.maxAngularVelocity, velocity);
            if (f > 1) result.maxAngularAcceleration = std::max(result.maxAngularAcceleration,
                std::abs(velocity - previousVelocity[joint]) / sdt);
            previousVelocity[joint] = velocity;
        }

        const std::size_t planted = std::min(frame.plantedJoints.size(), frame.worldJointPositions.size());
        for (std::size_t joint = 0; joint < planted; ++joint) {
            if (!frame.plantedJoints[joint] || joint >= prev.worldJointPositions.size()) continue;
            result.maxPlantedJointSpeed = std::max(result.maxPlantedJointSpeed,
                glm::distance(frame.worldJointPositions[joint], prev.worldJointPositions[joint]) / sdt);
            // Stance velocity sample: both endpoints planted (anim_lint's rule).
            if (joint < prev.plantedJoints.size() && prev.plantedJoints[joint]) {
                const glm::vec3 d = frame.worldJointPositions[joint] - prev.worldJointPositions[joint];
                stanceVelocities.emplace_back(d.x / sdt, d.z / sdt);
            }
        }
    }

    // --- stance body velocity (= −mean stance-foot XZ velocity) and residual ---
    result.stanceSamples = static_cast<int>(stanceVelocities.size());
    if (!stanceVelocities.empty()) {
        glm::vec2 mean(0.0f);
        for (const auto& v : stanceVelocities) mean += v;
        mean /= static_cast<float>(stanceVelocities.size());
        float resid = 0.0f;
        for (const auto& v : stanceVelocities) resid += glm::length(v - mean);
        result.stanceResidual = resid / static_cast<float>(stanceVelocities.size());
        result.stanceBodyVelocity = -mean;
        result.stanceBodySpeed = glm::length(mean);
        if (options.authoredSpeed > 0.0f)
            result.speedMismatch = std::abs(result.stanceBodySpeed - options.authoredSpeed) / options.authoredSpeed;
    }

    result.valid = true;
    return result;
}

} // namespace Phyxel::Scene::Motion
