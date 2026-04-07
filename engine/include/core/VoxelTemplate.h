#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core { struct InteractionPointDef; }

struct TemplateCube {
    glm::ivec3 relativePos;
    std::string material;
};

struct TemplateSubcube {
    glm::ivec3 parentRelativePos;
    glm::ivec3 subcubePos;
    std::string material;
};

struct TemplateMicrocube {
    glm::ivec3 parentRelativePos;
    glm::ivec3 subcubePos;
    glm::ivec3 microcubePos;
    std::string material;
};

class VoxelTemplate {
public:
    std::string name;
    std::vector<TemplateCube> cubes;
    std::vector<TemplateSubcube> subcubes;
    std::vector<TemplateMicrocube> microcubes;

    /// Canonical facing direction (radians, yaw) at rotation=0.
    /// Parsed from "# facing_yaw: X" header in the .txt file.
    /// yaw=0 → +Z, yaw=π → -Z (BlockBench front convention).
    float facingYaw = 0.0f;

    /// Interaction points parsed from "# interaction:" headers in the .txt file.
    std::vector<Core::InteractionPointDef> interactionPoints;

    /// Absolute path to the source .txt file (for saving back).
    std::string sourceFilePath;

    void addCube(const glm::ivec3& pos, const std::string& mat) {
        cubes.push_back({pos, mat});
    }

    void addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subPos, const std::string& mat) {
        subcubes.push_back({parentPos, subPos, mat});
    }

    void addMicrocube(const glm::ivec3& parentPos, const glm::ivec3& subPos, const glm::ivec3& microPos, const std::string& mat) {
        microcubes.push_back({parentPos, subPos, microPos, mat});
    }
};

} // namespace Phyxel
