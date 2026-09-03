#include "ui/UIRenderer.h"
#include "vulkan/VulkanDevice.h"
#include "core/AssetManager.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"
#include "stb_image.h"   // implementation lives in VulkanDevice.cpp
#include <cstring>
#include <array>
#include <algorithm>   // std::max — viewport rect clamping

namespace Phyxel {
namespace UI {

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

UIRenderer::UIRenderer(Vulkan::VulkanDevice* device, uint32_t width, uint32_t height)
    : device_(device), screenWidth_(width), screenHeight_(height) {
    vertices_.reserve(MAX_VERTICES);
    indices_.reserve(MAX_INDICES);
}

UIRenderer::~UIRenderer() {
    cleanup();
}

bool UIRenderer::initialize(VkRenderPass renderPass) {
    if (!createDescriptorResources()) {
        LOG_ERROR("UIRenderer", "Failed to create descriptor resources");
        return false;
    }
    if (!createPipeline(renderPass)) {
        LOG_ERROR("UIRenderer", "Failed to create pipeline");
        return false;
    }
    if (!createVertexBuffer()) {
        LOG_ERROR("UIRenderer", "Failed to create vertex buffer");
        return false;
    }
    initialized_ = true;
    LOG_INFO("UIRenderer", "Initialized ({}x{})", screenWidth_, screenHeight_);
    return true;
}

void UIRenderer::cleanup() {
    if (!device_) return;
    VkDevice dev = device_->getDevice();
    vkDeviceWaitIdle(dev);

    if (indexBuffer_)          { vkDestroyBuffer(dev, indexBuffer_, nullptr); indexBuffer_ = VK_NULL_HANDLE; }
    if (indexBufferMemory_)    { vkFreeMemory(dev, indexBufferMemory_, nullptr); indexBufferMemory_ = VK_NULL_HANDLE; }
    if (vertexBuffer_)         { vkDestroyBuffer(dev, vertexBuffer_, nullptr); vertexBuffer_ = VK_NULL_HANDLE; }
    if (vertexBufferMemory_)   { vkFreeMemory(dev, vertexBufferMemory_, nullptr); vertexBufferMemory_ = VK_NULL_HANDLE; }
    if (fontSampler_)          { vkDestroySampler(dev, fontSampler_, nullptr); fontSampler_ = VK_NULL_HANDLE; }
    if (fontImageView_)        { vkDestroyImageView(dev, fontImageView_, nullptr); fontImageView_ = VK_NULL_HANDLE; }
    if (fontImage_)            { vkDestroyImage(dev, fontImage_, nullptr); fontImage_ = VK_NULL_HANDLE; }
    if (fontImageMemory_)      { vkFreeMemory(dev, fontImageMemory_, nullptr); fontImageMemory_ = VK_NULL_HANDLE; }

    // Image textures (descriptor sets are freed with the pool below).
    for (auto& t : textures_) {
        if (t.sampler) vkDestroySampler(dev, t.sampler, nullptr);
        if (t.view)    vkDestroyImageView(dev, t.view, nullptr);
        if (t.image)   vkDestroyImage(dev, t.image, nullptr);
        if (t.memory)  vkFreeMemory(dev, t.memory, nullptr);
    }
    textures_.clear();
    textureCache_.clear();
    if (pipeline_)             { vkDestroyPipeline(dev, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_)       { vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if (descriptorPool_)       { vkDestroyDescriptorPool(dev, descriptorPool_, nullptr); descriptorPool_ = VK_NULL_HANDLE; }
    if (descriptorSetLayout_)  { vkDestroyDescriptorSetLayout(dev, descriptorSetLayout_, nullptr); descriptorSetLayout_ = VK_NULL_HANDLE; }
    initialized_ = false;
}

void UIRenderer::resize(uint32_t width, uint32_t height) {
    screenWidth_ = width;
    screenHeight_ = height;
}

// ════════════════════════════════════════════════════════════════
// Font Atlas Upload
// ════════════════════════════════════════════════════════════════

bool UIRenderer::uploadFontAtlas(const uint8_t* pixels, uint32_t atlasWidth, uint32_t atlasHeight) {
    VkDevice dev = device_->getDevice();

    // Clean up previous atlas if any
    if (fontSampler_)    { vkDestroySampler(dev, fontSampler_, nullptr); fontSampler_ = VK_NULL_HANDLE; }
    if (fontImageView_)  { vkDestroyImageView(dev, fontImageView_, nullptr); fontImageView_ = VK_NULL_HANDLE; }
    if (fontImage_)      { vkDestroyImage(dev, fontImage_, nullptr); fontImage_ = VK_NULL_HANDLE; }
    if (fontImageMemory_){ vkFreeMemory(dev, fontImageMemory_, nullptr); fontImageMemory_ = VK_NULL_HANDLE; }

    VkDeviceSize imageSize = atlasWidth * atlasHeight;

    // Staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    device_->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(dev, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, imageSize);
    vkUnmapMemory(dev, stagingBufferMemory);

    // Create R8 image
    device_->createImage(atlasWidth, atlasHeight, VK_FORMAT_R8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, fontImage_, fontImageMemory_);

    // Transition to transfer dst
    VkCommandBuffer cmd = device_->beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = fontImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {atlasWidth, atlasHeight, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, fontImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    device_->endSingleTimeCommands(cmd);

    vkDestroyBuffer(dev, stagingBuffer, nullptr);
    vkFreeMemory(dev, stagingBufferMemory, nullptr);

    // Image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = fontImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(dev, &viewInfo, nullptr, &fontImageView_) != VK_SUCCESS) return false;

    // Sampler — nearest for pixel-art
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    if (vkCreateSampler(dev, &samplerInfo, nullptr, &fontSampler_) != VK_SUCCESS) return false;

    updateDescriptorSet();
    LOG_INFO("UIRenderer", "Font atlas uploaded ({}x{})", atlasWidth, atlasHeight);
    return true;
}

// ════════════════════════════════════════════════════════════════
// Image Textures (arbitrary RGBA PNGs for UIImage)
// ════════════════════════════════════════════════════════════════

int UIRenderer::loadTexture(const std::string& path) {
    auto cached = textureCache_.find(path);
    if (cached != textureCache_.end()) return cached->second;

    if (textures_.size() >= MAX_IMAGE_TEXTURES) {
        LOG_WARN("UIRenderer", "UI image texture limit ({}) reached; cannot load {}", MAX_IMAGE_TEXTURES, path);
        return -1;
    }

    VkDevice dev = device_->getDevice();

    // Load pixels (try as-given, then the resolved textures dir).
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!pixels) {
        std::string resolved = Core::AssetManager::instance().resolveTexture(path);
        if (!resolved.empty()) pixels = stbi_load(resolved.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    }
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        LOG_ERROR("UIRenderer", "Failed to load UI image '{}' ({})", path, reason ? reason : "unknown");
        textureCache_[path] = -1;   // cache the failure so we don't retry every frame
        return -1;
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;
    UITexture t;

    // Staging buffer
    VkBuffer staging; VkDeviceMemory stagingMem;
    device_->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem);
    void* data;
    vkMapMemory(dev, stagingMem, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(dev, stagingMem);
    stbi_image_free(pixels);

    device_->createImage(w, h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, t.image, t.memory);

    VkCommandBuffer cmd = device_->beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = t.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    device_->endSingleTimeCommands(cmd);
    vkDestroyBuffer(dev, staging, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);

    // Image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = t.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(dev, &viewInfo, nullptr, &t.view) != VK_SUCCESS) {
        LOG_ERROR("UIRenderer", "Failed to create image view for {}", path);
        return -1;
    }

    // Sampler — linear for smooth icons.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;
    if (vkCreateSampler(dev, &samplerInfo, nullptr, &t.sampler) != VK_SUCCESS) {
        LOG_ERROR("UIRenderer", "Failed to create sampler for {}", path);
        return -1;
    }

    // Descriptor set (same layout as the font set; binding 0 = combined image sampler).
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;
    if (vkAllocateDescriptorSets(dev, &allocInfo, &t.set) != VK_SUCCESS) {
        LOG_ERROR("UIRenderer", "Failed to allocate descriptor set for {}", path);
        return -1;
    }
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = t.view;
    imageInfo.sampler = t.sampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = t.set;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

    textures_.push_back(t);
    int idx = static_cast<int>(textures_.size()) - 1;
    textureCache_[path] = idx;
    LOG_INFO("UIRenderer", "Loaded UI image '{}' ({}x{}) -> texture {}", path, w, h, idx);
    return idx;
}

// ════════════════════════════════════════════════════════════════
// Batching
// ════════════════════════════════════════════════════════════════

void UIRenderer::beginFrame() {
    vertices_.clear();
    indices_.clear();
    runs_.clear();
}

void UIRenderer::pushQuad(glm::vec2 pos, glm::vec2 size, glm::vec2 uvMin, glm::vec2 uvMax,
                          glm::vec4 color, VkDescriptorSet set, float mode) {
    if (vertices_.size() + 4 > MAX_VERTICES) return;

    // Appear animation: offset slides the quad, alpha fades it. Applied BEFORE
    // the clip so entering content is clipped at its panel's edge (see pushAnim).
    if (!animStack_.empty()) {
        pos += animStack_.back().second;
        color.a *= animStack_.back().first;
        if (color.a <= 0.003f) return;   // fully faded — skip the quad
    }

    // CPU clip against the active clip rect (single choke point — every rect,
    // image, and glyph flows through here, so panels can contain their content
    // without breaking the one-draw-call batching a GPU scissor would split).
    // Quads partially inside are clamped with proportionally-adjusted UVs.
    if (!clipStack_.empty()) {
        const glm::vec4& c = clipStack_.back();   // x, y, xMax, yMax
        const float x0 = pos.x,           y0 = pos.y;
        const float x1 = pos.x + size.x,  y1 = pos.y + size.y;
        const float cx0 = std::max(x0, c.x), cy0 = std::max(y0, c.y);
        const float cx1 = std::min(x1, c.z), cy1 = std::min(y1, c.w);
        if (cx0 >= cx1 || cy0 >= cy1) return;     // fully outside
        if (cx0 != x0 || cy0 != y0 || cx1 != x1 || cy1 != y1) {
            const glm::vec2 uvSpan = uvMax - uvMin;
            const float w = size.x > 0.0f ? size.x : 1.0f;
            const float h = size.y > 0.0f ? size.y : 1.0f;
            uvMin = {uvMin.x + uvSpan.x * (cx0 - x0) / w, uvMin.y + uvSpan.y * (cy0 - y0) / h};
            uvMax = {uvMax.x - uvSpan.x * (x1 - cx1) / w, uvMax.y - uvSpan.y * (y1 - cy1) / h};
            pos  = {cx0, cy0};
            size = {cx1 - cx0, cy1 - cy0};
        }
    }

    // Start a new draw run when the texture/descriptor set or sampling mode changes.
    if (runs_.empty() || runs_.back().set != set || runs_.back().mode != mode) {
        runs_.push_back({set, mode, static_cast<uint32_t>(indices_.size()), 0});
    }

    uint32_t base = static_cast<uint32_t>(vertices_.size());

    // Top-left, top-right, bottom-right, bottom-left
    vertices_.push_back({pos,                               uvMin,              color});
    vertices_.push_back({{pos.x + size.x, pos.y},          {uvMax.x, uvMin.y}, color});
    vertices_.push_back({{pos.x + size.x, pos.y + size.y}, uvMax,              color});
    vertices_.push_back({{pos.x, pos.y + size.y},          {uvMin.x, uvMax.y}, color});

    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
    indices_.push_back(base + 0);
    runs_.back().indexCount += 6;
}

void UIRenderer::drawQuad(glm::vec2 pos, glm::vec2 size, glm::vec2 uvMin, glm::vec2 uvMax, glm::vec4 color) {
    // Text glyphs + rects sample the R8 font atlas (alpha-mask mode 0).
    pushQuad(pos, size, uvMin, uvMax, color, descriptorSet_, 0.0f);
}

void UIRenderer::drawRect(glm::vec2 pos, glm::vec2 size, glm::vec4 color) {
    // Sample the font atlas's reserved solid-white texel (set by the active font).
    // Zero-area UV => all 4 verts sample the same texel (NEAREST filter).
    drawQuad(pos, size, whitePixelUV_, whitePixelUV_, color);
}

void UIRenderer::drawImage(glm::vec2 pos, glm::vec2 size, int textureIndex, glm::vec4 tint) {
    if (textureIndex < 0 || textureIndex >= static_cast<int>(textures_.size())) {
        drawRect(pos, size, tint);  // fallback placeholder
        return;
    }
    // RGBA image sampled full-color × tint (mode 1).
    pushQuad(pos, size, {0.0f, 0.0f}, {1.0f, 1.0f}, tint, textures_[textureIndex].set, 1.0f);
}

void UIRenderer::endFrame(VkCommandBuffer cmd) {
    if (!initialized_ || vertices_.empty()) return;

    VkDevice dev = device_->getDevice();

    // Upload vertices
    {
        void* data;
        VkDeviceSize size = vertices_.size() * sizeof(UIVertex);
        vkMapMemory(dev, vertexBufferMemory_, 0, size, 0, &data);
        memcpy(data, vertices_.data(), size);
        vkUnmapMemory(dev, vertexBufferMemory_);
    }
    // Upload indices
    {
        void* data;
        VkDeviceSize size = indices_.size() * sizeof(uint32_t);
        vkMapMemory(dev, indexBufferMemory_, 0, size, 0, &data);
        memcpy(data, indices_.data(), size);
        vkUnmapMemory(dev, indexBufferMemory_);
    }

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // Set viewport/scissor. Both are DYNAMIC state, so the placement rect costs nothing here —
    // no pipeline variant, no recreation. The push constant below keeps mapping HUD-logical pixels
    // to the full NDC square; this viewport transform is what lands that square in the rect.
    const bool haveRect = (viewportW_ > 0.0f && viewportH_ > 0.0f);
    const float vpX = haveRect ? viewportX_ : 0.0f;
    const float vpY = haveRect ? viewportY_ : 0.0f;
    const float vpW = haveRect ? viewportW_ : (float)screenWidth_;
    const float vpH = haveRect ? viewportH_ : (float)screenHeight_;

    VkViewport viewport{vpX, vpY, vpW, vpH, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    // Scissor is integer and must not go negative or the validation layer rejects it.
    VkRect2D scissor{{(int32_t)std::max(0.0f, vpX), (int32_t)std::max(0.0f, vpY)},
                     {(uint32_t)std::max(0.0f, vpW), (uint32_t)std::max(0.0f, vpH)}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind vertex/index buffers
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    // One draw per run (each binds its own descriptor set + sampling mode). Runs
    // preserve submission order, so painter's-algorithm layering is unchanged.
    UIPushConstants pc;
    pc.scale = {2.0f / screenWidth_, 2.0f / screenHeight_};
    pc.translate = {-1.0f, -1.0f};

    for (const auto& run : runs_) {
        if (run.indexCount == 0 || run.set == VK_NULL_HANDLE) continue;
        pc.mode = run.mode;
        vkCmdPushConstants(cmd, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(UIPushConstants), &pc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                                0, 1, &run.set, 0, nullptr);
        vkCmdDrawIndexed(cmd, run.indexCount, 1, run.firstIndex, 0, 0);
    }
}

// ════════════════════════════════════════════════════════════════
// Vulkan Resource Creation
// ════════════════════════════════════════════════════════════════

bool UIRenderer::createDescriptorResources() {
    VkDevice dev = device_->getDevice();

    // Descriptor set layout: one combined image sampler
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorCount = 1;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) return false;

    // Descriptor pool — font set (1) + up to MAX_IMAGE_TEXTURES image sets.
    const uint32_t kMaxSets = 1 + MAX_IMAGE_TEXTURES;
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxSets};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = kMaxSets;
    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) return false;

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;
    if (vkAllocateDescriptorSets(dev, &allocInfo, &descriptorSet_) != VK_SUCCESS) return false;

    return true;
}

void UIRenderer::updateDescriptorSet() {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = fontImageView_;
    imageInfo.sampler = fontSampler_;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device_->getDevice(), 1, &write, 0, nullptr);
}

bool UIRenderer::createPipeline(VkRenderPass renderPass) {
    VkDevice dev = device_->getDevice();

    auto vertCode = Utils::readFile(Core::AssetManager::instance().resolveShader("ui.vert.spv"));
    auto fragCode = Utils::readFile(Core::AssetManager::instance().resolveShader("ui.frag.spv"));
    if (vertCode.empty() || fragCode.empty()) {
        LOG_ERROR("UIRenderer", "Failed to load UI shaders");
        return false;
    }

    VkShaderModule vertModule = createShaderModule(dev, vertCode);
    VkShaderModule fragModule = createShaderModule(dev, fragCode);
    if (!vertModule || !fragModule) {
        LOG_ERROR("UIRenderer", "Failed to create shader modules");
        if (vertModule) vkDestroyShaderModule(dev, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(dev, fragModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main"}
    };

    // Vertex input: pos(vec2) + uv(vec2) + color(vec4)
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(UIVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attrDescs{};
    attrDescs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(UIVertex, pos)};
    attrDescs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(UIVertex, uv)};
    attrDescs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, color)};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    // Alpha blending
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constant range — vertex uses scale/translate, fragment uses mode.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(UIPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(dev, vertModule, nullptr);
        vkDestroyShaderModule(dev, fragModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    vkDestroyShaderModule(dev, vertModule, nullptr);
    vkDestroyShaderModule(dev, fragModule, nullptr);
    return result == VK_SUCCESS;
}

bool UIRenderer::createVertexBuffer() {
    VkDeviceSize vertexSize = MAX_VERTICES * sizeof(UIVertex);
    VkDeviceSize indexSize = MAX_INDICES * sizeof(uint32_t);

    device_->createBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vertexBuffer_, vertexBufferMemory_);

    device_->createBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indexBuffer_, indexBufferMemory_);

    return vertexBuffer_ != VK_NULL_HANDLE && indexBuffer_ != VK_NULL_HANDLE;
}

} // namespace UI
} // namespace Phyxel
