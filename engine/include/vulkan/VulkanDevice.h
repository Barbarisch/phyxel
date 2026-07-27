#pragma once

#include "core/Types.h"
#include "graphics/Light.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <cstdlib>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Vulkan {

// Vertex structure for cube rendering
struct Vertex {
    uint32_t vertexID;
    
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }
    
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(1);
        
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32_UINT;
        attributeDescriptions[0].offset = offsetof(Vertex, vertexID);
        
        return attributeDescriptions;
    }
};

// Instance data structure - compressed format with texture support.
// MUST stay layout-identical to Phyxel::InstanceData (core/Types.h): the CPU writes that
// struct into the instance buffer and this defines how the pipeline reads it.
struct InstanceData {
    uint32_t packedData;      // 15 bits position (5+5+5), 6 bits face mask, 11 bits available for future features
    uint16_t textureIndex;    // Texture atlas index (0-65535)
    uint16_t reserved;        // Flags: bit0 emissive, bit1 transparent, bits2-9 alpha, bit10 mirror, bits11-14 damage
    uint32_t light;           // Smooth lighting: bits0-15 = 4 per-corner skylight nibbles (corner = vertexID&3)
    uint32_t light2;          // Per-corner block light: corner0 RGB (bits0-11) | corner1 RGB (bits12-23)
    uint32_t light3;          // Per-corner block light: corner2 RGB (bits0-11) | corner3 RGB (bits12-23)
    uint32_t tint;            // Per-voxel 0xRRGGBB tint multiplier (0xFFFFFF = none). MUST match Phyxel::InstanceData.

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 1;
        bindingDescription.stride = sizeof(InstanceData);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return bindingDescription;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(7);

        attributeDescriptions[0].binding = 1;
        attributeDescriptions[0].location = 1;
        attributeDescriptions[0].format = VK_FORMAT_R32_UINT;  // uint32 packed data
        attributeDescriptions[0].offset = offsetof(InstanceData, packedData);

        attributeDescriptions[1].binding = 1;
        attributeDescriptions[1].location = 2;
        attributeDescriptions[1].format = VK_FORMAT_R16_UINT;  // uint16 texture index
        attributeDescriptions[1].offset = offsetof(InstanceData, textureIndex);

        attributeDescriptions[2].binding = 1;
        attributeDescriptions[2].location = 3;
        attributeDescriptions[2].format = VK_FORMAT_R16_UINT;  // uint16 reserved (flags)
        attributeDescriptions[2].offset = offsetof(InstanceData, reserved);

        attributeDescriptions[3].binding = 1;
        attributeDescriptions[3].location = 4;
        attributeDescriptions[3].format = VK_FORMAT_R32_UINT;  // uint32 baked light (corner skies)
        attributeDescriptions[3].offset = offsetof(InstanceData, light);

        attributeDescriptions[4].binding = 1;
        attributeDescriptions[4].location = 5;
        attributeDescriptions[4].format = VK_FORMAT_R32_UINT;  // uint32 per-corner block light (corners 0,1)
        attributeDescriptions[4].offset = offsetof(InstanceData, light2);

        attributeDescriptions[5].binding = 1;
        attributeDescriptions[5].location = 6;
        attributeDescriptions[5].format = VK_FORMAT_R32_UINT;  // uint32 per-corner block light (corners 2,3)
        attributeDescriptions[5].offset = offsetof(InstanceData, light3);

        attributeDescriptions[6].binding = 1;
        attributeDescriptions[6].location = 7;
        attributeDescriptions[6].format = VK_FORMAT_R32_UINT;  // uint32 per-voxel tint (0xRRGGBB)
        attributeDescriptions[6].offset = offsetof(InstanceData, tint);

        return attributeDescriptions;
    }
};

