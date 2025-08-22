#pragma once

#include "core/Types.h"
#include <btBulletDynamicsCommon.h>
#include <vector>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace VulkanCube {
namespace Physics {

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    // Initialization
    bool initialize();
    void cleanup();

    // World management
    void stepSimulation(float deltaTime, int maxSubSteps = 1, float fixedTimeStep = 1.0f/60.0f);
    void reset();

    // Rigid body management
    btRigidBody* createCube(const glm::vec3& position, const glm::vec3& size = glm::vec3(1.0f), float mass = 1.0f);
    btRigidBody* createCube(const glm::vec3& position, const glm::vec3& size, const std::string& materialName);
    btRigidBody* createBreakawaCube(const glm::vec3& position, const glm::vec3& size, const std::string& materialName); // Special cube with shrunk collision
    btRigidBody* createBreakawaCube(const glm::vec3& position, const glm::vec3& size, float mass); // Special cube with shrunk collision and custom mass
    btRigidBody* createStaticCube(const glm::vec3& position, const glm::vec3& size = glm::vec3(1.0f));
    
    // Deprecated: Ground plane no longer needed - fallen cubes are automatically cleaned up
    // btRigidBody* createGround(const glm::vec3& position = glm::vec3(0, -1, 0), const glm::vec3& size = glm::vec3(50, 1, 50));
    
    void removeCube(btRigidBody* body);
    void removeAllCubes();

    // Bulk operations
    void createCubeGrid(int width, int height, int depth, const glm::vec3& startPos, float spacing = 2.0f);
    void addCubesFromScene(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& sizes);

    // Data extraction for rendering
    void getTransforms(std::vector<glm::mat4>& transforms) const;
    void getCubeStates(std::vector<glm::vec3>& positions, std::vector<glm::quat>& rotations) const;

    // Physics properties
    void setGravity(const glm::vec3& gravity);
    glm::vec3 getGravity() const;
    
    // Debug and information
    int getRigidBodyCount() const;
    void printDebugInfo() const;
    
    // Collision tuning functions
    void optimizeCollisionSettings();
    void configurePenetrationRecovery(float erp = 0.8f, float cfm = 0.0f);
    void tuneContactProcessing(int maxContacts = 4, float contactThreshold = 0.02f);
    void setFallThreshold(float threshold) { fallThreshold = threshold; }
    float getFallThreshold() const { return fallThreshold; }

    // Getters
    btDiscreteDynamicsWorld* getWorld() const { return dynamicsWorld.get(); }
    btBroadphaseInterface* getBroadphase() const { return broadphase.get(); }

private:
    // Bullet Physics components
    std::unique_ptr<btDefaultCollisionConfiguration> collisionConfiguration;
    std::unique_ptr<btCollisionDispatcher> dispatcher;
    std::unique_ptr<btDbvtBroadphase> broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
    std::unique_ptr<btDiscreteDynamicsWorld> dynamicsWorld;

    // Keep track of created bodies for cleanup
    std::vector<btRigidBody*> rigidBodies;
    std::vector<btDefaultMotionState*> motionStates;
    std::vector<btCollisionShape*> collisionShapes; // Dynamically created shapes
    
    // Cleanup settings
    float fallThreshold = -20.0f; // Y position below which cubes are deleted

    // Helper functions
    btRigidBody* createRigidBody(float mass, const btTransform& startTransform, btCollisionShape* shape);
    void cleanupFallenCubes(); // Remove cubes that have fallen below the world
    btTransform glmToBulletTransform(const glm::vec3& position, const glm::quat& rotation = glm::quat(1,0,0,0)) const;
    glm::mat4 bulletToGlmMatrix(const btTransform& transform) const;
    glm::vec3 bulletToGlmVector(const btVector3& vec) const;
    btVector3 glmToBulletVector(const glm::vec3& vec) const;
};

} // namespace Physics
} // namespace VulkanCube
