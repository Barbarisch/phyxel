#pragma once

#include "core/Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <algorithm>
#include <glm/glm.hpp>

namespace VulkanCube {
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

// Instance data structure - compressed format with texture support
struct InstanceData {
    uint32_t packedData;      // 15 bits position (5+5+5), 6 bits face mask, 11 bits available for future features
    uint16_t textureIndex;    // Texture atlas index (0-65535)
    uint16_t reserved;        // Reserved for future use (ensures 8-byte alignment)
    
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 1;
        bindingDescription.stride = sizeof(InstanceData);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return bindingDescription;
    }
    
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(2);
        
        attributeDescriptions[0].binding = 1;
        attributeDescriptions[0].location = 1;
        attributeDescriptions[0].format = VK_FORMAT_R32_UINT;  // uint32 packed data
        attributeDescriptions[0].offset = offsetof(InstanceData, packedData);
        
        attributeDescriptions[1].binding = 1;
        attributeDescriptions[1].location = 2;
        attributeDescriptions[1].format = VK_FORMAT_R16_UINT;  // uint16 texture index
        attributeDescriptions[1].offset = offsetof(InstanceData, textureIndex);
        
        return attributeDescriptions;
    }
};

// Uniform buffer object
struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightSpaceMatrix; // For shadow mapping
    alignas(4) uint32_t numInstances;
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
    bool createDescriptorSetLayout();
    bool createDescriptorPool();
    bool createDescriptorSets();
    void updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj, const glm::mat4& lightSpaceMatrix, uint32_t numInstances);
    void updateInstanceBuffer(const std::vector<InstanceData>& instances);
    
    // Dynamic subcube buffer management
    bool createDynamicSubcubeBuffer(uint32_t maxDynamicSubcubes);
    void updateDynamicSubcubeBuffer(const std::vector<DynamicSubcubeInstanceData>& dynamicSubcubes);
    void bindDynamicSubcubeBuffer(uint32_t frameIndex);
    void cleanupDynamicSubcubeBuffer();
    
    // Buffer creation helpers
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    
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
    bool createTextureAtlasSampler();
    void updateDescriptorSetsWithTexture();
    void cleanupTextureAtlas();    
    
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
        void drawIndexed(uint32_t frameIndex, uint32_t indexCount, uint32_t instanceCount);
        
        // Push constants and chunk rendering
        void pushConstants(uint32_t frameIndex, VkPipelineLayout pipelineLayout, const glm::vec3& chunkBaseOffset);
        void pushConstants(uint32_t frameIndex, VkPipelineLayout pipelineLayout, const glm::vec3& chunkBaseOffset, uint32_t debugMode);
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
            range.size = 16;  // 12 bytes for vec3 chunkBaseOffset + 4 bytes for uint debugMode
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
        uint32_t getSwapChainImageCount() const { return static_cast<uint32_t>(swapChainImages.size()); }
        uint32_t getGraphicsQueueFamily() const;
        VkCommandBuffer getCommandBuffer(uint32_t frameIndex) const;
        
        // Command buffer utilities
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        
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
    
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Texture atlas resources
    VkImage textureAtlasImage = VK_NULL_HANDLE;
    VkDeviceMemory textureAtlasImageMemory = VK_NULL_HANDLE;
    VkImageView textureAtlasImageView = VK_NULL_HANDLE;
    VkSampler textureAtlasSampler = VK_NULL_HANDLE;

    // Shadow map resources
    VkImageView shadowMapImageView = VK_NULL_HANDLE;
    VkSampler shadowMapSampler = VK_NULL_HANDLE;

    // Command buffers
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    
    // Window resize handling
    bool framebufferResized = false;

    // Helper methods for swapchain
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
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
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

    // Cleanup state tracking
    bool m_cleanedUp = false;
};

} // namespace Vulkan
} // namespace VulkanCube
