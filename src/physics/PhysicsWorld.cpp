#include "physics/PhysicsWorld.h"
#include "physics/Material.h"
#include "utils/Logger.h"
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
        
        LOG_INFO_FMT("Physics", "Physics world initialized successfully (with automatic fallen cube cleanup at Y < " 
                  << fallThreshold << ")");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR_FMT("Physics", "Failed to initialize physics world: " << e.what());
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
    
    // Note: All collision shapes are created per-cube and cleaned up via collisionShapes vector
    
    rigidBodies.clear();
}

void PhysicsWorld::stepSimulation(float deltaTime, int maxSubSteps, float fixedTimeStep) {
    if (dynamicsWorld) {
        dynamicsWorld->stepSimulation(deltaTime, maxSubSteps, fixedTimeStep);
        
        // Clean up cubes that have fallen below the world
        cleanupFallenCubes();
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
    
    // Make dynamic cubes slightly smaller than static counterparts to prevent embedding
    // This creates natural gaps that allow broken pieces to separate cleanly
    glm::vec3 adjustedSize = size;
    if (mass > 0.0f) { // Only reduce size for dynamic objects
        const float dynamicSizeReduction = 0.95f; // 5% smaller than static cubes
        adjustedSize = size * dynamicSizeReduction;
        // std::cout << "[PHYSICS] Creating dynamic cube with reduced size (" << adjustedSize.x << ", " << adjustedSize.y << ", " << adjustedSize.z 
        //           << ") - 95% of original (" << size.x << ", " << size.y << ", " << size.z << ") for gap prevention" << std::endl;
    } else {
        //std::cout << "[PHYSICS] Creating static cube with full size (" << adjustedSize.x << ", " << adjustedSize.y << ", " << adjustedSize.z << ")" << std::endl;
    }
    
    // Create a collision shape based on the adjusted size parameter
    // adjustedSize is the full size, btBoxShape expects half-extents
    btVector3 halfExtents(adjustedSize.x / 2.0f, adjustedSize.y / 2.0f, adjustedSize.z / 2.0f);
    btBoxShape* collisionShape = new btBoxShape(halfExtents);
    collisionShapes.push_back(collisionShape); // Store for cleanup
    
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
    
    // Set appropriate collision margin based on cube type
    float objectSize = std::min({size.x, size.y, size.z});
    float appropriateMargin;
    
    if (objectSize <= 0.34f) {  // Subcubes are 1/3 scale ≈ 0.333
        // Subcubes - smaller margin for precision
        appropriateMargin = 0.005f;
    } else {
        // Regular cubes - standard margin
        appropriateMargin = 0.01f;
    }
    
    collisionShape->setMargin(appropriateMargin);
    // std::cout << "[COLLISION] Set collision margin " << appropriateMargin 
    //           << " for " << (objectSize <= 0.34f ? "subcube" : "regular cube") 
    //           << " size " << objectSize << std::endl;
    
    // Add to world
    dynamicsWorld->addRigidBody(body);
    rigidBodies.push_back(body);
    
    // Force activation to ensure immediate overlap resolution when spawned at exact positions
    body->setActivationState(ACTIVE_TAG);
    body->activate(true);
    body->setDeactivationTime(0.5f); // Stay active longer to resolve overlaps
    
    return body;
}

btRigidBody* PhysicsWorld::createCube(const glm::vec3& position, const glm::vec3& size, const std::string& materialName) {
    if (!dynamicsWorld) {
        return nullptr;
    }
    
    // Get material properties
    static Physics::MaterialManager materialManager;
    const auto& material = materialManager.getMaterial(materialName);
    
    // Make dynamic cubes slightly smaller than static counterparts to prevent embedding
    // This creates natural gaps that allow broken pieces to separate cleanly
    const float dynamicSizeReduction = 0.95f; // 5% smaller than static cubes
    glm::vec3 adjustedSize = size * dynamicSizeReduction;
    
    // Create a collision shape based on the adjusted size parameter
    // adjustedSize is the full size, btBoxShape expects half-extents
    btVector3 halfExtents(adjustedSize.x / 2.0f, adjustedSize.y / 2.0f, adjustedSize.z / 2.0f);
    btBoxShape* collisionShape = new btBoxShape(halfExtents);
    collisionShapes.push_back(collisionShape); // Store for cleanup
    
    // std::cout << "[PHYSICS] Creating dynamic cube with material '" << materialName << "' at (" 
    //           << position.x << ", " << position.y << ", " << position.z 
    //           << ") - size reduced to " << (dynamicSizeReduction * 100) << "% of original for gap prevention" << std::endl;
    
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
    
    // Set appropriate collision margin based on cube type
    float objectSize = std::min({size.x, size.y, size.z});
    float appropriateMargin;
    
    if (objectSize <= 0.34f) {  // Subcubes are 1/3 scale ≈ 0.333
        // Subcubes - smaller margin for precision
        appropriateMargin = 0.005f;
    } else {
        // Regular cubes - standard margin
        appropriateMargin = 0.01f;
    }
    
    collisionShape->setMargin(appropriateMargin);
    // std::cout << "[COLLISION] Set collision margin " << appropriateMargin 
    //           << " for " << (objectSize <= 0.34f ? "subcube" : "regular cube") 
    //           << " size " << objectSize << std::endl;
    
    // Add to world
    dynamicsWorld->addRigidBody(body);
    rigidBodies.push_back(body);
    
    // Force activation to ensure immediate overlap resolution when spawned at exact positions
    body->setActivationState(ACTIVE_TAG);
    body->activate(true);
    body->setDeactivationTime(0.5f); // Stay active longer to resolve overlaps
    
    // std::cout << "[MATERIAL] Applied '" << materialName << "' properties: mass=" << material.mass 
    //           << ", friction=" << material.friction << ", restitution=" << material.restitution 
    //           << " (forced activation for overlap resolution)" << std::endl;
    
    return body;
}

btRigidBody* PhysicsWorld::createBreakawaCube(const glm::vec3& position, const glm::vec3& size, const std::string& materialName) {
    if (!dynamicsWorld) {
        return nullptr;
    }
    
    // Get material properties
    static Physics::MaterialManager materialManager;
    const auto& material = materialManager.getMaterial(materialName);
    
    // Create a collision shape that's slightly smaller than the visual size to prevent embedding
    // This gives the physics engine room to separate the cube from surrounding geometry
    float shrinkFactor = 0.95f; // 5% smaller collision shape
    glm::vec3 shrunkSize = size * shrinkFactor;
    btVector3 halfExtents(shrunkSize.x / 2.0f, shrunkSize.y / 2.0f, shrunkSize.z / 2.0f);
    btBoxShape* collisionShape = new btBoxShape(halfExtents);
    collisionShapes.push_back(collisionShape); // Store for cleanup
    
    // std::cout << "[PHYSICS] Creating breakaway cube with material '" << materialName << "' at (" 
    //           << position.x << ", " << position.y << ", " << position.z << ")" 
    //           << " with " << (shrinkFactor * 100) << "% collision size for gap creation" << std::endl;
    
    // Create transform
    btTransform startTransform = glmToBulletTransform(position);
    
    // Create motion state
    btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
    motionStates.push_back(motionState);
    
    // Calculate local inertia based on the full mass (not the shrunk collision size)
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
    
    // Set appropriate collision margin based on cube type
    float objectSize = std::min({shrunkSize.x, shrunkSize.y, shrunkSize.z});
    float appropriateMargin = (objectSize <= 0.32f) ? 0.005f : 0.01f; // Subcube vs regular cube (accounting for 95% shrink)
    
    collisionShape->setMargin(appropriateMargin);
    // std::cout << "[COLLISION] Set collision margin " << appropriateMargin 
    //           << " for " << (objectSize <= 0.32f ? "subcube" : "regular cube") 
    //           << " breakaway size " << objectSize << std::endl;
    
    // Add to world with normal collision (no special filtering)
    // Dynamic cubes should collide with everything including static chunks and other dynamic cubes
    dynamicsWorld->addRigidBody(body);
    rigidBodies.push_back(body);
    
    //std::cout << "[COLLISION] Dynamic cube added with normal collision - can collide with static chunks and other cubes" << std::endl;
    
    // Force activation and set collision flags for immediate separation
    body->setActivationState(ACTIVE_TAG);
    body->activate(true);
    body->setDeactivationTime(1.0f); // Stay active longer for breakaway cubes
    
    // Set collision flags to ensure it processes collisions immediately
    body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK);
    
    // std::cout << "[MATERIAL] Applied '" << materialName << "' breakaway properties: mass=" << material.mass 
    //           << ", friction=" << material.friction << ", restitution=" << material.restitution 
    //           << " (shrunk collision for gap creation)" << std::endl;
    
    return body;
}

