#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace Phyxel {
    namespace Vulkan { class VulkanDevice; }
namespace UI {

/// Per-vertex data for UI quads.
struct UIVertex {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;
};

/// Push constants for the UI shader — maps screen coords to NDC.
struct UIPushConstants {
    glm::vec2 scale;     // 2.0/screenWidth, 2.0/screenHeight
    glm::vec2 translate; // -1.0, -1.0
};

/**
 * @brief Low-level Vulkan 2D quad renderer for custom UI.
 *
 * Batches textured/colored quads each frame and renders them
 * in a single draw call inside the post-process render pass.
 * Uses an R8 font atlas texture for text, and solid-white UV
 * region for colored rectangles.
 */
class UIRenderer {
public:
    UIRenderer(Vulkan::VulkanDevice* device, uint32_t width, uint32_t height);
    ~UIRenderer();

    /// Create Vulkan resources. Must be called after VulkanDevice is ready.
    /// renderPass should be the post-process render pass (swapchain target).
    bool initialize(VkRenderPass renderPass);

    /// Release all Vulkan resources.
    void cleanup();

    /// Recreate after swapchain resize.
    void resize(uint32_t width, uint32_t height);

    // ── Font atlas ──────────────────────────────────────────────

    /// Upload font atlas as R8 texture. Data is width*height bytes (grayscale).
    bool uploadFontAtlas(const uint8_t* pixels, uint32_t atlasWidth, uint32_t atlasHeight);

    // ── Batching API (call between beginFrame/endFrame) ─────────

    /// Start a new frame's UI batch — clears the vertex/index buffers.
    void beginFrame();

    /// Submit a colored, textured quad.
    void drawQuad(glm::vec2 pos, glm::vec2 size, glm::vec2 uvMin, glm::vec2 uvMax, glm::vec4 color);

    /// Submit a solid-color rectangle (uses a white pixel in the atlas).
    void drawRect(glm::vec2 pos, glm::vec2 size, glm::vec4 color);

    /// Flush the batch: upload vertices and record draw commands.
    /// Must be called inside an active render pass.
    void endFrame(VkCommandBuffer cmd);

    // ── Accessors ───────────────────────────────────────────────

    uint32_t getScreenWidth() const { return screenWidth_; }
    uint32_t getScreenHeight() const { return screenHeight_; }

private:
    bool createPipeline(VkRenderPass renderPass);
    bool createDescriptorResources();
    bool createVertexBuffer();
    void updateDescriptorSet();

    Vulkan::VulkanDevice* device_;
    uint32_t screenWidth_;
    uint32_t screenHeight_;

    // Vulkan resources
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    // Font atlas texture
    VkImage fontImage_ = VK_NULL_HANDLE;
    VkDeviceMemory fontImageMemory_ = VK_NULL_HANDLE;
    VkImageView fontImageView_ = VK_NULL_HANDLE;
    VkSampler fontSampler_ = VK_NULL_HANDLE;

    // Dynamic vertex buffer (re-uploaded each frame)
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory_ = VK_NULL_HANDLE;
    static constexpr size_t MAX_QUADS = 8192;
    static constexpr size_t MAX_VERTICES = MAX_QUADS * 4;
    static constexpr size_t MAX_INDICES = MAX_QUADS * 6;

    // CPU-side batch
    std::vector<UIVertex> vertices_;
    std::vector<uint32_t> indices_;

    bool initialized_ = false;
};

} // namespace UI
} // namespace Phyxel