// Uniform buffer object
struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightSpaceMatrix; // For shadow mapping
    alignas(16) glm::vec3 sunDirection; // Direction of the sun
    alignas(16) glm::vec3 sunColor;     // Color of the sun
    alignas(4) uint32_t numInstances;
    alignas(4) float ambientLight; // Ambient light strength
    alignas(4) float emissiveMultiplier; // Brightness of emissive objects
    alignas(16) glm::vec3 cameraPosition; // World-space camera position for specular
    alignas(16) glm::mat4 reflectedViewProj; // Reflected camera VP matrix (identity if no mirrors)
    alignas(4) float elapsedTime; // Seconds since engine start — drives grass wind + growth. Trailing field: safe to append.
    // Precombined per-frame products so vertex shaders do one mat4*vec4 instead of a
    // per-vertex mat4*mat4 (docs/ShaderMathRedundancyPlan.md Increment 1). GLSL UBO
    // blocks must declare fields in this exact order for std140 offsets to match.
    alignas(16) glm::mat4 viewProj;         // proj * view
    alignas(16) glm::mat4 biasedLightSpace; // clip->UV bias * lightSpaceMatrix (shadow sampling)
    // Camera-relative rendering (docs/CameraRelativeRendering.md): with the eye-at-origin
    // view, cameraPosition above becomes (0,0,0) and every position reaching the GPU is
    // world - camera. cameraWorld carries the TRUE world-space camera position for shaders
    // that must reconstruct ABSOLUTE coordinates for stable hashing (varied tile rotation,
    // grass/foliage wind seeds): abs = rel + cameraWorld — the ~4 mm float error at 60 km is
    // harmless to floor()-to-voxel hashes. Appended LAST: GLSL blocks are std140 prefixes,
    // so existing truncated declarations stay valid.
    alignas(16) glm::vec3 cameraWorld;
    // Grass interaction displacers (docs/VegetationWindPlan.md Phase 4 v1) — characters near the
    // camera that bend grass aside. xyz = CAMERA-RELATIVE position (world - camera, matching every
    // other GPU position), w = push radius. Only grass.vert declares these (std140 prefix rule keeps
    // every other shader's truncated block valid). Patched AFTER updateUniformBuffer each frame via
    // setGrassDisplacers (updateUniformBuffer zero-fills them — count 0 = feature inert).
    alignas(16) glm::vec4 grassDisplacers[16];
    alignas(16) glm::vec4 grassDisplacersAux[16];  // x = strength envelope 0..1 (eased on CPU), yzw reserved
    alignas(16) glm::ivec4 grassDisplacerMeta;  // x = active count, yzw unused
};

class VulkanDevice {
public:
    VulkanDevice();
    ~VulkanDevice();

    // Initialization
    bool initialize();
    void cleanup();

    // Instance management
    bool createInstance();
    bool setupDebugMessenger();
    bool createSurface(void* window);

    // Physical device selection
    bool pickPhysicalDevice();
    bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    // Logical device creation
    bool createLogicalDevice();

    // Swapchain management
    bool createSwapChain(int windowWidth, int windowHeight);
    bool createFramebuffers(VkRenderPass renderPass);
    bool createCommandBuffers();
    bool createSyncObjects();
    
