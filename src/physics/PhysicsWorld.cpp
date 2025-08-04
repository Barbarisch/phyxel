#include "physics/PhysicsWorld.h"
#include <iostream>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace VulkanCube {
namespace Physics {

PhysicsWorld::PhysicsWorld() {
    // Constructor
}

PhysicsWorld::~PhysicsWorld() {
    cleanup();
}

bool PhysicsWorld::initialize() {
    try {
        // Create collision configuration
        collisionConfiguration = std::make_unique<btDefaultCollisionConfiguration>();
        
        // Create dispatcher
        dispatcher = std::make_unique<btCollisionDispatcher>(collisionConfiguration.get());
        
        // Create broadphase
        broadphase = std::make_unique<btDbvtBroadphase>();
        
        // Create solver
        solver = std::make_unique<btSequentialImpulseConstraintSolver>();
        
        // Create dynamics world
        dynamicsWorld = std::make_unique<btDiscreteDynamicsWorld>(
            dispatcher.get(), 
            broadphase.get(), 
            solver.get(), 
            collisionConfiguration.get()
        );
        
        // Set gravity
        dynamicsWorld->setGravity(btVector3(0, -9.81f, 0));
        
        // Create reusable collision shapes
        cubeShape = std::make_unique<btBoxShape>(btVector3(0.5f, 0.5f, 0.5f)); // 1x1x1 cube
        groundShape = std::make_unique<btBoxShape>(btVector3(50.0f, 0.5f, 50.0f)); // Large ground plane
        
        std::cout << "Physics world initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize physics world: " << e.what() << std::endl;
        return false;
    }
}

void PhysicsWorld::cleanup() {
    // Remove all rigid bodies
    removeAllCubes();
    
    // Clean up motion states
    for (auto* motionState : motionStates) {
        delete motionState;
    }
    motionStates.clear();
    
    // Bullet objects will be cleaned up by unique_ptr destructors
    dynamicsWorld.reset();
    solver.reset();
    broadphase.reset();
    dispatcher.reset();
    collisionConfiguration.reset();
    
    cubeShape.reset();
    groundShape.reset();
    
    rigidBodies.clear();
}

void PhysicsWorld::stepSimulation(float deltaTime, int maxSubSteps) {
    if (dynamicsWorld) {
        dynamicsWorld->stepSimulation(deltaTime, maxSubSteps);
    }
}

void PhysicsWorld::reset() {
    removeAllCubes();
    
    // Reset gravity
    if (dynamicsWorld) {
        dynamicsWorld->setGravity(btVector3(0, -9.81f, 0));
    }
}

btRigidBody* PhysicsWorld::createCube(const glm::vec3& position, const glm::vec3& size, float mass) {
    if (!dynamicsWorld || !cubeShape) {
        return nullptr;
    }
    
    // Create transform
    btTransform startTransform = glmToBulletTransform(position);
    
    // Create motion state
    btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
    motionStates.push_back(motionState);
    
    // Calculate local inertia
    btVector3 localInertia(0, 0, 0);
    if (mass != 0.0f) {
        cubeShape->calculateLocalInertia(mass, localInertia);
    }
    
    // Create rigid body
    btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, cubeShape.get(), localInertia);
    btRigidBody* body = new btRigidBody(rbInfo);
    
    // Add to world
    dynamicsWorld->addRigidBody(body);
    rigidBodies.push_back(body);
    
    return body;
}

btRigidBody* PhysicsWorld::createStaticCube(const glm::vec3& position, const glm::vec3& size) {
    return createCube(position, size, 0.0f); // Mass 0 = static
}

btRigidBody* PhysicsWorld::createGround(const glm::vec3& position, const glm::vec3& size) {
    if (!dynamicsWorld || !groundShape) {
        return nullptr;
    }
    
    btTransform groundTransform = glmToBulletTransform(position);
    
    btDefaultMotionState* motionState = new btDefaultMotionState(groundTransform);
    motionStates.push_back(motionState);
    
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, motionState, groundShape.get(), btVector3(0, 0, 0));
    btRigidBody* groundBody = new btRigidBody(rbInfo);
    
    dynamicsWorld->addRigidBody(groundBody);
    rigidBodies.push_back(groundBody);
    
    return groundBody;
}

void PhysicsWorld::removeCube(btRigidBody* body) {
    if (!dynamicsWorld || !body) {
        return;
    }
    
    // Remove from world
    dynamicsWorld->removeRigidBody(body);
    
    // Remove from our tracking
    auto it = std::find(rigidBodies.begin(), rigidBodies.end(), body);
    if (it != rigidBodies.end()) {
        rigidBodies.erase(it);
    }
    
    // Clean up motion state
    btMotionState* motionState = body->getMotionState();
    if (motionState) {
        auto msIt = std::find(motionStates.begin(), motionStates.end(), motionState);
        if (msIt != motionStates.end()) {
            motionStates.erase(msIt);
            delete motionState;
        }
    }
    
    // Delete body
    delete body;
}

