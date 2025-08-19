#include "physics/PhysicsWorld.h"
#include "physics/Material.h"
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
        
        // Optimize collision settings for better interactions
        optimizeCollisionSettings();
        
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
    
    // Clean up dynamically created collision shapes
    for (auto* shape : collisionShapes) {
        delete shape;
    }
    collisionShapes.clear();
    
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

void PhysicsWorld::stepSimulation(float deltaTime, int maxSubSteps, float fixedTimeStep) {
    if (dynamicsWorld) {
        dynamicsWorld->stepSimulation(deltaTime, maxSubSteps, fixedTimeStep);
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
    if (!dynamicsWorld) {
        return nullptr;
    }
    
    // Create a collision shape based on the size parameter
    // size is the full size, btBoxShape expects half-extents
    btVector3 halfExtents(size.x / 2.0f, size.y / 2.0f, size.z / 2.0f);
    btBoxShape* collisionShape = new btBoxShape(halfExtents);
    collisionShapes.push_back(collisionShape); // Store for cleanup
    
    std::cout << "[PHYSICS] Creating cube with full size (" << size.x << ", " << size.y << ", " << size.z 
              << ") -> half-extents (" << halfExtents.x() << ", " << halfExtents.y() << ", " << halfExtents.z() << ")" << std::endl;
    
    // Create transform
    btTransform startTransform = glmToBulletTransform(position);
    
    // Create motion state
    btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
    motionStates.push_back(motionState);
    
    // Calculate local inertia
    btVector3 localInertia(0, 0, 0);
    if (mass != 0.0f) {
        collisionShape->calculateLocalInertia(mass, localInertia);
    }
    
    // Create rigid body with the correctly sized collision shape
    btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, collisionShape, localInertia);
    
    // Set more realistic physics properties for dynamic objects
    if (mass > 0.0f) {
        rbInfo.m_restitution = 0.2f;  // Low bounce for rock-like behavior
        rbInfo.m_friction = 0.8f;     // High friction so they don't slide around too much
        rbInfo.m_rollingFriction = 0.3f; // Rolling resistance
    }
    
    btRigidBody* body = new btRigidBody(rbInfo);
    
    // Set appropriate collision margin based on object size and type
    float objectSize = std::min({size.x, size.y, size.z});
    float appropriateMargin;
    
    if (objectSize <= 0.25f) {
        // Very small objects (subcubes) - ultra-fine margin
        appropriateMargin = 0.002f;
    } else if (objectSize < 1.0f) {
        // Small objects - fine margin
        appropriateMargin = 0.005f;
    } else {
        // Regular objects - standard margin  
        appropriateMargin = 0.01f;
    }
    
    collisionShape->setMargin(appropriateMargin);
    std::cout << "[COLLISION] Set collision margin " << appropriateMargin 
              << " for object size " << objectSize << std::endl;
    
    // Add to world
    dynamicsWorld->addRigidBody(body);
    rigidBodies.push_back(body);
    
    return body;
}

