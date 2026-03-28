#include "scene/CharacterSkeleton.h"
#include "graphics/AnimationSystem.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace Phyxel {
namespace Scene {

// ============================================================================
// BoneFilterConfig
// ============================================================================

bool BoneFilterConfig::shouldSkip(const std::string& boneName) const {
    std::string lower = boneName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (const auto& pattern : skipPatterns) {
        if (lower.find(pattern) != std::string::npos) return true;
    }
    return false;
}

// ============================================================================
// CharacterSkeleton — Loading
// ============================================================================

bool CharacterSkeleton::loadFromAnimFile(const std::string& filePath) {
    AnimationSystem animSystem;
    std::vector<AnimationClip> clips; // discarded here — caller loads clips separately
    if (!animSystem.loadFromFile(filePath, skeleton, clips, voxelModel)) {
        return false;
    }

    computeBoneSizes();
    generateJointDefs();
    return true;
}

// ============================================================================
// Children map
// ============================================================================

std::map<int, std::vector<int>> CharacterSkeleton::buildChildrenMap() const {
    std::map<int, std::vector<int>> children;
    for (const auto& bone : skeleton.bones) {
        if (bone.parentId >= 0) {
            children[bone.parentId].push_back(bone.id);
        }
    }
    return children;
}

// ============================================================================
// Physics bone filtering
// ============================================================================

std::vector<int> CharacterSkeleton::getPhysicsBoneIds() const {
    std::vector<int> ids;
    for (const auto& bone : skeleton.bones) {
        if (!boneFilter.shouldSkip(bone.name)) {
            ids.push_back(bone.id);
        }
    }
    return ids;
}

// ============================================================================
// Compute bone sizes from hierarchy (used for physics body creation)
// ============================================================================

void CharacterSkeleton::computeBoneSizes() {
    boneSizes.clear();
    boneOffsets.clear();
    boneMasses.clear();

    auto children = buildChildrenMap();

    for (const auto& bone : skeleton.bones) {
        if (boneFilter.shouldSkip(bone.name)) continue;

        // If we have MODEL shapes, compute bounding box from them
        if (!voxelModel.shapes.empty()) {
            glm::vec3 minPt(1e9f), maxPt(-1e9f);
            bool hasShapes = false;
            for (const auto& shape : voxelModel.shapes) {
                if (shape.boneId == bone.id) {
                    hasShapes = true;
                    glm::vec3 half = shape.size * 0.5f;
                    minPt = glm::min(minPt, shape.offset - half);
                    maxPt = glm::max(maxPt, shape.offset + half);
                }
            }
            if (hasShapes) {
                glm::vec3 size = maxPt - minPt;
                size = glm::max(size, glm::vec3(0.05f));
                boneSizes[bone.id] = size;
                boneOffsets[bone.id] = (minPt + maxPt) * 0.5f;
                float volume = size.x * size.y * size.z;
                boneMasses[bone.id] = volume * 1000.0f; // density ~1000 kg/m³
                continue;
            }
        }

        // Auto-generate from skeleton hierarchy (child direction)
        glm::vec3 targetVector(0.0f);
        bool hasChild = !children[bone.id].empty();

        if (hasChild) {
            // Prefer Spine child for multi-child bones (like Hips)
            int targetChildId = -1;
            if (children[bone.id].size() > 1) {
                for (int childId : children[bone.id]) {
                    if (skeleton.bones[childId].name.find("Spine") != std::string::npos) {
                        targetChildId = childId;
                        break;
                    }
                }
            }

            if (targetChildId >= 0) {
                targetVector = skeleton.bones[targetChildId].localPosition;
            } else {
                for (int childId : children[bone.id]) {
                    targetVector += skeleton.bones[childId].localPosition;
                }
                targetVector /= static_cast<float>(children[bone.id].size());
            }
        }

        float len = glm::length(targetVector);
        if (len < 0.01f) len = 0.1f;

        glm::vec3 size(0.1f);
        glm::vec3 offset(0.0f);

        if (!hasChild) {
            size = glm::vec3(0.05f);
        } else {
            offset = targetVector * 0.5f;
            glm::vec3 absDir = glm::abs(targetVector);
            float thickness = glm::clamp(len * 0.25f, 0.05f, 0.15f);

            // Torso/head are thicker
            std::string lower = bone.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("spine") != std::string::npos ||
                lower.find("head") != std::string::npos ||
                lower.find("hip") != std::string::npos) {
                thickness = glm::clamp(len * 0.6f, 0.15f, 0.3f);
            }

            if (absDir.x >= absDir.y && absDir.x >= absDir.z)
                size = glm::vec3(len, thickness, thickness);
            else if (absDir.y >= absDir.x && absDir.y >= absDir.z)
                size = glm::vec3(thickness, len, thickness);
            else
                size = glm::vec3(thickness, thickness, len);
        }

        boneSizes[bone.id] = size;
        boneOffsets[bone.id] = offset;
        float volume = size.x * size.y * size.z;
        boneMasses[bone.id] = std::max(0.5f, volume * 1000.0f);
    }
}

// ============================================================================
// Joint def auto-generation heuristics
// ============================================================================