void PhysicsWorld::removeAllCubes() {
    if (!dynamicsWorld) {
        return;
    }
    
    // Remove all rigid bodies from world
    for (btRigidBody* body : rigidBodies) {
        dynamicsWorld->removeRigidBody(body);
        delete body;
    }
    rigidBodies.clear();
    
    // Clean up motion states
    for (btDefaultMotionState* motionState : motionStates) {
        delete motionState;
    }
    motionStates.clear();
}

void PhysicsWorld::createCubeGrid(int width, int height, int depth, const glm::vec3& startPos, float spacing) {
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            for (int z = 0; z < depth; ++z) {
                glm::vec3 position = startPos + glm::vec3(x * spacing, y * spacing, z * spacing);
                createCube(position);
            }
        }
    }
    
    std::cout << "Created cube grid: " << width << "x" << height << "x" << depth << " = " << (width * height * depth) << " cubes" << std::endl;
}

void PhysicsWorld::addCubesFromScene(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& sizes) {
    size_t count = std::min(positions.size(), sizes.size());
    
    for (size_t i = 0; i < count; ++i) {
        createCube(positions[i], sizes[i]);
    }
    
    std::cout << "Added " << count << " cubes from scene data" << std::endl;
}

void PhysicsWorld::getTransforms(std::vector<glm::mat4>& transforms) const {
    transforms.clear();
    transforms.reserve(rigidBodies.size());
    
    for (const btRigidBody* body : rigidBodies) {
        btTransform transform;
        body->getMotionState()->getWorldTransform(transform);
        transforms.push_back(bulletToGlmMatrix(transform));
    }
}

void PhysicsWorld::getCubeStates(std::vector<glm::vec3>& positions, std::vector<glm::quat>& rotations) const {
    positions.clear();
    rotations.clear();
    
    positions.reserve(rigidBodies.size());
    rotations.reserve(rigidBodies.size());
    
    for (const btRigidBody* body : rigidBodies) {
        btTransform transform;
        body->getMotionState()->getWorldTransform(transform);
        
        // Extract position
        btVector3 pos = transform.getOrigin();
        positions.push_back(glm::vec3(pos.getX(), pos.getY(), pos.getZ()));
        
        // Extract rotation
        btQuaternion rot = transform.getRotation();
        rotations.push_back(glm::quat(rot.getW(), rot.getX(), rot.getY(), rot.getZ()));
    }
}

void PhysicsWorld::setGravity(const glm::vec3& gravity) {
    if (dynamicsWorld) {
        dynamicsWorld->setGravity(glmToBulletVector(gravity));
    }
}

glm::vec3 PhysicsWorld::getGravity() const {
    if (dynamicsWorld) {
        return bulletToGlmVector(dynamicsWorld->getGravity());
    }
    return glm::vec3(0.0f);
}

int PhysicsWorld::getRigidBodyCount() const {
    return static_cast<int>(rigidBodies.size());
}

void PhysicsWorld::printDebugInfo() const {
    std::cout << "Physics World Debug Info:" << std::endl;
    std::cout << "  Rigid bodies: " << rigidBodies.size() << std::endl;
    std::cout << "  Motion states: " << motionStates.size() << std::endl;
    
    if (dynamicsWorld) {
        std::cout << "  World objects: " << dynamicsWorld->getNumCollisionObjects() << std::endl;
        btVector3 gravity = dynamicsWorld->getGravity();
        std::cout << "  Gravity: (" << gravity.getX() << ", " << gravity.getY() << ", " << gravity.getZ() << ")" << std::endl;
    }
}

btRigidBody* PhysicsWorld::createRigidBody(float mass, const btTransform& startTransform, btCollisionShape* shape) {
    btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
    motionStates.push_back(motionState);
    
    btVector3 localInertia(0, 0, 0);
    if (mass != 0.0f) {
        shape->calculateLocalInertia(mass, localInertia);
    }
    
    btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, shape, localInertia);
    btRigidBody* body = new btRigidBody(rbInfo);
    
    dynamicsWorld->addRigidBody(body);
    rigidBodies.push_back(body);
    
    return body;
}

btTransform PhysicsWorld::glmToBulletTransform(const glm::vec3& position, const glm::quat& rotation) const {
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(position.x, position.y, position.z));
    transform.setRotation(btQuaternion(rotation.x, rotation.y, rotation.z, rotation.w));
    return transform;
}

glm::mat4 PhysicsWorld::bulletToGlmMatrix(const btTransform& transform) const {
    btScalar matrix[16];
    transform.getOpenGLMatrix(matrix);
    
    return glm::mat4(
        matrix[0], matrix[1], matrix[2], matrix[3],
        matrix[4], matrix[5], matrix[6], matrix[7],
        matrix[8], matrix[9], matrix[10], matrix[11],
        matrix[12], matrix[13], matrix[14], matrix[15]
    );
}

glm::vec3 PhysicsWorld::bulletToGlmVector(const btVector3& vec) const {
    return glm::vec3(vec.getX(), vec.getY(), vec.getZ());
}

btVector3 PhysicsWorld::glmToBulletVector(const glm::vec3& vec) const {
    return btVector3(vec.x, vec.y, vec.z);
}

} // namespace Physics
} // namespace VulkanCube
