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
