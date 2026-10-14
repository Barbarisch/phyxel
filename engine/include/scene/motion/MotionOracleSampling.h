#pragma once

#include "graphics/Animation.h"
#include "scene/motion/MotionOracle.h"

#include <array>
#include <string>
#include <vector>

namespace Phyxel::Scene::Motion {

/// Headless (no engine, no GPU) sampling of a clip on a skeleton into OracleFrames, so the
/// same evaluateMotion() judges a clip file and a live capture (A1, docs/AnimationSystemV3Plan.md).
struct SampledClip {
    std::vector<OracleFrame> frames;
    float secondsPerFrame = 1.0f / 60.0f;
    /// (parent, child) for every non-root bone — the chain-length reference.
    std::vector<std::pair<std::size_t, std::size_t>> chainEdges;
};

/// Pose `bindSkeleton` (copied) with `clip` at `hz` samples per second over the clip's
/// duration (inclusive end). Positions are model-space joint origins; forward is +Z.
SampledClip sampleClip(const Phyxel::Skeleton& bindSkeleton,
                       const Phyxel::AnimationClip& clip,
                       float hz = 60.0f);

/// Feet = bones whose short name ends with "ToeBase" (preferred) else "Foot", one per side.
/// Mirrors tools/anim_pipeline/anim_lint.py::find_feet so both languages judge the same joints.
std::vector<std::size_t> findFeet(const Phyxel::Skeleton& skeleton);

/// (grandparent, parent, foot) for each foot — the 2-bone leg the knee metric needs.
std::vector<std::array<std::size_t, 3>> legChainsForFeet(const Phyxel::Skeleton& skeleton,
                                                         const std::vector<std::size_t>& feet);

} // namespace Phyxel::Scene::Motion
