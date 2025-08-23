#pragma once

#include "Types.h"
#include "core/Subcube.h"
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vulkan/vulkan.h>

// Bullet Physics forward declarations (global namespace)
class btRigidBody;
class btCollisionShape;
class btCompoundShape;

namespace VulkanCube {

// Forward declarations
namespace Physics {
    class PhysicsWorld;
}

/**
 * Chunk class that manages a 32x32x32 section of cubes
 * Handles cube storage, face generation, and Vulkan buffer management
 */
class Chunk {
    friend class ChunkManager;  // Allow ChunkManager to access private members for cross-chunk culling
    
private:
    std::vector<Cube*> cubes;                      // Pointers to cubes for efficient deletion (32x32x32)
    std::vector<Subcube*> staticSubcubes;          // Static subcubes (part of chunk physics body)
    std::vector<InstanceData> faces;               // Visible faces only (CPU pre-filtered for rendering)
    VkBuffer instanceBuffer = VK_NULL_HANDLE;      // Vulkan buffer for this chunk's face instance data
    VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
    void* mappedMemory = nullptr;                  // Persistent mapping for updates
    uint32_t numInstances = 0;                     // Variable count based on visible faces
    glm::ivec3 worldOrigin = glm::ivec3(0);        // World-space origin of this chunk
    bool needsUpdate = false;                      // Flag for buffer updates
    
    // Buffer capacity management
    static constexpr size_t DEFAULT_BUFFER_CAPACITY = 25000;   // Realistic capacity based on testing (8x current usage)
    size_t bufferCapacity = 0;                     // Current allocated buffer capacity
    
    // Vulkan device handles (set by ChunkManager)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    
    // Physics body for static geometry (compound shape made from individual cube collision boxes)
    mutable btRigidBody* chunkPhysicsBody = nullptr;
    mutable btCompoundShape* chunkCollisionShape = nullptr;  // Changed from btCollisionShape* to btCompoundShape* for direct access
    
    // Collision shape optimization: Track mapping from subcube positions to collision shape indices
    std::unordered_map<glm::ivec3, int, ivec3_hash> subcubeToCollisionIndex;

public:
    // Constructor
    explicit Chunk(const glm::ivec3& origin = glm::ivec3(0));
    
    // Destructor
    ~Chunk();
    
    // Copy constructor and assignment operator (deleted - chunks should not be copied)
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    
    // Move constructor and assignment operator
    Chunk(Chunk&& other) noexcept;
    Chunk& operator=(Chunk&& other) noexcept;
    
    // Initialization
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    
    // Basic properties
    glm::ivec3 getWorldOrigin() const { return worldOrigin; }
    size_t getCubeCount() const { return cubes.size(); }
    size_t getStaticSubcubeCount() const { return staticSubcubes.size(); }
    size_t getTotalSubcubeCount() const { return staticSubcubes.size(); }     // Only static subcubes in chunks
    uint32_t getNumInstances() const { return numInstances; }
    bool getNeedsUpdate() const { return needsUpdate; }
    void setNeedsUpdate(bool needsUpdate) { this->needsUpdate = needsUpdate; }
    
    // Buffer capacity analysis
    size_t getBufferCapacity() const { return bufferCapacity; }
    
    // Cube access
    Cube* getCubeAt(const glm::ivec3& localPos);
    const Cube* getCubeAt(const glm::ivec3& localPos) const;
    Cube* getCubeAtIndex(size_t index);
    const Cube* getCubeAtIndex(size_t index) const;
    
