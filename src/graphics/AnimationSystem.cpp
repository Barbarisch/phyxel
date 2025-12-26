#include "graphics/AnimationSystem.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace Phyxel {

    bool AnimationSystem::loadFromFile(const std::string& filePath, Skeleton& outSkeleton, std::vector<AnimationClip>& outClips, VoxelModel& outModel) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open animation file: " << filePath << std::endl;
            return false;
        }

        std::string line;
        std::string token;

        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            ss >> token;

            if (token == "SKELETON") {
                int boneCount;
                std::getline(file, line); // BoneCount N
                std::stringstream ss2(line);
                std::string temp;
                ss2 >> temp >> boneCount;

                for (int i = 0; i < boneCount; ++i) {
                    std::getline(file, line);
                    std::stringstream ssBone(line);
                    std::string boneToken;
                    int id, parentId;
                    std::string name;
                    glm::vec3 pos, scale;
                    glm::quat rot;

                    ssBone >> boneToken >> id >> name >> parentId 
                           >> pos.x >> pos.y >> pos.z 
                           >> rot.x >> rot.y >> rot.z >> rot.w 
                           >> scale.x >> scale.y >> scale.z;

                    outSkeleton.addBone(name, parentId, pos, rot, scale);
                }
            }
            else if (token == "MODEL") {
                int boxCount;
                std::getline(file, line); // BoxCount N
                std::stringstream ss2(line);
                std::string temp;
                ss2 >> temp >> boxCount;

                for (int i = 0; i < boxCount; ++i) {
                    std::getline(file, line);
                    std::stringstream ssBox(line);
                    std::string boxToken;
                    int id;
                    glm::vec3 size, center;
                    
                    ssBox >> boxToken >> id >> size.x >> size.y >> size.z >> center.x >> center.y >> center.z;
                    
                    BoneShape shape;
                    shape.boneId = id;
                    shape.size = size;
                    shape.offset = center;
                    outModel.shapes.push_back(shape);
                }
            }
            else if (token == "ANIMATION") {
                AnimationClip clip;
                ss >> clip.name;

                // Duration
                std::getline(file, line);
                std::stringstream ssDur(line);
                std::string durToken;
                ssDur >> durToken >> clip.duration;

                // Speed (Optional, for backward compatibility check next token)
                // We peek at the next line. If it starts with "Speed", we parse it.
                // Otherwise we assume it's BoneChannelCount.
                std::streampos oldPos = file.tellg();
                std::getline(file, line);
                std::stringstream ssPeek(line);
                std::string peekToken;
                ssPeek >> peekToken;
                
                if (peekToken == "Speed") {
                    ssPeek >> clip.speed;
                } else {
                    // Not speed, rewind
                    file.seekg(oldPos);
                }

                // BoneChannelCount
                std::getline(file, line);
                std::stringstream ssCount(line);
                std::string countToken;
                int channelCount;
                ssCount >> countToken >> channelCount;

                if (countToken != "BoneChannelCount") {
                    std::cerr << "Error loading animation '" << clip.name << "': Expected BoneChannelCount, got '" << countToken << "'" << std::endl;
                    // Skip this animation or return false?
                    // For now, let's try to continue but warn, though it will likely fail.
                }

                for (int i = 0; i < channelCount; ++i) {
                    std::getline(file, line);
                    std::stringstream ssCh(line);
                    std::string chToken;
                    int boneId, posCount, rotCount, scaleCount;
                    ssCh >> chToken >> boneId >> posCount >> rotCount >> scaleCount;

                    AnimationChannel channel;
                    channel.boneId = boneId;

                    for (int k = 0; k < posCount; ++k) {
                        std::getline(file, line);
                        std::stringstream ssKey(line);
                        std::string keyToken;
                        PositionKeyframe key;
                        ssKey >> keyToken >> key.time >> key.value.x >> key.value.y >> key.value.z;
                        channel.positionKeys.push_back(key);
                    }

                    for (int k = 0; k < rotCount; ++k) {
                        std::getline(file, line);
                        std::stringstream ssKey(line);
                        std::string keyToken;
                        RotationKeyframe key;
                        ssKey >> keyToken >> key.time >> key.value.x >> key.value.y >> key.value.z >> key.value.w;
                        channel.rotationKeys.push_back(key);
                    }

                    for (int k = 0; k < scaleCount; ++k) {
                        std::getline(file, line);
                        std::stringstream ssKey(line);
                        std::string keyToken;
                        ScaleKeyframe key;
                        ssKey >> keyToken >> key.time >> key.value.x >> key.value.y >> key.value.z;
                        channel.scaleKeys.push_back(key);
                    }

                    clip.channels.push_back(channel);
                }
                outClips.push_back(clip);
            }
        }

        return true;
    }

    template<typename T>
    int AnimationSystem::findKeyframeIndex(const std::vector<T>& keys, float time) {
        for (size_t i = 0; i < keys.size() - 1; ++i) {
            if (time < keys[i + 1].time) {
                return (int)i;
            }
        }
        return (int)keys.size() - 1;
    }

    glm::vec3 AnimationSystem::interpolatePosition(const std::vector<PositionKeyframe>& keys, float time) {
        if (keys.empty()) return glm::vec3(0.0f);
        if (keys.size() == 1) return keys[0].value;

        int p0Index = findKeyframeIndex(keys, time);
        int p1Index = p0Index + 1;
        
        if (p1Index >= keys.size()) return keys[p0Index].value;

        float scaleFactor = (time - keys[p0Index].time) / (keys[p1Index].time - keys[p0Index].time);
        return glm::mix(keys[p0Index].value, keys[p1Index].value, scaleFactor);
    }

    glm::quat AnimationSystem::interpolateRotation(const std::vector<RotationKeyframe>& keys, float time) {
        if (keys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (keys.size() == 1) return keys[0].value;

        int p0Index = findKeyframeIndex(keys, time);
        int p1Index = p0Index + 1;

        if (p1Index >= keys.size()) return keys[p0Index].value;

        float scaleFactor = (time - keys[p0Index].time) / (keys[p1Index].time - keys[p0Index].time);
        return glm::slerp(keys[p0Index].value, keys[p1Index].value, scaleFactor);
    }

    glm::vec3 AnimationSystem::interpolateScale(const std::vector<ScaleKeyframe>& keys, float time) {
        if (keys.empty()) return glm::vec3(1.0f);
        if (keys.size() == 1) return keys[0].value;

        int p0Index = findKeyframeIndex(keys, time);
        int p1Index = p0Index + 1;

        if (p1Index >= keys.size()) return keys[p0Index].value;

        float scaleFactor = (time - keys[p0Index].time) / (keys[p1Index].time - keys[p0Index].time);
        return glm::mix(keys[p0Index].value, keys[p1Index].value, scaleFactor);
    }

    void AnimationSystem::updateAnimation(Skeleton& skeleton, const AnimationClip& clip, float time, bool loop) {
        float animTime = time;
        if (loop && clip.duration > 0.0f) {
            animTime = fmod(time, clip.duration);
        } else if (clip.duration > 0.0f) {
            animTime = std::min(time, clip.duration);
        }

        for (const auto& channel : clip.channels) {
            if (channel.boneId >= 0 && channel.boneId < skeleton.bones.size()) {
                Bone& bone = skeleton.bones[channel.boneId];
                
                if (!channel.positionKeys.empty())
                    bone.currentPosition = interpolatePosition(channel.positionKeys, animTime);
                
                if (!channel.rotationKeys.empty())
                    bone.currentRotation = interpolateRotation(channel.rotationKeys, animTime);
                
                if (!channel.scaleKeys.empty())
                    bone.currentScale = interpolateScale(channel.scaleKeys, animTime);
            }
        }
    }

    void AnimationSystem::updateGlobalTransforms(Skeleton& skeleton) {
        for (auto& bone : skeleton.bones) {
            glm::mat4 localTransform = glm::mat4(1.0f);
            localTransform = glm::translate(localTransform, bone.currentPosition);
            localTransform = localTransform * glm::mat4_cast(bone.currentRotation);
            localTransform = glm::scale(localTransform, bone.currentScale);

            if (bone.parentId == -1) {
                bone.globalTransform = localTransform;
            } else {
                bone.globalTransform = skeleton.bones[bone.parentId].globalTransform * localTransform;
            }
        }
    }
}