    // Rendering resources
    bool createVertexBuffer();
    bool createIndexBuffer();
    bool createInstanceBuffer();
    bool createUniformBuffers();
    bool createLightBuffers();
    void updateLightBuffer(uint32_t frameIndex, const Graphics::LightBufferGPU& lightData);
    void cleanupLightBuffers();
    bool createAtlasUVBuffers();
    void updateAtlasUVBuffer(const std::vector<glm::vec4>& uvs, uint32_t fallbackIndex,
                             uint32_t count512, uint32_t count1024);
    void cleanupAtlasUVBuffers();
    bool createDescriptorSetLayout();
    bool createDescriptorPool();
    bool createDescriptorSets();
    void updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj, const glm::mat4& lightSpaceMatrix, const glm::vec3& sunDirection, const glm::vec3& sunColor, uint32_t numInstances, float ambientLight = 1.0f, float emissiveMultiplier = 2.0f, const glm::vec3& cameraPosition = glm::vec3(0.0f), float elapsedTime = 0.0f);
    void setReflectedViewProj(uint32_t frameIndex, const glm::mat4& reflectedVP);
    /// Patch the grass displacer arrays in this frame's UBO (call AFTER updateUniformBuffer,
    /// which zero-fills them). displacers: xyz = camera-relative position, w = push radius;
    /// aux: x = strength envelope 0..1 (CPU-eased attack/release).
    void setGrassDisplacers(uint32_t frameIndex, const glm::vec4* displacers,
                            const glm::vec4* aux, int count);

    // Reflection UBO buffers (separate per-frame UBOs for rendering from reflected camera)
    bool createReflectionBuffers();
    void updateReflectionUniformBuffer(uint32_t frameIndex, const glm::mat4& reflectedView, const glm::mat4& proj, const glm::mat4& lightSpaceMatrix, const glm::vec3& sunDirection, const glm::vec3& sunColor, uint32_t numInstances, float ambientLight, float emissiveMultiplier, const glm::vec3& cameraPosition);
    void bindReflectionDescriptorSets(uint32_t frameIndex, VkPipelineLayout layout);
    void cleanupReflectionBuffers();
    void updateInstanceBuffer(const std::vector<InstanceData>& instances);
    
    // Dynamic subcube buffer management
    bool createDynamicSubcubeBuffer(uint32_t maxDynamicSubcubes);
    void updateDynamicSubcubeBuffer(const std::vector<DynamicSubcubeInstanceData>& dynamicSubcubes);
    void bindDynamicSubcubeBuffer(uint32_t frameIndex);
    void cleanupDynamicSubcubeBuffer();
    uint32_t getMaxDynamicSubcubes() const { return maxDynamicSubcubes; }

    // Character instance buffer management
    bool createCharacterInstanceBuffer(uint32_t maxInstances);
    void updateCharacterInstanceBuffer(const std::vector<CharacterInstanceData>& instances);
    void bindCharacterInstanceBuffer(VkCommandBuffer commandBuffer);
    void cleanupCharacterInstanceBuffer();
    /// Capacity of the shared character instance buffer. Callers MUST clamp their
    /// batches to this — a vkCmdDraw with firstInstance past it reads stale memory
    /// and the character silently vanishes (see RenderCoordinator::batchParts).
    uint32_t getMaxCharacterInstances() const { return maxCharacterInstances; }
    
    // Buffer creation helpers
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    // Compute infrastructure
    bool initComputeResources();
    void cleanupComputeResources();
    // Device-local storage buffer (SSBO). extraUsage: e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    void createStorageBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory, VkBufferUsageFlags extraUsage = 0);
    // Host-coherent staging buffer, persistently mapped. Call vkUnmapMemory before destroying.
    void createPersistentStagingBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory, void** mappedPtr);

    VkQueue          getComputeQueue()  const { return computeQueue; }
    VkCommandPool    getComputeCommandPool() const { return computeCommandPool; }
    VkCommandBuffer  getComputeCommandBuffer(uint32_t frameIndex) const;
    uint32_t         getComputeQueueFamily() const { return computeQueueFamily; }
    bool             computeSharesGraphicsQueue() const { return computeQueueFamily == graphicsQueueFamily; }
    
    // Depth buffer management
        bool createDepthResources();
        VkFormat findDepthFormat();
        VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
        void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, 
                        VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);    
        
    // Texture atlas management
    bool loadTextureAtlas(const std::string& atlasPath);
    bool uploadTextureAtlasPixels(const uint8_t* pixels, int width, int height);
    // Upload a layer-major RGBA texture array (one texSize² layer per textureIndex) into the
    // voxel texture image for resolution class `target` (0 = 512 array @ binding 1, 1 = 1024
    // array @ binding 5) as a 2D array with a full mip chain. size = texSize*texSize*4*layers.
    bool uploadTextureArray(int target, const uint8_t* pixels, int texSize, int layerCount);

    /// Whether the device supports BC texture compression (BC7). If false, callers must use
    /// the uncompressed uploadTextureArray() path.
    bool bc7Supported() const { return bc7Supported_; }

    /// Whether the pipelineStatisticsQuery feature was enabled (for the D0 overdraw counter).
    bool pipelineStatsSupported() const { return pipelineStatsSupported_; }

    /// Upload a pre-compressed BC7 texture array for resolution class `target` (0/1). `data`
    /// holds all mip levels tightly packed in level-major / layer-minor order (level 0 first;
    /// within a level, layer 0..N-1, each layer = ceil(w/4)*ceil(h/4)*16 bytes).
    /// `levelByteOffsets` has mipLevels entries (start of each level in `data`).
    bool uploadTextureArrayBC7(int target, const uint8_t* data, size_t dataSize,
                               const std::vector<size_t>& levelByteOffsets,
                               int baseSize, int layerCount, int mipLevels);
    bool createTextureAtlasSampler();
    void updateDescriptorSetsWithTexture();
    void cleanupTextureAtlas();

    /// Load a PNG from disk and register it as an ImGui texture.
    /// Returns an ImTextureID (cast from VkDescriptorSet), or nullptr on failure.
    /// Textures are cached by path; repeated calls return the same handle.
    /// All ImGui textures are freed in cleanupImGuiTextures().
    void* loadImGuiTexture(const std::string& path);

    /// Free all textures loaded via loadImGuiTexture.
    void cleanupImGuiTextures();    
    
    // Shadow map resources
    void setShadowMapResources(VkImageView imageView, VkSampler sampler) {
        shadowMapImageView = imageView;
        shadowMapSampler = sampler;
    }
        
        // Command buffer operations
        void waitForFence(uint32_t frameIndex);
        void resetFence(uint32_t frameIndex);
        VkResult acquireNextImage(uint32_t frameIndex, uint32_t* imageIndex);
        void resetCommandBuffer(uint32_t frameIndex);
        void beginCommandBuffer(uint32_t frameIndex);
        void beginRenderPass(uint32_t frameIndex, uint32_t imageIndex, VkRenderPass renderPass);
        void endRenderPass(uint32_t frameIndex);
        void endCommandBuffer(uint32_t frameIndex);
        bool submitCommandBuffer(uint32_t frameIndex);
        VkResult presentFrame(uint32_t imageIndex, uint32_t frameIndex);
        VkCommandBuffer getCommandBuffer(uint32_t frameIndex);
        
        // Rendering command recording
        void bindVertexBuffers(uint32_t frameIndex);
        void bindIndexBuffer(uint32_t frameIndex);
        void bindDescriptorSets(uint32_t frameIndex, VkPipelineLayout pipelineLayout);
        void drawIndexed(uint32_t frameIndex, uint32_t indexCount, uint32_t instanceCount,
                         uint32_t firstInstance = 0);

        // D1 (docs/RenderDensityPlan.md): each chunk face-instance is drawn with the cube's 36
        // indices, but the shader collapses them onto the instance's single face quad → ~12
        // triangles/face (6× the 2 a quad needs). s_quadDraw ON draws only the first 6 indices
        // (the +Z front quad {4,6,5,6,7,5}), which the shader repositions per faceID → one quad/face.
        // MAIN OPAQUE PASS ONLY (back-culled): pixel-identical there. The SHADOW pass FRONT-culls
        // closed casters (ShadowMap.cpp:429) and needs BOTH windings → it stays 36-index; likewise
        // reflection/OIT/mirror keep 36 (mirrored/transparent winding unverified). See renderShadowPass.
        static bool s_quadDraw;
        static uint32_t chunkIndexCount() { return s_quadDraw ? 6u : 36u; }

        // Push constants and chunk rendering. chunkBaseOffset is CAMERA-RELATIVE (positions);
        // chunkBaseAbs is the EXACT absolute chunk origin (varied-hash seed) — both required
        // so tile rotations stay world-stable while geometry stays precision-safe.
        void pushConstants(uint32_t frameIndex, VkPipelineLayout pipelineLayout, const glm::vec3& chunkBaseOffset, const glm::vec3& chunkBaseAbs);
        void pushConstants(uint32_t frameIndex, VkPipelineLayout pipelineLayout, const glm::vec3& chunkBaseOffset, uint32_t debugMode, const glm::vec3& chunkBaseAbs);
        void bindInstanceBufferWithOffset(uint32_t frameIndex, VkDeviceSize offset);
        void drawChunk(uint32_t frameIndex, VkPipelineLayout pipelineLayout, 
                      const glm::vec3& chunkBaseOffset, VkDeviceSize instanceOffset, uint32_t instanceCount);
        
        // Convenience function for multi-chunk rendering
        struct ChunkRenderData {
            glm::vec3 worldPosition;     // World position of chunk (e.g., chunkX*32, chunkY*32, chunkZ*32)
            VkDeviceSize instanceOffset; // Offset in instance buffer for this chunk's data
            uint32_t instanceCount;      // Number of visible cubes in this chunk
        };
        void drawChunks(uint32_t frameIndex, VkPipelineLayout pipelineLayout, 
                       const std::vector<ChunkRenderData>& chunks);
        
        // Static helper for creating push constant range (use in pipeline layout creation)
        static VkPushConstantRange getPushConstantRange() {
            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            range.offset = 0;
            // vec3 chunkBaseOffset (camera-relative) + uint debugMode + vec3 chunkBaseAbs
            // (exact absolute chunk origin — seeds the `varied` tile-rotation hash, which
            // must not re-roll with camera motion; docs/CameraRelativeRendering.md).
            range.size = 32;
            return range;
        }

        // Getters
        VkDevice getDevice() const { return device; }
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
        VkInstance getInstance() const { return instance; }
        VkSurfaceKHR getSurface() const { return surface; }
        VkQueue getGraphicsQueue() const { return graphicsQueue; }
        VkQueue getPresentQueue() const { return presentQueue; }
        VkExtent2D getSwapChainExtent() const { return swapChainExtent; }
        VkFormat getSwapChainImageFormat() const { return swapChainImageFormat; }
        VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }
        // Set-0 descriptor whose UBO holds the reflected camera (used by the mirror reflection pass).
        VkDescriptorSet getReflectionDescriptorSet(uint32_t frameIndex) const { return reflectionDescriptorSets[frameIndex]; }
        uint32_t getSwapChainImageCount() const { return static_cast<uint32_t>(swapChainImages.size()); }
        VkImage getSwapChainImage(uint32_t index) const { return swapChainImages[index]; }
        VkFramebuffer getSwapChainFramebuffer(uint32_t index) const { return swapChainFramebuffers[index]; }
        uint32_t getGraphicsQueueFamily() const;
        VkCommandBuffer getCommandBuffer(uint32_t frameIndex) const;
        
        // Command buffer utilities
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        
        // Synchronization / device idle
        void deviceWaitIdle();
        void recreateSyncObjects();

        // Window resize handling
        void setFramebufferResized(bool resized) { framebufferResized = resized; }
        bool getFramebufferResized() const { return framebufferResized; }
        bool recreateSwapChain(int windowWidth, int windowHeight, VkRenderPass renderPass);
        void cleanupSwapChain();