void CharacterSkeleton::generateJointDefs() {
    jointDefs.clear();

    auto physicsBones = getPhysicsBoneIds();
    // Build a set for quick lookup
    std::set<int> physicsSet(physicsBones.begin(), physicsBones.end());

    for (int boneId : physicsBones) {
        const auto& bone = skeleton.bones[boneId];
        if (bone.parentId < 0) continue; // Root bone has no joint

        // Find the nearest ancestor that's in the physics set
        int parentPhysId = bone.parentId;
        while (parentPhysId >= 0 && physicsSet.find(parentPhysId) == physicsSet.end()) {
            parentPhysId = skeleton.bones[parentPhysId].parentId;
        }
        if (parentPhysId < 0) continue; // No physics ancestor

        CharacterJointDef joint;
        joint.parentBoneId = parentPhysId;
        joint.childBoneId = boneId;

        // Compute anchor points
        // Child anchor: at the top of the child bone (toward parent)
        if (boneOffsets.count(boneId)) {
            // The child's offset points from pivot toward child's center
            // Anchor should be at the "parent end" of the child = -offset direction
            joint.childAnchor = -boneOffsets[boneId];
        }

        // Parent anchor: at the bottom of the parent bone (toward child)
        // Use the child's local position relative to parent
        joint.parentAnchor = bone.localPosition;
        // If parent has an offset, adjust
        if (boneOffsets.count(parentPhysId)) {
            // Bone's local position is relative to parent pivot
            // We need it relative to parent body center
            // parentAnchor = boneLocalPos - parentOffset (since parent body center is at parentOffset from pivot)
        }

        // Determine joint type from bone name heuristics
        std::string lower = bone.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        std::string parentLower = skeleton.bones[parentPhysId].name;
        std::transform(parentLower.begin(), parentLower.end(), parentLower.begin(), ::tolower);

        if (lower.find("head") != std::string::npos ||
            parentLower.find("neck") != std::string::npos) {
            // Neck joint → cone-twist with moderate range
            joint.type = JointType::ConeTwist;
            joint.swingSpan1 = 0.6f;   // ~34° pitch
            joint.swingSpan2 = 0.3f;   // ~17° roll
            joint.twistSpan  = 0.8f;   // ~46° yaw
            joint.motorStrength = 150.0f;
        }
        else if (lower.find("knee") != std::string::npos ||
                 lower.find("shin") != std::string::npos ||
                 lower.find("calf") != std::string::npos ||
                 (lower.find("leg") != std::string::npos && lower.find("lower") != std::string::npos)) {
            // Knee → hinge, bends backward only
            joint.type = JointType::Hinge;
            joint.hingeLimitLow = -2.0f;
            joint.hingeLimitHigh = 0.05f;
            joint.hingeAxis = glm::vec3(0.0f, 0.0f, 1.0f); // lateral axis
            joint.motorStrength = 400.0f;
        }
        else if (lower.find("elbow") != std::string::npos ||
                 lower.find("forearm") != std::string::npos ||
                 (lower.find("arm") != std::string::npos && lower.find("lower") != std::string::npos)) {
            // Elbow → hinge, bends forward only
            joint.type = JointType::Hinge;
            joint.hingeLimitLow = -0.05f;
            joint.hingeLimitHigh = 2.0f;
            joint.hingeAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            joint.motorStrength = 200.0f;
        }
        else if (lower.find("hip") != std::string::npos ||
                 lower.find("thigh") != std::string::npos ||
                 (lower.find("leg") != std::string::npos && lower.find("upper") != std::string::npos) ||
                 (lower.find("leg") != std::string::npos && lower.find("lower") == std::string::npos &&
                  lower.find("shin") == std::string::npos)) {
            // Hip → hinge (simplified; could be cone-twist for full range)
            joint.type = JointType::Hinge;
            joint.hingeLimitLow = -1.2f;
            joint.hingeLimitHigh = 1.2f;
            joint.hingeAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            joint.motorStrength = 400.0f;
        }
        else if (lower.find("shoulder") != std::string::npos ||
                 (lower.find("arm") != std::string::npos && lower.find("upper") != std::string::npos) ||
                 (lower.find("arm") != std::string::npos && lower.find("lower") == std::string::npos &&
                  lower.find("forearm") == std::string::npos)) {
            // Shoulder → cone-twist with moderate range
            joint.type = JointType::ConeTwist;
            joint.swingSpan1 = 0.8f;
            joint.swingSpan2 = 0.6f;
            joint.twistSpan  = 0.3f;
            joint.motorStrength = 200.0f;
        }
        else if (lower.find("spine") != std::string::npos ||
                 lower.find("chest") != std::string::npos) {
            // Spine → cone-twist with limited range (stiff to support upper body)
            joint.type = JointType::ConeTwist;
            joint.swingSpan1 = 0.25f;
            joint.swingSpan2 = 0.25f;
            joint.twistSpan  = 0.15f;
            joint.motorStrength = 500.0f;
        }
        else if (lower.find("foot") != std::string::npos ||
                 lower.find("ankle") != std::string::npos) {
            // Ankle → hinge with limited range
            joint.type = JointType::Hinge;
            joint.hingeLimitLow = -0.4f;
            joint.hingeLimitHigh = 0.4f;
            joint.hingeAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            joint.motorStrength = 150.0f;
        }
        else if (lower.find("hand") != std::string::npos ||
                 lower.find("wrist") != std::string::npos) {
            // Wrist → hinge with limited range
            joint.type = JointType::Hinge;
            joint.hingeLimitLow = -0.4f;
            joint.hingeLimitHigh = 0.4f;
            joint.hingeAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            joint.motorStrength = 100.0f;
        }
        else {
            // Default: cone-twist with moderate range
            joint.type = JointType::ConeTwist;
            joint.swingSpan1 = 0.3f;
            joint.swingSpan2 = 0.3f;
            joint.twistSpan  = 0.2f;
            joint.motorStrength = 250.0f;
        }

        jointDefs[boneId] = joint;
    }
}

} // namespace Scene
} // namespace Phyxel