btRigidBody* PhysicsWorld::createBreakawaCube(const glm::vec3& position, const glm::vec3& size, float mass) {
    if (!dynamicsWorld) {
        return nullptr;
    }
    
    // Create a collision shape that's slightly smaller than the visual size to prevent embedding
    // This gives the physics engine room to separate the cube from surrounding geometry
    float shrinkFactor = 0.95f; // 5% smaller collision shape
    glm::vec3 shrunkSize = size * shrinkFactor;
    btVector3 halfExtents(shrunkSize.x / 2.0f, shrunkSize.y / 2.0f, shrunkSize.z / 2.0f);
    btBoxShape* collisionShape = new btBoxShape(halfExtents);
    collisionShapes.push_back(collisionShape); // Store for cleanup
    
    // std::cout << "[PHYSICS] Creating breakaway cube with mass " << mass << " at (" 
    //           << position.x << ", " << position.y << ", " << position.z << ")" 
    //           << " with " << (shrinkFactor * 100) << "% collision size for gap creation" << std::endl;
    
    // Create transform
    btTransform startTransform = glmToBulletTransform(position);
    
    // Create motion state
    btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
    motionStates.push_back(motionState);
    
    // Calculate local inertia based on the mass
    btVector3 localInertia(0, 0, 0);
    if (mass != 0.0f) {
        collisionShape->calculateLocalInertia(mass, localInertia);
    }
    
    // Create rigid body with default properties
    btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, collisionShape, localInertia);
    
    // Apply reasonable default physics properties for breakaway cubes
    rbInfo.m_restitution = 0.3f;  // Some bounce
    rbInfo.m_friction = 0.7f;     // Good friction
    rbInfo.m_rollingFriction = 0.35f; // Rolling friction
    
    btRigidBody* body = new btRigidBody(rbInfo);
    
    // Apply damping for stability
    body->setDamping(0.1f, 0.1f); // Light damping
    
    // Set appropriate collision margin based on cube type
    float objectSize = std::min({shrunkSize.x, shrunkSize.y, shrunkSize.z});
    float appropriateMargin = (objectSize <= 0.32f) ? 0.005f : 0.01f; // Subcube vs regular cube (accounting for 95% shrink)
    
    collisionShape->setMargin(appropriateMargin);
    // std::cout << "[COLLISION] Set collision margin " << appropriateMargin 
    //           << " for " << (objectSize <= 0.32f ? "subcube" : "regular cube") 
    //           << " breakaway size " << objectSize << std::endl;
    
    // Add to world
    dynamicsWorld->addRigidBody(body);
    rigidBodies.push_back(body);
    
    // Force activation and set collision flags for immediate separation
    body->setActivationState(ACTIVE_TAG);
    body->activate(true);
    body->setDeactivationTime(1.0f); // Stay active longer for breakaway cubes
    
    // Set collision flags to ensure it processes collisions immediately
    body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK);
    
    // std::cout << "[MATERIAL] Applied breakaway properties: mass=" << mass 
    //           << ", friction=0.7, restitution=0.3 (shrunk collision for gap creation)" << std::endl;
    
    return body;
}