btRigidBody* PhysicsWorld::createCube(const glm::vec3& position, const glm::vec3& size, const std::string& materialName) {
    if (!dynamicsWorld) {
        return nullptr;
    }
    
    // Get material properties
    static Physics::MaterialManager materialManager;
    const auto& material = materialManager.getMaterial(materialName);
    
    // Create a collision shape based on the size parameter
    // size is the full size, btBoxShape expects half-extents
    btVector3 halfExtents(size.x / 2.0f, size.y / 2.0f, size.z / 2.0f);
    btBoxShape* collisionShape = new btBoxShape(halfExtents);
    collisionShapes.push_back(collisionShape); // Store for cleanup
    
    std::cout << "[PHYSICS] Creating cube with material '" << materialName << "' at (" 
              << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
    
    // Create transform
    btTransform startTransform = glmToBulletTransform(position);
    
    // Create motion state
    btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
    motionStates.push_back(motionState);
    
    // Calculate local inertia
    btVector3 localInertia(0, 0, 0);
    if (material.mass != 0.0f) {
        collisionShape->calculateLocalInertia(material.mass, localInertia);
    }
    
    // Create rigid body with material properties
    btRigidBody::btRigidBodyConstructionInfo rbInfo(material.mass, motionState, collisionShape, localInertia);
    
    // Apply material physics properties
    rbInfo.m_restitution = material.restitution;
    rbInfo.m_friction = material.friction;
    rbInfo.m_rollingFriction = material.friction * 0.5f; // Rolling friction as fraction of surface friction
    
    btRigidBody* body = new btRigidBody(rbInfo);
    
    // Apply damping
    body->setDamping(material.linearDamping, material.angularDamping);
    
    // Set appropriate collision margin based on object size and type
    float objectSize = std::min({size.x, size.y, size.z});
    float appropriateMargin;
    
    if (objectSize <= 0.25f) {
        // Very small objects (subcubes) - ultra-fine margin
        appropriateMargin = 0.002f;
    } else if (objectSize < 1.0f) {
        // Small objects - fine margin
        appropriateMargin = 0.005f;
    } else {
        // Regular objects - standard margin  
        appropriateMargin = 0.01f;
    }
    
    collisionShape->setMargin(appropriateMargin);
    std::cout << "[COLLISION] Set collision margin " << appropriateMargin 
              << " for material object size " << objectSize << std::endl;
    
    // Add to world
    dynamicsWorld->addRigidBody(body);
    rigidBodies.push_back(body);
    
    std::cout << "[MATERIAL] Applied '" << materialName << "' properties: mass=" << material.mass 
              << ", friction=" << material.friction << ", restitution=" << material.restitution << std::endl;
    
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

// =============================================================================
// COLLISION TUNING FUNCTIONS
// =============================================================================

void PhysicsWorld::optimizeCollisionSettings() {
    if (!dynamicsWorld) {
        return;
    }
    
    std::cout << "[COLLISION] Optimizing collision settings for better object interactions..." << std::endl;
    
    // Configure penetration recovery (Error Reduction Parameter and Constraint Force Mixing)
    configurePenetrationRecovery(0.8f, 0.0001f);
    
    // Set proper collision margins
    setCollisionMargins(0.005f, 0.02f);
    
    // Tune contact processing
    tuneContactProcessing(4, 0.01f);
    
    // Configure dynamics world solver settings for better stability
    auto& solverInfo = dynamicsWorld->getSolverInfo();
    solverInfo.m_numIterations = 10; // More iterations for stability
    solverInfo.m_solverMode |= SOLVER_USE_2_FRICTION_DIRECTIONS; // Better friction
    solverInfo.m_solverMode |= SOLVER_USE_WARMSTARTING; // Faster convergence
    
    std::cout << "[COLLISION] Collision optimization complete" << std::endl;
}

void PhysicsWorld::setCollisionMargins(float dynamicMargin, float staticMargin) {
    std::cout << "[COLLISION] Setting collision margins - Dynamic: " << dynamicMargin 
              << ", Static: " << staticMargin << std::endl;
    
    // Apply margins to existing shapes
    if (cubeShape) {
        cubeShape->setMargin(dynamicMargin);
    }
    if (groundShape) {
        groundShape->setMargin(staticMargin);
    }
    
    // Note: For new shapes, margins will be set in createCube functions
}

void PhysicsWorld::configurePenetrationRecovery(float erp, float cfm) {
    if (!dynamicsWorld) {
        return;
    }
    
    std::cout << "[COLLISION] Configuring penetration recovery - ERP: " << erp 
              << ", CFM: " << cfm << std::endl;
    
    // Error Reduction Parameter - how quickly penetrations are resolved (0.0 to 1.0)
    dynamicsWorld->getSolverInfo().m_erp = erp;
    
    // Constraint Force Mixing - softness of contacts (usually very small or 0)
    dynamicsWorld->getSolverInfo().m_globalCfm = cfm;
    
    // Contact ERP - specific to contact constraints
    dynamicsWorld->getSolverInfo().m_erp2 = erp * 0.8f; // Slightly softer for contacts
}

void PhysicsWorld::tuneContactProcessing(int maxContacts, float contactThreshold) {
    if (!dynamicsWorld) {
        return;
    }
    
    std::cout << "[COLLISION] Tuning contact processing - Max contacts: " << maxContacts 
              << ", Threshold: " << contactThreshold << std::endl;
    
    // Configure contact processing parameters (safer approach)
    dynamicsWorld->getDispatchInfo().m_enableSatConvex = true; // Better convex collision detection
    dynamicsWorld->getDispatchInfo().m_enableSPU = false; // Disable SPU processing for consistency
    
    // Set contact cache settings
    dynamicsWorld->getSolverInfo().m_splitImpulse = true; // Split position and velocity solving
    dynamicsWorld->getSolverInfo().m_splitImpulsePenetrationThreshold = -0.02f; // Threshold for split impulse
    dynamicsWorld->getSolverInfo().m_splitImpulseTurnErp = 0.1f; // Turn ERP for split impulse
}

} // namespace Physics
} // namespace VulkanCube