private:
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;

    // Compute resources
    uint32_t         computeQueueFamily = 0;
    uint32_t         graphicsQueueFamily = 0;
    VkCommandPool    computeCommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> computeCommandBuffers;

    // Swapchain
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    // Depth buffer
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    // Rendering resources
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceBufferMemory = VK_NULL_HANDLE;
    
    // Dynamic subcube buffer
    VkBuffer dynamicSubcubeBuffer = VK_NULL_HANDLE;
    VkDeviceMemory dynamicSubcubeBufferMemory = VK_NULL_HANDLE;
    uint32_t maxDynamicSubcubes = 0;

    // Character instance buffer
    VkBuffer characterInstanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory characterInstanceBufferMemory = VK_NULL_HANDLE;
    void* characterInstanceMapped = nullptr;  // persistent map (HOST_COHERENT)
    uint32_t maxCharacterInstances = 0;
    
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Reflection UBO buffers (per-frame, separate from main UBOs)
    std::vector<VkBuffer> reflectionUniformBuffers;
    std::vector<VkDeviceMemory> reflectionUniformBuffersMemory;
    VkDescriptorPool reflectionDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> reflectionDescriptorSets;

    // Texture array resources — class 0 (512px, binding 1)
    VkImage textureAtlasImage = VK_NULL_HANDLE;
    VkDeviceMemory textureAtlasImageMemory = VK_NULL_HANDLE;
    VkImageView textureAtlasImageView = VK_NULL_HANDLE;
    VkSampler textureAtlasSampler = VK_NULL_HANDLE;
    // Class 1 (1024px, binding 5) — hi-res object/detail array
    VkImage textureArrayHiImage = VK_NULL_HANDLE;
    VkDeviceMemory textureArrayHiImageMemory = VK_NULL_HANDLE;
    VkImageView textureArrayHiImageView = VK_NULL_HANDLE;
    // Normal+roughness arrays (RGB=normal, A=roughness; UNORM): binding 6 (512), binding 7 (1024)
    VkImage textureNormal512Image = VK_NULL_HANDLE;
    VkDeviceMemory textureNormal512ImageMemory = VK_NULL_HANDLE;
    VkImageView textureNormal512ImageView = VK_NULL_HANDLE;
    VkImage textureNormal1024Image = VK_NULL_HANDLE;
    VkDeviceMemory textureNormal1024ImageMemory = VK_NULL_HANDLE;
    VkImageView textureNormal1024ImageView = VK_NULL_HANDLE;
    bool bc7Supported_ = false;  // set during logical-device creation
    bool pipelineStatsSupported_ = false;  // pipelineStatisticsQuery feature (D0 overdraw counter)

    // ImGui texture cache (for menu images, logos, etc.)
    struct ImGuiTextureEntry {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    std::unordered_map<std::string, ImGuiTextureEntry> imguiTextureCache_;

    // Shadow map resources
    VkImageView shadowMapImageView = VK_NULL_HANDLE;
    VkSampler shadowMapSampler = VK_NULL_HANDLE;

    // Light SSBO resources
    std::vector<VkBuffer> lightBuffers;
    std::vector<VkDeviceMemory> lightBuffersMemory;

    // Atlas UV SSBO resources
    std::vector<VkBuffer> atlasUVBuffers;
    std::vector<VkDeviceMemory> atlasUVBuffersMemory;

    // Command buffers
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    
    // Window resize handling
    bool framebufferResized = false;

    // Helper methods for swapchain
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkPresentModeKHR preferredPresentMode_ = VK_PRESENT_MODE_IMMEDIATE_KHR;
public:
    void setPreferredPresentMode(VkPresentModeKHR mode) { preferredPresentMode_ = mode; }
    VkPresentModeKHR getPreferredPresentMode() const { return preferredPresentMode_; }
private:
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, int windowWidth, int windowHeight);

    // Synchronization
    static const int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    // Helper functions
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    // Validation layers and extensions
    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

#ifdef NDEBUG
    // Release: validation layers compiled out entirely (zero overhead).
    const bool enableValidationLayers = false;
#else
    // Debug: validation layers are OPT-IN to keep normal debug iteration fast.
    // They add heavy per-Vulkan-call CPU overhead (can drop FPS ~3-5x). Enable
    // only when actually debugging GPU/Vulkan issues:
    //   set PHYXEL_VALIDATION=1   (PowerShell: $env:PHYXEL_VALIDATION=1)
    const bool enableValidationLayers = (std::getenv("PHYXEL_VALIDATION") != nullptr);
#endif

    // Cleanup state tracking
    bool m_cleanedUp = false;
};

} // namespace Vulkan
} // namespace Phyxel
