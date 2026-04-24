#pragma once

#include <string>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Phyxel {

    struct Bone {
        std::string name;
        int id = -1;
        int parentId = -1;
        
        // Bind pose (local to parent)
        glm::vec3 localPosition{0.0f};
        glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 localScale{1.0f};
        
        // Current animated state (local to parent)
        glm::vec3 currentPosition{0.0f};
        glm::quat currentRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 currentScale{1.0f};

        // Computed global transform (model space)
        glm::mat4 globalTransform{1.0f};
    };

    struct Skeleton {
        std::vector<Bone> bones;
        std::map<std::string, int> boneMap; // Name to index

        void addBone(const std::string& name, int parentId, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
            Bone bone;
            bone.name = name;
            bone.id = (int)bones.size();
            bone.parentId = parentId;
            bone.localPosition = pos;
            bone.localRotation = rot;
            bone.localScale = scale;
            
            // Initialize current state to bind pose
            bone.currentPosition = pos;
            bone.currentRotation = rot;
            bone.currentScale = scale;

            boneMap[name] = bone.id;
            bones.push_back(bone);
        }
    };

    struct PositionKeyframe {
        float time;
        glm::vec3 value;
    };

    struct RotationKeyframe {
        float time;
        glm::quat value;
    };

    struct ScaleKeyframe {
        float time;
        glm::vec3 value;
    };

    struct AnimationChannel {
        int boneId;
        std::vector<PositionKeyframe> positionKeys;
        std::vector<RotationKeyframe> rotationKeys;
        std::vector<ScaleKeyframe> scaleKeys;
    };

    struct AnimationClip {
        std::string name;
        float duration = 0.0f;
        float ticksPerSecond = 0.0f;
        float speed = 0.0f; // Extracted movement speed (units/sec)
        bool useRootMotion = false;
        glm::bvec3 rootMotionAxes = {false, false, false}; // which axes (x,y,z) to extract
        std::vector<AnimationChannel> channels;

        // Tuning metadata — stored as "# clip_meta:" comments in the .anim file.
        // Used by the Clip Parameter Tuner in the anim editor and by the motion
        // warp system at runtime to scale landing animations to the actual fall distance.
        bool  warpEnabled      = false; // apply spatial Y warp to root motion
        float authoredFallDist = 0.667f; // fall distance this clip was authored for (world units)
        float takeoffEnd       = 0.1f;   // normalized time when the takeoff phase ends (before free-fall)
        float contactFrame     = 0.85f;  // normalized time (0-1) when feet contact ground
        float warpScaleMin     = 0.4f;   // minimum allowed warp multiplier
        float warpScaleMax     = 2.5f;   // maximum allowed warp multiplier
        float hitFrameFraction = 0.4f;   // normalized time (0-1) for combat hit trigger
        bool  interruptible    = false;  // player can cancel this animation early
        float interruptAfter   = 0.5f;  // normalized time after which player input cancels it
    };

    struct BoneShape {
        int boneId;
        glm::vec3 size;
        glm::vec3 offset;
    };

    struct VoxelModel {
        std::vector<BoneShape> shapes;
    };
}
