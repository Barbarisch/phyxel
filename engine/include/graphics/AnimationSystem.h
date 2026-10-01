#pragma once
#include "Animation.h"
#include <string>
#include <vector>
#include <map>

namespace Phyxel {

    class AnimationSystem {
    public:
        // Load skeleton and animations from the custom .anim file
        bool loadFromFile(const std::string& filePath, Skeleton& outSkeleton, std::vector<AnimationClip>& outClips, VoxelModel& outModel);

        // Pre-parse an .anim file into the internal parse cache so a later
        // loadFromFile (e.g. the first character spawn) is a cheap copy instead
        // of a ~5s disk parse. Safe to call from a background thread; pass the
        // exact path string that spawns will use so the cache key matches.
        static void prewarm(const std::string& filePath);

        // Drop one file (or all, with an empty path) from the parse cache so the
        // next loadFromFile re-reads the disk. Hot reload without this served the
        // STALE keyframes and only refreshed clip_meta (A0 #7,
        // docs/AnimationSystemV3Plan.md §1.3).
        static void invalidateCache(const std::string& filePath = "");
        
        // Update a skeleton's pose based on an animation and time
        // loop: whether to loop the animation
        void updateAnimation(Skeleton& skeleton, const AnimationClip& clip, float time, bool loop = true);

        // Blend between two animations
        // blendFactor: 0.0 = clipA, 1.0 = clipB
        void blendAnimation(Skeleton& skeleton, 
                           const AnimationClip& clipA, float timeA, bool loopA,
                           const AnimationClip& clipB, float timeB, bool loopB,
                           float blendFactor);
        
        // Calculate global transforms for all bones in the skeleton
        // This should be called after updateAnimation
        void updateGlobalTransforms(Skeleton& skeleton);

        /// A3: the animated LOCAL position of one bone in `clip` at `time` (looped like
        /// updateAnimation), or `fallback` when the clip has no position channel for it. Lets
        /// root-motion extraction read the CURRENT clip's own root while the skeleton holds a
        /// blended pose — root motion used to pause for the whole crossfade.
        glm::vec3 sampleBonePosition(const AnimationClip& clip, int boneId, float time, bool loop,
                                     const glm::vec3& fallback);

    private:
        // Helper for interpolation
        glm::vec3 interpolatePosition(const std::vector<PositionKeyframe>& keys, float time);
        glm::quat interpolateRotation(const std::vector<RotationKeyframe>& keys, float time);
        glm::vec3 interpolateScale(const std::vector<ScaleKeyframe>& keys, float time);
        
        // Helper to find keyframe index
        template<typename T>
        int findKeyframeIndex(const std::vector<T>& keys, float time);
    };
}
