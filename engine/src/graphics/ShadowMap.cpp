#include "graphics/ShadowMap.h"
#include "utils/FileUtils.h"
#include "core/AssetManager.h"
#include "core/Types.h"
#include "utils/Logger.h"
#include <stdexcept>
#include <array>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace Phyxel {
namespace Graphics {

// Helper to create shader module
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

ShadowMap::ShadowMap(Vulkan::VulkanDevice* device, uint32_t width, uint32_t height)
    : device(device), width(width), height(height) {
}

ShadowMap::~ShadowMap() {
    cleanup();
}

bool ShadowMap::initialize() {
    if (!createDepthResources()) return false;
    if (!createRenderPass()) return false;
    if (!createFramebuffer()) return false;
    if (!createSampler()) return false;
    if (!createPipeline()) return false;
    if (!createCharacterShadowPipeline()) return false;
    if (!createKinematicShadowPipeline()) return false;
    if (!createDynamicShadowPipeline()) return false;
    return true;
}

void ShadowMap::cleanup() {
    VkDevice vkDevice = device->getDevice();

    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (characterPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, characterPipeline, nullptr);
        characterPipeline = VK_NULL_HANDLE;
    }
    if (characterPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, characterPipelineLayout, nullptr);
        characterPipelineLayout = VK_NULL_HANDLE;
    }
    if (kinematicPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, kinematicPipeline, nullptr);
        kinematicPipeline = VK_NULL_HANDLE;
    }
    if (kinematicPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, kinematicPipelineLayout, nullptr);
        kinematicPipelineLayout = VK_NULL_HANDLE;
    }
    if (dynamicPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, dynamicPipeline, nullptr);
        dynamicPipeline = VK_NULL_HANDLE;
    }
    if (dynamicPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, dynamicPipelineLayout, nullptr);
        dynamicPipelineLayout = VK_NULL_HANDLE;
    }
    if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vkDevice, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
    }
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vkDevice, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(vkDevice, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vkDevice, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(vkDevice, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }
    if (depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }
}

bool ShadowMap::createDepthResources() {
    VkFormat depthFormat = device->findDepthFormat();

    device->createImage(width, height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);

    depthImageView = device->createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    return true;
}

