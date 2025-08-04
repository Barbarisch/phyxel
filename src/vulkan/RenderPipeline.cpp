#include "vulkan/RenderPipeline.h"
#include "utils/FileUtils.h"
#include <iostream>
#include <array>

namespace VulkanCube {
namespace Vulkan {

RenderPipeline::RenderPipeline(const VulkanDevice& device) : vulkanDevice(device) {
    // Constructor
}

RenderPipeline::~RenderPipeline() {
    cleanup();
}

bool RenderPipeline::createGraphicsPipeline() {
    if (!createRenderPass()) {
        std::cerr << "Failed to create render pass!" << std::endl;
        return false;
    }

    // Vertex input - use both vertex and instance data
    auto vertexBindingDescription = Vertex::getBindingDescription();
    auto vertexAttributeDescriptions = Vertex::getAttributeDescriptions();
    auto instanceBindingDescription = InstanceData::getBindingDescription();
    auto instanceAttributeDescriptions = InstanceData::getAttributeDescriptions();

    std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
        vertexBindingDescription,
        instanceBindingDescription
    };

    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    attributeDescriptions.insert(attributeDescriptions.end(), vertexAttributeDescriptions.begin(), vertexAttributeDescriptions.end());
    attributeDescriptions.insert(attributeDescriptions.end(), instanceAttributeDescriptions.begin(), instanceAttributeDescriptions.end());

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor will be dynamic
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // Changed to match our index buffer winding
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth and stencil testing
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Pipeline layout
    VkDescriptorSetLayout deviceDescriptorSetLayout = vulkanDevice.getDescriptorSetLayout();
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &deviceDescriptorSetLayout;

    VkResult result = vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create pipeline layout! Error: " << result << std::endl;
        return false;
    }

    // Shader stages (assuming shaders are already loaded)
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    
    if (vertShaderModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";
        shaderStages.push_back(vertShaderStageInfo);
    }

    if (fragShaderModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";
        shaderStages.push_back(fragShaderStageInfo);
    }

    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    result = vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create graphics pipeline! Error: " << result << std::endl;
        return false;
    }

    return true;
}

bool RenderPipeline::createComputePipeline() {
    if (!createComputeDescriptorSetLayout()) {
        std::cerr << "Failed to create compute descriptor set layout!" << std::endl;
        return false;
    }

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;

    VkResult result = vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &computePipelineLayout);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create compute pipeline layout! Error: " << result << std::endl;
        return false;
    }

    // Shader stage
    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    // Compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = computePipelineLayout;

    result = vkCreateComputePipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create compute pipeline! Error: " << result << std::endl;
        return false;
    }

    return true;
}

void RenderPipeline::cleanup() {
    VkDevice device = vulkanDevice.getDevice();

    if (graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        graphicsPipeline = VK_NULL_HANDLE;
    }

    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    if (computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
        computePipelineLayout = VK_NULL_HANDLE;
    }

    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    // Note: descriptorSetLayout is now managed by VulkanDevice, don't destroy it here

    if (computeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
        computeDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (vertShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vertShaderModule = VK_NULL_HANDLE;
    }

    if (fragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        fragShaderModule = VK_NULL_HANDLE;
    }

    if (computeShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, computeShaderModule, nullptr);
        computeShaderModule = VK_NULL_HANDLE;
    }
}

bool RenderPipeline::loadShaders(const std::string& vertPath, const std::string& fragPath) {
    auto vertShaderCode = Utils::readFile(vertPath);
    auto fragShaderCode = Utils::readFile(fragPath);

    if (vertShaderCode.empty()) {
        std::cerr << "Failed to load vertex shader: " << vertPath << std::endl;
        return false;
    }

    if (fragShaderCode.empty()) {
        std::cerr << "Failed to load fragment shader: " << fragPath << std::endl;
        return false;
    }

    vertShaderModule = createShaderModule(vertShaderCode);
    fragShaderModule = createShaderModule(fragShaderCode);

    if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        std::cerr << "Failed to create shader modules!" << std::endl;
        return false;
    }

    return true;
}

bool RenderPipeline::loadComputeShader(const std::string& compPath) {
    auto computeShaderCode = Utils::readFile(compPath);

    if (computeShaderCode.empty()) {
        std::cerr << "Failed to load compute shader: " << compPath << std::endl;
        return false;
    }

    computeShaderModule = createShaderModule(computeShaderCode);

    if (computeShaderModule == VK_NULL_HANDLE) {
        std::cerr << "Failed to create compute shader module!" << std::endl;
        return false;
    }

    return true;
}

void RenderPipeline::bindGraphicsPipeline(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
}

void RenderPipeline::bindComputePipeline(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
}

bool RenderPipeline::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_B8G8R8A8_SRGB; // Should be determined by swapchain
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(vulkanDevice.getDevice(), &renderPassInfo, nullptr, &renderPass);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create render pass! Error: " << result << std::endl;
        return false;
    }

    return true;
}

VkShaderModule RenderPipeline::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    VkResult result = vkCreateShaderModule(vulkanDevice.getDevice(), &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create shader module! Error: " << result << std::endl;
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

bool RenderPipeline::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor set layout! Error: " << result << std::endl;
        return false;
    }

    return true;
}

bool RenderPipeline::createComputeDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    // Instance data buffer
    VkDescriptorSetLayoutBinding instanceBinding{};
    instanceBinding.binding = 0;
    instanceBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    instanceBinding.descriptorCount = 1;
    instanceBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(instanceBinding);

    // Uniform buffer
    VkDescriptorSetLayoutBinding uniformBinding{};
    uniformBinding.binding = 1;
    uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBinding.descriptorCount = 1;
    uniformBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(uniformBinding);

    // Visibility buffer
    VkDescriptorSetLayoutBinding visibilityBinding{};
    visibilityBinding.binding = 2;
    visibilityBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    visibilityBinding.descriptorCount = 1;
    visibilityBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(visibilityBinding);

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &computeDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create compute descriptor set layout! Error: " << result << std::endl;
        return false;
    }

    return true;
}

VkFormat RenderPipeline::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(vulkanDevice.getPhysicalDevice(), format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    return VK_FORMAT_UNDEFINED;
}

VkFormat RenderPipeline::findDepthFormat() {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

} // namespace Vulkan
} // namespace VulkanCube
