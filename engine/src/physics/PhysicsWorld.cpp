#include "physics/PhysicsWorld.h"
#include "physics/Material.h"
#include "utils/Logger.h"
#include <iostream>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Phyxel {
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
        broadphase->getOverlappingPairCache()->setInternalGhostPairCallback(new btGhostPairCallback());
        
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
    // Cleanup characters FIRST — they reference the dynamics world
    if (dynamicsWorld) {
        for (auto* character : characters) {
            dynamicsWorld->removeAction(character);
            delete character;
        }
        characters.clear();

        for (auto* ghost : ghostObjects) {
            dynamicsWorld->removeCollisionObject(ghost);
            delete ghost;
        }
        ghostObjects.clear();
    }

    for (auto* shape : characterShapes) {
        delete shape;
    }
    characterShapes.clear();

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
    
    rigidBodies.clear();

    // Bullet world and subsystems cleaned up by unique_ptr destructors (order matters)
    dynamicsWorld.reset();
    solver.reset();
    broadphase.reset();
    dispatcher.reset();
    collisionConfiguration.reset();
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

// ============================================================================
// Consolidated cube creation helper
// All four public cube creation methods delegate to this to eliminate duplication.
// ============================================================================
btRigidBody* PhysicsWorld::createCubeInternal(const CubeCreationParams& params) {
    if (!dynamicsWorld) {
        return nullptr;
    }

    // Apply size shrink factor (creates gaps so broken pieces separate cleanly)
    glm::vec3 adjustedSize = params.size * params.sizeShrinkFactor;

    // Create box collision shape (half-extents)
    btVector3 halfExtents(adjustedSize.x / 2.0f, adjustedSize.y / 2.0f, adjustedSize.z / 2.0f);
    btBoxShape* collisionShape = new btBoxShape(halfExtents);
    collisionShapes.push_back(collisionShape);

    // Transform & motion state
    btTransform startTransform = glmToBulletTransform(params.position);
    btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
    motionStates.push_back(motionState);

    // Local inertia
    btVector3 localInertia(0, 0, 0);
    if (params.mass != 0.0f) {
        collisionShape->calculateLocalInertia(params.mass, localInertia);
    }

    // Construction info with physics properties
    btRigidBody::btRigidBodyConstructionInfo rbInfo(params.mass, motionState, collisionShape, localInertia);
    if (params.mass > 0.0f) {
        rbInfo.m_restitution = params.restitution;
        rbInfo.m_friction = params.friction;
        rbInfo.m_rollingFriction = params.rollingFriction;
    }

    btRigidBody* body = new btRigidBody(rbInfo);

    // Damping (only meaningful for dynamic objects)
    if (params.linearDamping > 0.0f || params.angularDamping > 0.0f) {
        body->setDamping(params.linearDamping, params.angularDamping);
    }

    // Collision margin based on object size
    float objectSize = std::min({adjustedSize.x, adjustedSize.y, adjustedSize.z});
    float appropriateMargin;
    bool isMicrocube = (objectSize < 0.12f);

    if (isMicrocube) {
        appropriateMargin = 0.002f;
    } else if (objectSize <= 0.34f) {
        appropriateMargin = 0.005f;
    } else {
        appropriateMargin = 0.01f;
    }
    collisionShape->setMargin(appropriateMargin);

    // CCD for dynamic objects to prevent tunneling
    if (params.mass > 0.0f) {
        if (isMicrocube) {
            body->setCcdMotionThreshold(objectSize * 0.5f);
            body->setCcdSweptSphereRadius(objectSize * 0.4f);
        } else {
            body->setCcdMotionThreshold(objectSize * 0.5f);
            body->setCcdSweptSphereRadius(objectSize * 0.2f);
        }
    }

    // Add to world & track
    dynamicsWorld->addRigidBody(body);
    rigidBodies.push_back(body);

    // Force activation for immediate overlap resolution
    body->setActivationState(ACTIVE_TAG);
    body->activate(true);
    body->setDeactivationTime(params.deactivationTime);

    // Breakaway cubes get custom material callback for immediate collision processing
    if (params.isBreakaway) {
        body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK);
    }

    return body;
}

btRigidBody* PhysicsWorld::createCube(const glm::vec3& position, const glm::vec3& size, float mass) {
    CubeCreationParams params;
    params.position = position;
    params.size = size;
    params.mass = mass;
    params.sizeShrinkFactor = (mass > 0.0f) ? 0.95f : 1.0f;
    // Default dynamic properties: low bounce, high friction
    params.restitution = 0.2f;
    params.friction = 0.8f;
    params.rollingFriction = 0.3f;
    return createCubeInternal(params);
}

