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
        
        // Update a skeleton's pose based on an animation and time
        // loop: whether to loop the animation
        void updateAnimation(Skeleton& skeleton, const AnimationClip& clip, float time, bool loop = true);
        
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