bool ShadowMap::createRenderPass() {
    VkFormat depthFormat = device->findDepthFormat();

    VkAttachmentDescription attachmentDescription{};
    attachmentDescription.format = depthFormat;
    attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
    attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 0;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthReference;

    // Dependencies
    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachmentDescription;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device->getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool ShadowMap::createFramebuffer() {
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &depthImageView;
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device->getDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool ShadowMap::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    if (vkCreateSampler(device->getDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool ShadowMap::createPipeline() {
    // Load shaders
    auto vertShaderCode = Utils::readFile(Core::AssetManager::instance().resolveShader("shadow.vert.spv"));
    auto fragShaderCode = Utils::readFile(Core::AssetManager::instance().resolveShader("shadow.frag.spv"));

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        LOG_ERROR("ShadowMap", "Failed to load shadow shaders!");
        return false;
    }

    VkShaderModule vertShaderModule = createShaderModule(device->getDevice(), vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(device->getDevice(), fragShaderCode);

    if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        LOG_ERROR("ShadowMap", "Failed to create shadow shader modules!");
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // Vertex Input
    auto bindingDescription = Vulkan::Vertex::getBindingDescription();
    auto attributeDescriptions = Vulkan::Vertex::getAttributeDescriptions();
    auto instanceBindingDescription = Vulkan::InstanceData::getBindingDescription();
    auto instanceAttributeDescriptions = Vulkan::InstanceData::getAttributeDescriptions();

    std::vector<VkVertexInputBindingDescription> bindings = { bindingDescription, instanceBindingDescription };
    std::vector<VkVertexInputAttributeDescription> attributes = attributeDescriptions;
    attributes.insert(attributes.end(), instanceAttributeDescriptions.begin(), instanceAttributeDescriptions.end());

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInputInfo.pVertexBindingDescriptions = bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport & Scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)width;
    viewport.height = (float)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { width, height };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    rasterizer.depthBiasClamp = 0.0f;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth Stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color Blending (None for shadow map)
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0; // No color attachment

    // Push Constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(glm::vec3); // Matrix + Offset

    // Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0; // No descriptor sets for now
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        return false;
    }

    vkDestroyShaderModule(device->getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(device->getDevice(), fragShaderModule, nullptr);

    return true;
}

void ShadowMap::beginRenderPass(VkCommandBuffer commandBuffer) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = { width, height };

    VkClearValue clearValue = { 1.0f, 0 };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void ShadowMap::endRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

glm::mat4 ShadowMap::getLightSpaceMatrix(const glm::vec3& lightDir, const glm::vec3& center, float range) {
    // Place the "sun" camera opposite to the light direction (up in the sky)
    glm::mat4 lightView = glm::lookAt(center - lightDir * range, center, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 lightProj = glm::ortho(-range, range, -range, range, 1.0f, range * 2.0f);
    lightProj[1][1] *= -1; // Vulkan Y flip
    return lightProj * lightView;
}

// ---------------------------------------------------------------------------
// Helper: build the shared depth-only pipeline state (no vertex input yet)
// ---------------------------------------------------------------------------
static bool buildDepthOnlyPipelineState(
    uint32_t width, uint32_t height,
    VkPipelineInputAssemblyStateCreateInfo& inputAssembly,
    VkViewport& viewport,
    VkRect2D& scissor,
    VkPipelineViewportStateCreateInfo& viewportState,
    VkPipelineRasterizationStateCreateInfo& rasterizer,
    VkPipelineMultisampleStateCreateInfo& multisampling,
    VkPipelineDepthStencilStateCreateInfo& depthStencil,
    VkPipelineColorBlendStateCreateInfo& colorBlending)
{
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    scissor  = { {0,0}, {width, height} };
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
    viewportState.scissorCount  = 1; viewportState.pScissors  = &scissor;

    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // No culling for shadow casters
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    rasterizer.depthBiasClamp = 0.0f;

    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0; // No color attachment

    return true;
}

// ---------------------------------------------------------------------------
// Character shadow pipeline (character_shadow.vert)
// Push constant: mat4 model (64) + mat4 lightSpaceMatrix (64) = 128 bytes
// Instance data: CharacterInstanceData (offset vec3, scale vec3, color vec4)
// ---------------------------------------------------------------------------
bool ShadowMap::createCharacterShadowPipeline() {
    auto vertCode = Utils::readFile(Core::AssetManager::instance().resolveShader("character_shadow.vert.spv"));
    auto fragCode = Utils::readFile(Core::AssetManager::instance().resolveShader("shadow.frag.spv"));
    if (vertCode.empty() || fragCode.empty()) {
        LOG_ERROR("ShadowMap", "Failed to load character shadow shaders");
        return false;
    }
    VkShaderModule vertMod = createShaderModule(device->getDevice(), vertCode);
    VkShaderModule fragMod = createShaderModule(device->getDevice(), fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod; stages[1].pName = "main";

    // Per-instance vertex input (CharacterInstanceData): offset, scale, color
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding   = 0;
    bindingDesc.stride    = sizeof(CharacterInstanceData);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attribs[3]{};
    attribs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(CharacterInstanceData, offset) };
    attribs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(CharacterInstanceData, scale) };
    attribs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(CharacterInstanceData, color) };

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount   = 1;
    vertInput.pVertexBindingDescriptions      = &bindingDesc;
    vertInput.vertexAttributeDescriptionCount = 3;
    vertInput.pVertexAttributeDescriptions    = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    VkViewport viewport{}; VkRect2D scissor{};
    VkPipelineViewportStateCreateInfo viewportState{};
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    VkPipelineMultisampleStateCreateInfo multisampling{};
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    buildDepthOnlyPipelineState(width, height, inputAssembly, viewport, scissor, viewportState, rasterizer, multisampling, depthStencil, colorBlending);

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(glm::mat4) * 2; // model + lightSpaceMatrix

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device->getDevice(), &layoutInfo, nullptr, &characterPipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("ShadowMap", "Failed to create character shadow pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.layout              = characterPipelineLayout;
    pipelineInfo.renderPass          = renderPass;
    pipelineInfo.subpass             = 0;

    VkResult r = vkCreateGraphicsPipelines(device->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &characterPipeline);
    vkDestroyShaderModule(device->getDevice(), vertMod, nullptr);
    vkDestroyShaderModule(device->getDevice(), fragMod, nullptr);
    if (r != VK_SUCCESS) { LOG_ERROR("ShadowMap", "Failed to create character shadow pipeline"); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Kinematic shadow pipeline (kinematic_shadow.vert)
// Push constant: mat4 modelMatrix (64) + mat4 lightSpaceMatrix (64) = 128 bytes
// Instance data: KinematicFaceData layout (localPosition, scale, uvOffset, textureIndex, faceId)
// ---------------------------------------------------------------------------
bool ShadowMap::createKinematicShadowPipeline() {
    auto vertCode = Utils::readFile(Core::AssetManager::instance().resolveShader("kinematic_shadow.vert.spv"));
    auto fragCode = Utils::readFile(Core::AssetManager::instance().resolveShader("shadow.frag.spv"));
    if (vertCode.empty() || fragCode.empty()) {
        LOG_ERROR("ShadowMap", "Failed to load kinematic shadow shaders");
        return false;
    }
    VkShaderModule vertMod = createShaderModule(device->getDevice(), vertCode);
    VkShaderModule fragMod = createShaderModule(device->getDevice(), fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod; stages[1].pName = "main";

    // KinematicFaceData layout (matches kinematic_voxel.vert locations 0-4)
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding   = 0;
    bindingDesc.stride    = sizeof(float)*3 + sizeof(float)*3 + sizeof(float)*2 + sizeof(uint32_t)*2; // 40 bytes
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attribs[5]{};
    uint32_t offset = 0;
    attribs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offset }; offset += 12; // localPosition
    attribs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offset }; offset += 12; // scale
    attribs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offset }; offset += 8;  // uvOffset
    attribs[3] = { 3, 0, VK_FORMAT_R32_UINT,         offset }; offset += 4;  // textureIndex
    attribs[4] = { 4, 0, VK_FORMAT_R32_UINT,         offset };               // faceId

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount   = 1;
    vertInput.pVertexBindingDescriptions      = &bindingDesc;
    vertInput.vertexAttributeDescriptionCount = 5;
    vertInput.pVertexAttributeDescriptions    = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    VkViewport viewport{}; VkRect2D scissor{};
    VkPipelineViewportStateCreateInfo viewportState{};
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    VkPipelineMultisampleStateCreateInfo multisampling{};
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    buildDepthOnlyPipelineState(width, height, inputAssembly, viewport, scissor, viewportState, rasterizer, multisampling, depthStencil, colorBlending);

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(glm::mat4) * 2; // modelMatrix + lightSpaceMatrix

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device->getDevice(), &layoutInfo, nullptr, &kinematicPipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("ShadowMap", "Failed to create kinematic shadow pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.layout              = kinematicPipelineLayout;
    pipelineInfo.renderPass          = renderPass;
    pipelineInfo.subpass             = 0;

    VkResult r = vkCreateGraphicsPipelines(device->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &kinematicPipeline);
    vkDestroyShaderModule(device->getDevice(), vertMod, nullptr);
    vkDestroyShaderModule(device->getDevice(), fragMod, nullptr);
    if (r != VK_SUCCESS) { LOG_ERROR("ShadowMap", "Failed to create kinematic shadow pipeline"); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Dynamic (GPU particle) shadow pipeline (dynamic_shadow.vert)
// Push constant: mat4 lightSpaceMatrix (64 bytes) only
// Instance data: DynamicSubcubeInstanceData layout (same as dynamic_voxel.vert)
// ---------------------------------------------------------------------------
bool ShadowMap::createDynamicShadowPipeline() {
    auto vertCode = Utils::readFile(Core::AssetManager::instance().resolveShader("dynamic_shadow.vert.spv"));
    auto fragCode = Utils::readFile(Core::AssetManager::instance().resolveShader("shadow.frag.spv"));
    if (vertCode.empty() || fragCode.empty()) {
        LOG_ERROR("ShadowMap", "Failed to load dynamic shadow shaders");
        return false;
    }
    VkShaderModule vertMod = createShaderModule(device->getDevice(), vertCode);
    VkShaderModule fragMod = createShaderModule(device->getDevice(), fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod; stages[1].pName = "main";

    // DynamicSubcubeInstanceData layout (matches dynamic_voxel.vert locations 0-6)
    // Binding 0: vertexID (per-vertex, uint)
    // Binding 1: per-instance data
    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding = 0; bindings[0].stride = sizeof(uint32_t); bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    // stride = worldPos(12) + textureIndex(4) + faceID(4) + scale(12) + rotation(16) + localPos(12) = 60 → padded to 64
    bindings[1].binding = 1; bindings[1].stride = 64; bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attribs[7]{};
    attribs[0] = { 0, 0, VK_FORMAT_R32_UINT,         0  }; // vertexID
    uint32_t ioff = 0;
    attribs[1] = { 1, 1, VK_FORMAT_R32G32B32_SFLOAT, ioff }; ioff += 12; // worldPosition
    attribs[2] = { 2, 1, VK_FORMAT_R32_UINT,         ioff }; ioff += 4;  // textureIndex
    attribs[3] = { 3, 1, VK_FORMAT_R32_UINT,         ioff }; ioff += 4;  // faceID
    attribs[4] = { 4, 1, VK_FORMAT_R32G32B32_SFLOAT, ioff }; ioff += 12; // scale
    attribs[5] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, ioff }; ioff += 16; // rotation
    attribs[6] = { 6, 1, VK_FORMAT_R32G32B32_SINT,   ioff };               // localPosition

    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount   = 2;
    vertInput.pVertexBindingDescriptions      = bindings;
    vertInput.vertexAttributeDescriptionCount = 7;
    vertInput.pVertexAttributeDescriptions    = attribs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    VkViewport viewport{}; VkRect2D scissor{};
    VkPipelineViewportStateCreateInfo viewportState{};
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    VkPipelineMultisampleStateCreateInfo multisampling{};
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    buildDepthOnlyPipelineState(width, height, inputAssembly, viewport, scissor, viewportState, rasterizer, multisampling, depthStencil, colorBlending);

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(glm::mat4); // lightSpaceMatrix only

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device->getDevice(), &layoutInfo, nullptr, &dynamicPipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("ShadowMap", "Failed to create dynamic shadow pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.layout              = dynamicPipelineLayout;
    pipelineInfo.renderPass          = renderPass;
    pipelineInfo.subpass             = 0;

    VkResult r = vkCreateGraphicsPipelines(device->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &dynamicPipeline);
    vkDestroyShaderModule(device->getDevice(), vertMod, nullptr);
    vkDestroyShaderModule(device->getDevice(), fragMod, nullptr);
    if (r != VK_SUCCESS) { LOG_ERROR("ShadowMap", "Failed to create dynamic shadow pipeline"); return false; }
    return true;
}

} // namespace Graphics
} // namespace Phyxel