btRigidBody* PhysicsWorld::createCube(const glm::vec3& position, const glm::vec3& size, const std::string& materialName) {
    static Physics::MaterialManager materialManager;
    const auto& material = materialManager.getMaterial(materialName);

    CubeCreationParams params;
    params.position = position;
    params.size = size;
    params.mass = material.mass;
    params.sizeShrinkFactor = 0.95f;
    params.restitution = material.restitution;
    params.friction = material.friction;
    params.rollingFriction = material.friction * 0.5f;
    params.linearDamping = material.linearDamping;
    params.angularDamping = material.angularDamping;
    return createCubeInternal(params);
}

btRigidBody* PhysicsWorld::createBreakawayCube(const glm::vec3& position, const glm::vec3& size, const std::string& materialName) {
    static Physics::MaterialManager materialManager;
    const auto& material = materialManager.getMaterial(materialName);

    CubeCreationParams params;
    params.position = position;
    params.size = size;
    params.mass = material.mass;
    params.sizeShrinkFactor = 0.95f;
    params.restitution = material.restitution;
    params.friction = material.friction;
    params.rollingFriction = material.friction * 0.5f;
    params.linearDamping = material.linearDamping;
    params.angularDamping = material.angularDamping;
    params.deactivationTime = 1.0f;
    params.isBreakaway = true;
    return createCubeInternal(params);
}

btRigidBody* PhysicsWorld::createBreakawayCube(const glm::vec3& position, const glm::vec3& size, float mass) {
    CubeCreationParams params;
    params.position = position;
    params.size = size;
    params.mass = mass;
    params.sizeShrinkFactor = 0.95f;
    params.restitution = 0.3f;
    params.friction = 0.7f;
    params.rollingFriction = 0.35f;
    params.linearDamping = 0.1f;
    params.angularDamping = 0.1f;
    params.deactivationTime = 1.0f;
    params.isBreakaway = true;
    return createCubeInternal(params);
}

btRigidBody* PhysicsWorld::createStaticCube(const glm::vec3& position, const glm::vec3& size) {
    return createCube(position, size, 0.0f); // Mass 0 = static
}

btPairCachingGhostObject* PhysicsWorld::createCharacterGhostObject(const glm::vec3& position, float radius, float height) {
    btTransform startTransform;
    startTransform.setIdentity();
    startTransform.setOrigin(glmToBulletVector(position));

    btConvexShape* capsule = new btCapsuleShape(radius, height);
    characterShapes.push_back(capsule);

    btPairCachingGhostObject* ghostObject = new btPairCachingGhostObject();
    ghostObject->setWorldTransform(startTransform);
    ghostObject->setCollisionShape(capsule);
    ghostObject->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

    dynamicsWorld->addCollisionObject(ghostObject, btBroadphaseProxy::CharacterFilter, btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter);
    
    ghostObjects.push_back(ghostObject);
    return ghostObject;
}

btKinematicCharacterController* PhysicsWorld::createCharacterController(btPairCachingGhostObject* ghostObject, float stepHeight) {
    btKinematicCharacterController* controller = new btKinematicCharacterController(ghostObject, static_cast<btConvexShape*>(ghostObject->getCollisionShape()), stepHeight);
    
    // Configure controller
    controller->setGravity(dynamicsWorld->getGravity());
    controller->setJumpSpeed(10.0f); // Default jump speed
    
    dynamicsWorld->addAction(controller);
    characters.push_back(controller);
    
    return controller;
}

void PhysicsWorld::removeCharacter(btKinematicCharacterController* character) {
    if (!character) return;

    // Find and remove character
    auto it = std::find(characters.begin(), characters.end(), character);
    if (it != characters.end()) {
        dynamicsWorld->removeAction(character);
        
        // Also remove the ghost object
        btPairCachingGhostObject* ghost = character->getGhostObject();
        if (ghost) {
            dynamicsWorld->removeCollisionObject(ghost);
            
            auto ghostIt = std::find(ghostObjects.begin(), ghostObjects.end(), ghost);
            if (ghostIt != ghostObjects.end()) {
                ghostObjects.erase(ghostIt);
            }
            delete ghost;
        }
        
        delete character;
        characters.erase(it);
    }
}

void PhysicsWorld::addConstraint(btTypedConstraint* constraint, bool disableCollisionsBetweenLinkedBodies) {
    if (dynamicsWorld && constraint) {
        dynamicsWorld->addConstraint(constraint, disableCollisionsBetweenLinkedBodies);
    }
}

void PhysicsWorld::removeConstraint(btTypedConstraint* constraint) {
    if (dynamicsWorld && constraint) {
        dynamicsWorld->removeConstraint(constraint);
    }
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
            // Skip bodies marked as external/character parts (UserPointer != nullptr)
            if (body->getUserPointer() != nullptr) {
                continue;
            }

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
} // namespace Phyxel
