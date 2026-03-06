#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {

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