btRigidBody* PhysicsWorld::createStaticCube(const glm::vec3& position, const glm::vec3& size) {
    return createCube(position, size, 0.0f); // Mass 0 = static
}

void PhysicsWorld::removeCube(btRigidBody* body) {
    if (!dynamicsWorld || !body) {
        return;
    }
    
    // Additional safety check: verify the body is actually in our tracking list
    auto bodyIt = std::find(rigidBodies.begin(), rigidBodies.end(), body);
    if (bodyIt == rigidBodies.end()) {
        //std::cout << "[PHYSICS] Warning: Attempted to remove rigid body not in tracking list" << std::endl;
        return; // Body not tracked by us, don't try to remove it
    }
    
    // Remove from world first (this can throw if body is invalid)
    try {
        dynamicsWorld->removeRigidBody(body);
    } catch (...) {
        //std::cout << "[PHYSICS] Error: Failed to remove rigid body from dynamics world" << std::endl;
        // Continue with cleanup anyway
    }
    
    // Remove from our tracking
    rigidBodies.erase(bodyIt);
    
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
    
    LOG_INFO_FMT("Physics", "Created cube grid: " << width << "x" << height << "x" << depth << " = " << (width * height * depth) << " cubes");
}

void PhysicsWorld::addCubesFromScene(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& sizes) {
    size_t count = std::min(positions.size(), sizes.size());
    
    for (size_t i = 0; i < count; ++i) {
        createCube(positions[i], sizes[i]);
    }
    
    LOG_INFO_FMT("Physics", "Added " << count << " cubes from scene data");
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
    LOG_DEBUG("Physics", "Physics World Debug Info:");
    LOG_DEBUG_FMT("Physics", "  Rigid bodies: " << rigidBodies.size());
    LOG_DEBUG_FMT("Physics", "  Motion states: " << motionStates.size());
    
    if (dynamicsWorld) {
        LOG_DEBUG_FMT("Physics", "  World objects: " << dynamicsWorld->getNumCollisionObjects());
        btVector3 gravity = dynamicsWorld->getGravity();
        LOG_DEBUG_FMT("Physics", "  Gravity: (" << gravity.getX() << ", " << gravity.getY() << ", " << gravity.getZ() << ")");
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
    
    //std::cout << "[COLLISION] Optimizing collision settings for exact-position spawning..." << std::endl;
    
    // Configure penetration recovery with strong settings to handle exact-position spawning
    configurePenetrationRecovery(0.9f, 0.000001f); // High ERP for strong overlap resolution
    
    // Tune contact processing with better settings
    tuneContactProcessing(6, 0.004f); // Good balance of contacts and performance
    
    // Configure dynamics world solver settings for strong overlap resolution
    auto& solverInfo = dynamicsWorld->getSolverInfo();
    solverInfo.m_numIterations = 20; // Increased from 15 for better overlap resolution
    solverInfo.m_solverMode |= SOLVER_USE_2_FRICTION_DIRECTIONS; // Better friction
    solverInfo.m_solverMode |= SOLVER_USE_WARMSTARTING; // Faster convergence
    solverInfo.m_solverMode |= SOLVER_RANDMIZE_ORDER; // Better convergence
    
    // Enhanced penetration recovery settings for exact-position spawning
    solverInfo.m_splitImpulse = true; // Separate position and velocity solving
    solverInfo.m_splitImpulsePenetrationThreshold = -0.01f; // Aggressive penetration threshold
    solverInfo.m_erp2 = 0.85f; // Strong split impulse error reduction
    
    // Enhanced contact processing for overlap resolution
    dynamicsWorld->getDispatchInfo().m_allowedCcdPenetration = 0.0005f; // Very low allowed penetration
    
    //std::cout << "[COLLISION] Exact-position spawning optimization complete - 20 iterations, 90% ERP, strong overlap resolution" << std::endl;
}

void PhysicsWorld::configurePenetrationRecovery(float erp, float cfm) {
    if (!dynamicsWorld) {
        return;
    }
    
    // std::cout << "[COLLISION] Configuring penetration recovery - ERP: " << erp 
    //           << ", CFM: " << cfm << std::endl;
    
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
    
    // std::cout << "[COLLISION] Tuning contact processing - Max contacts: " << maxContacts 
    //           << ", Threshold: " << contactThreshold << std::endl;
    
    // Configure contact processing parameters (safer approach)
    dynamicsWorld->getDispatchInfo().m_enableSatConvex = true; // Better convex collision detection
    dynamicsWorld->getDispatchInfo().m_enableSPU = false; // Disable SPU processing for consistency
    
    // Set contact cache settings
    dynamicsWorld->getSolverInfo().m_splitImpulse = true; // Split position and velocity solving
    dynamicsWorld->getSolverInfo().m_splitImpulsePenetrationThreshold = -0.02f; // Threshold for split impulse
    dynamicsWorld->getSolverInfo().m_splitImpulseTurnErp = 0.1f; // Turn ERP for split impulse
}

void PhysicsWorld::cleanupFallenCubes() {
    if (!dynamicsWorld) {
        return;
    }
    
    std::vector<btRigidBody*> bodiesToRemove;
    
    // Check all rigid bodies for ones that have fallen too far
    for (btRigidBody* body : rigidBodies) {
        if (!body) continue;
        
        // Only check dynamic bodies (mass > 0) - leave static chunks alone
        if (body->getMass() > 0.0f) {
            btTransform transform;
            body->getMotionState()->getWorldTransform(transform);
            float yPosition = transform.getOrigin().getY();
            
            if (yPosition < fallThreshold) {
                bodiesToRemove.push_back(body);
            }
        }
    }
    
    // Remove fallen cubes
    if (!bodiesToRemove.empty()) {
        LOG_INFO_FMT("Physics", "[CLEANUP] Removing " << bodiesToRemove.size() 
                  << " fallen cubes below Y = " << fallThreshold);
        
        for (btRigidBody* body : bodiesToRemove) {
            removeCube(body);
        }
    }
}

} // namespace Physics
} // namespace VulkanCube