    // Subcube access
    Subcube* getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    const Subcube* getSubcubeAt(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    std::vector<Subcube*> getSubcubesAt(const glm::ivec3& localPos);
    
    // Physics-related subcube access (for global transfer system)
    const std::vector<Subcube*>& getStaticSubcubes() const { return staticSubcubes; }
    
    // Cube manipulation
    bool setCubeColor(const glm::ivec3& localPos, const glm::vec3& color);
    bool removeCube(const glm::ivec3& localPos);
    bool addCube(const glm::ivec3& localPos, const glm::vec3& color);
    
    // Subcube manipulation
    bool subdivideAt(const glm::ivec3& localPos);              // Convert cube to 27 static subcubes
    bool addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos, const glm::vec3& color);
    bool removeSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    bool clearSubdivisionAt(const glm::ivec3& localPos);       // Remove all subcubes and restore cube
    
    // Physics-related subcube manipulation
    bool breakSubcube(const glm::ivec3& parentPos, const glm::ivec3& subcubePos,  // Move subcube from static to dynamic (direct to global)
                     class Physics::PhysicsWorld* physicsWorld = nullptr, 
                     const glm::vec3& impulseForce = glm::vec3(0.0f),
                     class ChunkManager* chunkManager = nullptr,               // For direct global transfer (avoids staging)
                     std::function<void(std::unique_ptr<Subcube>)> transferCallback = nullptr); // REFACTOR: Direct transfer callback
    
    // Chunk operations
    void populateWithCubes();                      // Fill chunk with 32x32x32 cubes
    void rebuildFaces();                           // Regenerate face data from cubes
    void updateVulkanBuffer();                     // Update GPU buffer with face data
    
    // Efficient partial updates for hover effects (avoids full rebuild)
    void updateSingleCubeColor(const glm::ivec3& localPos, const glm::vec3& newColor);
    void updateSingleSubcubeColor(const glm::ivec3& parentLocalPos, const glm::ivec3& subcubePos, const glm::vec3& newColor);
    
    // Vulkan buffer management
    void createVulkanBuffer();
    void cleanupVulkanResources();
    void ensureBufferCapacity(size_t requiredInstances);  // Handle buffer reallocation if needed
    
    // Physics management
    void setPhysicsWorld(class Physics::PhysicsWorld* world) { physicsWorld = world; }
    void createChunkPhysicsBody();                    // Create compound shape physics body for static geometry
    void updateChunkPhysicsBody();                    // Rebuild physics body when static geometry changes
    void forcePhysicsRebuild();                       // Force immediate compound shape rebuild (bypasses performance optimization)
    void cleanupPhysicsResources();                   // Clean up physics bodies
    class btRigidBody* getChunkPhysicsBody() const { return chunkPhysicsBody; }
    
    // Optimized collision shape management
    void removeCollisionShapeByIndex(int collisionIndex);    // Efficiently remove single collision shape
    void updateCollisionIndexMapping(int removedIndex);      // Update mappings after removal
    void rebuildCollisionIndexMapping();                     // Rebuild entire mapping (for batch operations)
    
    // Utility functions
    static size_t localToIndex(const glm::ivec3& localPos);
    static glm::ivec3 indexToLocal(size_t index);
    static size_t subcubeToIndex(const glm::ivec3& parentPos, const glm::ivec3& subcubePos);
    
    // Access for ChunkManager (friend access or public as needed)
    VkBuffer getInstanceBuffer() const { return instanceBuffer; }
    const std::vector<InstanceData>& getFaces() const { return faces; }
    void* getMappedMemory() const { return mappedMemory; }

private:
    // Collision box structure for physics optimization
    struct CollisionBox {
        glm::vec3 center;
        glm::vec3 halfExtents;
        
        CollisionBox(const glm::vec3& center, const glm::vec3& halfExtents) 
            : center(center), halfExtents(halfExtents) {}
    };
    
    std::vector<CollisionBox> generateMergedCollisionBoxes();  // Generate optimized collision boxes for compound shape
    
private:
    // Physics world reference for physics body creation
    class Physics::PhysicsWorld* physicsWorld = nullptr;
    
    // Helper functions
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool isValidLocalPosition(const glm::ivec3& localPos) const;
};

} // namespace VulkanCube
