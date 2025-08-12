#pragma once
#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>

class Cube {
public:
    glm::ivec3 position;
    glm::vec3 color;
    bool broken = false;

    // Bullet physics
    btRigidBody* rigidBody = nullptr;

    Cube(glm::ivec3 pos, glm::vec3 col)
        : position(pos), color(col), broken(false), rigidBody(nullptr) {}
};

struct Subcube {
    btRigidBody* body;
    glm::vec3 color;
    double spawnTime;
};

// Face visibility flags for each cube
struct CubeFaces {
    bool front  = true;  // +Z face
    bool back   = true;  // -Z face  
    bool right  = true;  // +X face
    bool left   = true;  // -X face
    bool top    = true;  // +Y face
    bool bottom = true;  // -Y face
    
    // Count how many faces are visible
    int getVisibleFaceCount() const {
        return front + back + right + left + top + bottom;
    }
};