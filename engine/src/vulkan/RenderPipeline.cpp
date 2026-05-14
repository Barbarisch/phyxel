#include "vulkan/RenderPipeline.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"
#include <iostream>
#include <array>

namespace Phyxel {
namespace Vulkan {

RenderPipeline::RenderPipeline(const VulkanDevice& device) : vulkanDevice(device) {
    // Constructor
}

RenderPipeline::~RenderPipeline() {
    cleanup();
}

bool RenderPipeline::createGraphicsPipeline() {
    if (renderPass == VK_NULL_HANDLE) {
        if (!createRenderPass()) {
            LOG_ERROR("Rendering", "Failed to create render pass!");
            return false;
        }
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
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; // Cull front faces instead of back
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // Fixed to match vertex generation order
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

    // Pipeline layout with push constants support
    VkDescriptorSetLayout deviceDescriptorSetLayout = vulkanDevice.getDescriptorSetLayout();
    
    // Add push constant range for chunk base offset
    VkPushConstantRange pushConstantRange = VulkanDevice::getPushConstantRange();
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &deviceDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkResult result = vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (result != VK_SUCCESS) {
        LOG_ERROR_FMT("Rendering", "Failed to create pipeline layout! Error: " << result);
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
        LOG_ERROR_FMT("Rendering", "Failed to create graphics pipeline! Error: " << result);
        return false;
    }

    return true;
}

bool RenderPipeline::createGraphicsPipelineForDynamicSubcubes() {
    // Creates the Vulkan graphics pipeline for dynamic voxel rendering.
    //
    // Topology: TRIANGLE_LIST (6 vertices per face = 2 triangles)
    // Culling:  CULL_FRONT + FRONT_FACE_CCW → CW-wound triangles survive
    //
    // Used by both the GPU compute path (vkCmdDrawIndirect, non-indexed)
    // and the CPU fallback path (vkCmdDrawIndexed, 6 indices per face).
    //
    // Vertex input:
    //   Binding 0 (per-vertex):   Vertex { uint32_t vertexID } — 8 vertices
    //   Binding 1 (per-instance): DynamicSubcubeInstanceData (64 bytes)
    //
    // See docs/DynamicSubcubeRenderPipeline.md for rendering architecture.
    //
    if (renderPass == VK_NULL_HANDLE) {
        if (!createRenderPass()) {
            return false;
        }
    }
    
    // Vertex input - use vertex data and dynamic subcube instance data
    auto vertexBindingDescription = Vertex::getBindingDescription();
    auto vertexAttributeDescriptions = Vertex::getAttributeDescriptions();
    auto instanceBindingDescription = DynamicSubcubeInstanceData::getBindingDescription();
    auto instanceAttributeDescriptions = DynamicSubcubeInstanceData::getAttributeDescriptions();

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

    // Rest of pipeline creation (same as regular pipeline)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(vulkanDevice.getSwapChainExtent().width);
    viewport.height = static_cast<float>(vulkanDevice.getSwapChainExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = vulkanDevice.getSwapChainExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; // Cull front faces instead of back
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create pipeline layout using the shared device descriptor set layout so that
    // voxel.frag can access all bindings (including binding 4 = AtlasUVBuffer).
    VkDescriptorSetLayout deviceDescriptorSetLayout = vulkanDevice.getDescriptorSetLayout();
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &deviceDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;  // No push constants needed
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dynamic pipeline layout!");
    }

    // Shader stages
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

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Create graphics pipeline
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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create dynamic graphics pipeline!");
    }

    return true;
}

bool RenderPipeline::createCharacterPipeline() {
    if (renderPass == VK_NULL_HANDLE) {
        if (!createRenderPass()) {
            LOG_ERROR("Rendering", "Failed to create render pass!");
            return false;
        }
    }

    // Create Character Pipeline Layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 144; // mat4 model (64) + mat4 viewProj (64) + vec4 color (16)

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout descSetLayout = vulkanDevice.getDescriptorSetLayout();
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &characterPipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("Rendering", "Failed to create character pipeline layout!");
        return false;
    }

    // Vertex input
    // No vertex input - vertices are generated in the shader
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth and stencil
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

    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = characterVertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = characterFragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = characterPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &characterPipeline) != VK_SUCCESS) {
        LOG_ERROR("Rendering", "Failed to create character graphics pipeline!");
        return false;
    }

    return true;
}

bool RenderPipeline::createDebugGraphicsPipeline() {
    // Similar to createGraphicsPipeline but with wireframe polygon mode and debug shaders
    
    // Vertex input - use both vertex and instance data (same as regular pipeline)
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

    // Rasterizer - KEY DIFFERENCE: Use LINE mode for wireframe visualization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;  // Wireframe mode!
    rasterizer.lineWidth = 1.5f;  // Slightly thicker lines for visibility
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Show all geometry in debug mode
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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

    // Debug shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    
    if (debugVertShaderModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = debugVertShaderModule;
        vertShaderStageInfo.pName = "main";
        shaderStages.push_back(vertShaderStageInfo);
    }

    if (debugFragShaderModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = debugFragShaderModule;
        fragShaderStageInfo.pName = "main";
        shaderStages.push_back(fragShaderStageInfo);
    }

    if (shaderStages.empty()) {
        LOG_ERROR("Rendering", "Debug shaders not loaded!");
        return false;
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
    pipelineInfo.layout = pipelineLayout;  // Use same layout as regular pipeline
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &debugGraphicsPipeline);
    if (result != VK_SUCCESS) {
        LOG_ERROR_FMT("Rendering", "Failed to create debug graphics pipeline! Error: " << result);
        return false;
    }

    LOG_INFO("Rendering", "Debug graphics pipeline created successfully");
    return true;
}

bool RenderPipeline::createDebugLinePipeline() {
    // Pipeline for debug line rendering (raycast visualization, etc.)
    
    // Vertex input - only position and color (simpler than voxel rendering)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 6;  // 3 pos + 3 color
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(2);
    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;
    // Color
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 3;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly - LINE LIST topology
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor (dynamic)
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
    rasterizer.lineWidth = 2.0f;  // Thicker lines for visibility
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth testing - read but don't write (render on top)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  // Don't write to depth buffer
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending with alpha
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

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

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    if (debugLineVertShaderModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = debugLineVertShaderModule;
        vertShaderStageInfo.pName = "main";
        shaderStages.push_back(vertShaderStageInfo);
    }

    if (debugLineFragShaderModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = debugLineFragShaderModule;
        fragShaderStageInfo.pName = "main";
        shaderStages.push_back(fragShaderStageInfo);
    }

    if (shaderStages.empty()) {
        LOG_ERROR("Rendering", "Debug line shaders not loaded!");
        return false;
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
    pipelineInfo.layout = pipelineLayout;  // Use same layout as regular pipeline
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &debugLinePipeline);
    if (result != VK_SUCCESS) {
        LOG_ERROR_FMT("Rendering", "Failed to create debug line pipeline! Error: " << result);
        return false;
    }

    LOG_INFO("Rendering", "Debug line pipeline created successfully");
    return true;
}

void RenderPipeline::setRenderPass(VkRenderPass pass) {
    if (ownsRenderPass && renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vulkanDevice.getDevice(), renderPass, nullptr);
    }
    renderPass = pass;
    ownsRenderPass = false;
}

void RenderPipeline::cleanup() {
    VkDevice device = vulkanDevice.getDevice();

    if (graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        graphicsPipeline = VK_NULL_HANDLE;
    }

    if (debugGraphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, debugGraphicsPipeline, nullptr);
        debugGraphicsPipeline = VK_NULL_HANDLE;
    }

    if (debugLinePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, debugLinePipeline, nullptr);
        debugLinePipeline = VK_NULL_HANDLE;
    }

    if (oitPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, oitPipeline, nullptr);
        oitPipeline = VK_NULL_HANDLE;
    }

    if (oitFragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, oitFragShaderModule, nullptr);
        oitFragShaderModule = VK_NULL_HANDLE;
    }

    // Mirror pipeline cleanup
    if (reflectionScenePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, reflectionScenePipeline, nullptr);
        reflectionScenePipeline = VK_NULL_HANDLE;
    }
    if (mirrorPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, mirrorPipeline, nullptr);
        mirrorPipeline = VK_NULL_HANDLE;
    }
    if (mirrorFragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, mirrorFragShaderModule, nullptr);
        mirrorFragShaderModule = VK_NULL_HANDLE;
    }
    if (mirrorPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, mirrorPipelineLayout, nullptr);
        mirrorPipelineLayout = VK_NULL_HANDLE;
    }
    if (mirrorDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, mirrorDescriptorPool, nullptr);
        mirrorDescriptorPool = VK_NULL_HANDLE;
        mirrorReflectionDescriptorSet = VK_NULL_HANDLE;
    }
    if (mirrorReflectionDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, mirrorReflectionDescSetLayout, nullptr);
        mirrorReflectionDescSetLayout = VK_NULL_HANDLE;
    }

    if (characterPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, characterPipeline, nullptr);
        characterPipeline = VK_NULL_HANDLE;
    }

    if (instancedCharacterPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, instancedCharacterPipeline, nullptr);
        instancedCharacterPipeline = VK_NULL_HANDLE;
    }

    if (characterPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, characterPipelineLayout, nullptr);
        characterPipelineLayout = VK_NULL_HANDLE;
    }

    if (instancedCharacterPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, instancedCharacterPipelineLayout, nullptr);
        instancedCharacterPipelineLayout = VK_NULL_HANDLE;
    }

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }

    if (ownsRenderPass && renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    // Note: descriptorSetLayout is now managed by VulkanDevice, don't destroy it here

    if (vertShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vertShaderModule = VK_NULL_HANDLE;
    }

    if (fragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        fragShaderModule = VK_NULL_HANDLE;
    }

    if (characterVertShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, characterVertShaderModule, nullptr);
        characterVertShaderModule = VK_NULL_HANDLE;
    }

    if (characterFragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, characterFragShaderModule, nullptr);
        characterFragShaderModule = VK_NULL_HANDLE;
    }

    if (instancedCharacterVertShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, instancedCharacterVertShaderModule, nullptr);
        instancedCharacterVertShaderModule = VK_NULL_HANDLE;
    }

    if (instancedCharacterFragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, instancedCharacterFragShaderModule, nullptr);
        instancedCharacterFragShaderModule = VK_NULL_HANDLE;
    }

    if (debugVertShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, debugVertShaderModule, nullptr);
        debugVertShaderModule = VK_NULL_HANDLE;
    }

    if (debugFragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, debugFragShaderModule, nullptr);
        debugFragShaderModule = VK_NULL_HANDLE;
    }

    if (debugLineVertShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, debugLineVertShaderModule, nullptr);
        debugLineVertShaderModule = VK_NULL_HANDLE;
    }

    if (debugLineFragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, debugLineFragShaderModule, nullptr);
        debugLineFragShaderModule = VK_NULL_HANDLE;
    }
}

bool RenderPipeline::loadShaders(const std::string& vertPath, const std::string& fragPath) {
    auto vertShaderCode = Utils::readFile(vertPath);
    auto fragShaderCode = Utils::readFile(fragPath);

    if (vertShaderCode.empty()) {
        LOG_ERROR_FMT("Rendering", "Failed to load vertex shader: " << vertPath);
        return false;
    }

    if (fragShaderCode.empty()) {
        LOG_ERROR_FMT("Rendering", "Failed to load fragment shader: " << fragPath);
        return false;
    }

    vertShaderModule = createShaderModule(vertShaderCode);
    fragShaderModule = createShaderModule(fragShaderCode);

    if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        LOG_ERROR("Rendering", "Failed to create shader modules!");
        return false;
    }

    return true;
}

bool RenderPipeline::loadCharacterShaders(const std::string& vertPath, const std::string& fragPath) {
    auto vertShaderCode = Utils::readFile(vertPath);
    auto fragShaderCode = Utils::readFile(fragPath);

    if (vertShaderCode.empty()) {
        LOG_ERROR_FMT("Rendering", "Failed to load character vertex shader: " << vertPath);
        return false;
    }

    if (fragShaderCode.empty()) {
        LOG_ERROR_FMT("Rendering", "Failed to load character fragment shader: " << fragPath);
        return false;
    }

    characterVertShaderModule = createShaderModule(vertShaderCode);
    characterFragShaderModule = createShaderModule(fragShaderCode);

    if (characterVertShaderModule == VK_NULL_HANDLE || characterFragShaderModule == VK_NULL_HANDLE) {
        LOG_ERROR("Rendering", "Failed to create character shader modules!");
        return false;
    }

    return true;
}

bool RenderPipeline::loadDebugShaders(const std::string& vertPath, const std::string& fragPath) {
    auto vertShaderCode = Utils::readFile(vertPath);
    auto fragShaderCode = Utils::readFile(fragPath);

    if (vertShaderCode.empty()) {
        LOG_ERROR_FMT("Rendering", "Failed to load debug vertex shader: " << vertPath);
        return false;
    }

    if (fragShaderCode.empty()) {
        LOG_ERROR_FMT("Rendering", "Failed to load debug fragment shader: " << fragPath);
        return false;
    }

    debugVertShaderModule = createShaderModule(vertShaderCode);
    debugFragShaderModule = createShaderModule(fragShaderCode);

    if (debugVertShaderModule == VK_NULL_HANDLE || debugFragShaderModule == VK_NULL_HANDLE) {
        LOG_ERROR("Rendering", "Failed to create debug shader modules!");
        return false;
    }

    return true;
}

bool RenderPipeline::loadDebugLineShaders(const std::string& vertPath, const std::string& fragPath) {
    auto vertShaderCode = Utils::readFile(vertPath);
    auto fragShaderCode = Utils::readFile(fragPath);

    if (vertShaderCode.empty()) {
        LOG_ERROR_FMT("Rendering", "Failed to load debug line vertex shader: " << vertPath);
        return false;
    }

    if (fragShaderCode.empty()) {
        LOG_ERROR_FMT("Rendering", "Failed to load debug line fragment shader: " << fragPath);
        return false;
    }

    debugLineVertShaderModule = createShaderModule(vertShaderCode);
    debugLineFragShaderModule = createShaderModule(fragShaderCode);

    if (debugLineVertShaderModule == VK_NULL_HANDLE || debugLineFragShaderModule == VK_NULL_HANDLE) {
        LOG_ERROR("Rendering", "Failed to create debug line shader modules!");
        return false;
    }

    return true;
}

void RenderPipeline::bindGraphicsPipeline(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
}

void RenderPipeline::bindCharacterPipeline(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, characterPipeline);
}

void RenderPipeline::bindDebugGraphicsPipeline(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debugGraphicsPipeline);
}

void RenderPipeline::bindDebugLinePipeline(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debugLinePipeline);
}

void RenderPipeline::bindOITPipeline(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, oitPipeline);
}

bool RenderPipeline::createOITPipeline(VkRenderPass oitRenderPass) {
    VkDevice device = vulkanDevice.getDevice();

    // Load transparent_voxel.frag shader
    auto fragCode = Utils::readFile("shaders/transparent_voxel.frag.spv");
    if (fragCode.empty()) {
        LOG_ERROR("RenderPipeline", "Failed to load transparent_voxel.frag.spv");
        return false;
    }
    if (oitFragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, oitFragShaderModule, nullptr);
        oitFragShaderModule = VK_NULL_HANDLE;
    }
    oitFragShaderModule = createShaderModule(fragCode);

    // Vertex stage: same static_voxel.vert as opaque pass
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertShaderModule; // already loaded (static_voxel.vert)
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = oitFragShaderModule;
    stages[1].pName = "main";

    // Same vertex input as opaque pipeline
    auto vertexBindingDescription = Vertex::getBindingDescription();
    auto vertexAttributeDescriptions = Vertex::getAttributeDescriptions();
    auto instanceBindingDescription = InstanceData::getBindingDescription();
    auto instanceAttributeDescriptions = InstanceData::getAttributeDescriptions();

    std::vector<VkVertexInputBindingDescription> bindingDescs = { vertexBindingDescription, instanceBindingDescription };
    std::vector<VkVertexInputAttributeDescription> attrDescs;
    attrDescs.insert(attrDescs.end(), vertexAttributeDescriptions.begin(), vertexAttributeDescriptions.end());
    attrDescs.insert(attrDescs.end(), instanceAttributeDescriptions.begin(), instanceAttributeDescriptions.end());

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescs.size());
    vi.pVertexBindingDescriptions = bindingDescs.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vi.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE; // No culling for transparent — show both sides
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth: test=true (occlude by opaque geometry), write=false (don't overwrite opaque depth)
    // Use LESS_OR_EQUAL (not LESS) because early-z in the opaque pass may have written depth for
    // glass fragments before voxel.frag discards them. Those fragments land at exactly the same
    // depth in the OIT pass, so LESS would fail (D < D = false). LESS_OR_EQUAL passes correctly.
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Blend attachment 0 (accum): additive blending — src=ONE, dst=ONE
    VkPipelineColorBlendAttachmentState blendAccum{};
    blendAccum.blendEnable = VK_TRUE;
    blendAccum.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAccum.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAccum.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAccum.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAccum.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAccum.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAccum.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    // Blend attachment 1 (reveal): multiplicative — dst = src_color * dst (1-alpha accumulates)
    VkPipelineColorBlendAttachmentState blendReveal{};
    blendReveal.blendEnable = VK_TRUE;
    blendReveal.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendReveal.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
    blendReveal.colorBlendOp        = VK_BLEND_OP_ADD;
    blendReveal.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendReveal.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendReveal.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendReveal.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT;

    std::array<VkPipelineColorBlendAttachmentState, 2> blendAtts = { blendAccum, blendReveal };
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 2; cb.pAttachments = blendAtts.data();

    std::vector<VkDynamicState> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dyn.pDynamicStates = dynStates.data();

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2; gpi.pStages = stages;
    gpi.pVertexInputState   = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState      = &vps;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState   = &ms;
    gpi.pDepthStencilState  = &ds;
    gpi.pColorBlendState    = &cb;
    gpi.pDynamicState       = &dyn;
    gpi.layout      = pipelineLayout; // same layout as opaque
    gpi.renderPass  = oitRenderPass;
    gpi.subpass     = 0;

    if (oitPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, oitPipeline, nullptr);
        oitPipeline = VK_NULL_HANDLE;
    }
    VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, nullptr, &oitPipeline);
    if (res != VK_SUCCESS) {
        LOG_ERROR_FMT("RenderPipeline", "Failed to create OIT pipeline! VkResult=" << res);
        return false;
    }
    LOG_INFO("RenderPipeline", "OIT transparent pipeline created");
    return true;
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
        LOG_ERROR_FMT("Rendering", "Failed to create render pass! Error: " << result);
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
        LOG_ERROR_FMT("Rendering", "Failed to create shader module! Error: " << result);
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

bool RenderPipeline::createDescriptorSetLayout() {
    LOG_DEBUG("Rendering", "Creating descriptor set layout with texture atlas support...");
    
    // UBO binding (binding 0)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Texture atlas sampler binding (binding 1)
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Shadow map sampler binding (binding 2)
    VkDescriptorSetLayoutBinding shadowMapLayoutBinding{};
    shadowMapLayoutBinding.binding = 2;
    shadowMapLayoutBinding.descriptorCount = 1;
    shadowMapLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapLayoutBinding.pImmutableSamplers = nullptr;
    shadowMapLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // All bindings
    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {uboLayoutBinding, samplerLayoutBinding, shadowMapLayoutBinding};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(vulkanDevice.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout);
    if (result != VK_SUCCESS) {
        LOG_ERROR_FMT("Rendering", "Failed to create descriptor set layout! Error: " << result);
        return false;
    }
    
    LOG_DEBUG_FMT("Rendering", "Descriptor set layout created successfully with texture atlas: " << descriptorSetLayout);

    return true;
}

bool RenderPipeline::createDynamicDescriptorSetLayout() {
    LOG_DEBUG("Rendering", "Creating dynamic descriptor set layout (same as static for texture consistency)...");
    
    // UBO binding (binding 0)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Texture sampler binding (binding 1) - same as static pipeline for compatibility
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
        LOG_ERROR_FMT("Rendering", "Failed to create dynamic descriptor set layout! Error: " << result);
        return false;
    }
    
    LOG_DEBUG_FMT("Rendering", "Dynamic descriptor set layout created successfully: " << descriptorSetLayout);

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

bool RenderPipeline::loadInstancedCharacterShaders(const std::string& vertPath, const std::string& fragPath) {
    auto vertCode = Utils::readFile(vertPath);
    auto fragCode = Utils::readFile(fragPath);

    if (vertCode.empty() || fragCode.empty()) {
        LOG_ERROR("Rendering", "Failed to load instanced character shader files!");
        return false;
    }

    instancedCharacterVertShaderModule = createShaderModule(vertCode);
    instancedCharacterFragShaderModule = createShaderModule(fragCode);

    if (instancedCharacterVertShaderModule == VK_NULL_HANDLE || instancedCharacterFragShaderModule == VK_NULL_HANDLE) {
        return false;
    }

    return true;
}

bool RenderPipeline::createInstancedCharacterPipeline() {
    if (instancedCharacterVertShaderModule == VK_NULL_HANDLE || instancedCharacterFragShaderModule == VK_NULL_HANDLE) {
        LOG_ERROR("Rendering", "Instanced character shaders not loaded!");
        return false;
    }

    // Pipeline layout with push constants support
    VkDescriptorSetLayout deviceDescriptorSetLayout = vulkanDevice.getDescriptorSetLayout();
    
    // Add push constant range for model matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) * 2; // model + viewProj

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &deviceDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &instancedCharacterPipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("Rendering", "Failed to create instanced character pipeline layout!");
        return false;
    }

    // Vertex input - Use CharacterInstanceData
    auto bindingDescription = CharacterInstanceData::getBindingDescription();
    auto attributeDescriptions = CharacterInstanceData::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth and stencil
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

    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = instancedCharacterVertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = instancedCharacterFragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = instancedCharacterPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &instancedCharacterPipeline) != VK_SUCCESS) {
        LOG_ERROR("Rendering", "Failed to create instanced character graphics pipeline!");
        return false;
    }

    return true;
}

void RenderPipeline::bindInstancedCharacterPipeline(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedCharacterPipeline);
}

void RenderPipeline::bindMirrorPipeline(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet mirrorReflDescSet) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mirrorPipeline);
    // Bind reflection texture at set 1
    if (mirrorReflDescSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            mirrorPipelineLayout, 1, 1, &mirrorReflDescSet, 0, nullptr);
    }
}

bool RenderPipeline::createMirrorPipeline(VkRenderPass sceneRenderPass) {
    VkDevice device = vulkanDevice.getDevice();

    // Load mirror_voxel.frag
    auto fragCode = Utils::readFile("shaders/mirror_voxel.frag.spv");
    if (fragCode.empty()) {
        LOG_ERROR("RenderPipeline", "Failed to load mirror_voxel.frag.spv");
        return false;
    }
    if (mirrorFragShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, mirrorFragShaderModule, nullptr);
        mirrorFragShaderModule = VK_NULL_HANDLE;
    }
    mirrorFragShaderModule = createShaderModule(fragCode);

    // --- Descriptor set layout for set 1: just the reflection sampler ---
    if (mirrorReflectionDescSetLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding reflBinding{};
        reflBinding.binding = 0;
        reflBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        reflBinding.descriptorCount = 1;
        reflBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &reflBinding;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &mirrorReflectionDescSetLayout) != VK_SUCCESS) {
            LOG_ERROR("RenderPipeline", "Failed to create mirror reflection descriptor set layout");
            return false;
        }
    }

    // --- Descriptor pool + set for reflection texture ---
    if (mirrorDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, mirrorDescriptorPool, nullptr);
        mirrorDescriptorPool = VK_NULL_HANDLE;
        mirrorReflectionDescriptorSet = VK_NULL_HANDLE;
    }
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &mirrorDescriptorPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mirrorDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mirrorReflectionDescSetLayout;
    vkAllocateDescriptorSets(device, &allocInfo, &mirrorReflectionDescriptorSet);

    // --- Pipeline layout: set 0 (main) + set 1 (reflection) ---
    if (mirrorPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, mirrorPipelineLayout, nullptr);
        mirrorPipelineLayout = VK_NULL_HANDLE;
    }
    VkDescriptorSetLayout setLayouts[] = { vulkanDevice.getDescriptorSetLayout(), mirrorReflectionDescSetLayout };
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = 16; // same as main pipeline (vec3 + int)
    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.setLayoutCount = 2;
    layoutCI.pSetLayouts = setLayouts;
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device, &layoutCI, nullptr, &mirrorPipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("RenderPipeline", "Failed to create mirror pipeline layout");
        return false;
    }

    // --- Shader stages ---
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertShaderModule; // static_voxel.vert
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = mirrorFragShaderModule;
    stages[1].pName = "main";

    // --- Vertex input (same as opaque pipeline) ---
    auto vertexBindingDescription = Vertex::getBindingDescription();
    auto vertexAttributeDescriptions = Vertex::getAttributeDescriptions();
    auto instanceBindingDescription = InstanceData::getBindingDescription();
    auto instanceAttributeDescriptions = InstanceData::getAttributeDescriptions();
    std::vector<VkVertexInputBindingDescription> bindingDescs = { vertexBindingDescription, instanceBindingDescription };
    std::vector<VkVertexInputAttributeDescription> attrDescs;
    attrDescs.insert(attrDescs.end(), vertexAttributeDescriptions.begin(), vertexAttributeDescriptions.end());
    attrDescs.insert(attrDescs.end(), instanceAttributeDescriptions.begin(), instanceAttributeDescriptions.end());
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescs.size());
    vi.pVertexBindingDescriptions = bindingDescs.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vi.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_BACK_BIT; // Normal back-face culling
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth: test and write (mirrors are opaque surfaces)
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    // Opaque blend: no blending, replace with mirror color
    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable = VK_FALSE;
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &blendAtt;

    std::vector<VkDynamicState> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dyn.pDynamicStates = dynStates.data();

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2; gpi.pStages = stages;
    gpi.pVertexInputState   = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState      = &vps;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState   = &ms;
    gpi.pDepthStencilState  = &ds;
    gpi.pColorBlendState    = &cb;
    gpi.pDynamicState       = &dyn;
    gpi.layout     = mirrorPipelineLayout;
    gpi.renderPass = sceneRenderPass;
    gpi.subpass    = 0;

    if (mirrorPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, mirrorPipeline, nullptr);
        mirrorPipeline = VK_NULL_HANDLE;
    }
    VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpi, nullptr, &mirrorPipeline);
    if (res != VK_SUCCESS) {
        LOG_ERROR("RenderPipeline", "Failed to create mirror pipeline (VkResult={})", static_cast<int>(res));
        return false;
    }
    LOG_INFO("RenderPipeline", "Mirror pipeline created");
    return true;
}

bool RenderPipeline::createReflectionScenePipeline(VkRenderPass sceneRenderPass) {
    // Identical to the main opaque pipeline but uses BACK_BIT culling so that geometry
    // rendered from a reflected camera (which flips winding order) is visible instead of culled.
    if (pipelineLayout == VK_NULL_HANDLE || vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        LOG_ERROR("RenderPipeline", "createReflectionScenePipeline: must call createGraphicsPipeline first");
        return false;
    }

    auto vertexBindingDescription    = Vertex::getBindingDescription();
    auto vertexAttributeDescriptions = Vertex::getAttributeDescriptions();
    auto instanceBindingDescription  = InstanceData::getBindingDescription();
    auto instanceAttributeDescriptions = InstanceData::getAttributeDescriptions();
    std::vector<VkVertexInputBindingDescription> bindingDescs = { vertexBindingDescription, instanceBindingDescription };
    std::vector<VkVertexInputAttributeDescription> attrDescs;
    attrDescs.insert(attrDescs.end(), vertexAttributeDescriptions.begin(), vertexAttributeDescriptions.end());
    attrDescs.insert(attrDescs.end(), instanceAttributeDescriptions.begin(), instanceAttributeDescriptions.end());

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = static_cast<uint32_t>(bindingDescs.size());
    vi.pVertexBindingDescriptions      = bindingDescs.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vi.pVertexAttributeDescriptions    = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vps{};
    vps.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth   = 1.0f;
    rs.cullMode    = VK_CULL_MODE_BACK_BIT;              // BACK_BIT: after winding flip from reflection, renders the correct faces
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable    = VK_FALSE;
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &blendAtt;

    std::vector<VkDynamicState> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dyn.pDynamicStates    = dynStates.data();

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertShaderModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragShaderModule;
    stages[1].pName  = "main";

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount          = 2; gpi.pStages = stages;
    gpi.pVertexInputState   = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState      = &vps;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState   = &ms;
    gpi.pDepthStencilState  = &ds;
    gpi.pColorBlendState    = &cb;
    gpi.pDynamicState       = &dyn;
    gpi.layout              = pipelineLayout;
    gpi.renderPass          = sceneRenderPass;
    gpi.subpass             = 0;

    if (reflectionScenePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanDevice.getDevice(), reflectionScenePipeline, nullptr);
        reflectionScenePipeline = VK_NULL_HANDLE;
    }
    VkResult res = vkCreateGraphicsPipelines(vulkanDevice.getDevice(), VK_NULL_HANDLE, 1, &gpi, nullptr, &reflectionScenePipeline);
    if (res != VK_SUCCESS) {
        LOG_ERROR("RenderPipeline", "Failed to create reflection scene pipeline (VkResult={})", static_cast<int>(res));
        return false;
    }
    LOG_INFO("RenderPipeline", "Reflection scene pipeline created");
    return true;
}

void RenderPipeline::updateMirrorReflectionDescriptor(VkImageView reflectionView, VkSampler reflectionSampler) {
    if (mirrorReflectionDescriptorSet == VK_NULL_HANDLE) return;
    if (reflectionView == VK_NULL_HANDLE || reflectionSampler == VK_NULL_HANDLE) {
        LOG_WARN("RenderPipeline", "updateMirrorReflectionDescriptor called with null handles — skipping");
        return;
    }
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView = reflectionView;
    imgInfo.sampler = reflectionSampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = mirrorReflectionDescriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(vulkanDevice.getDevice(), 1, &write, 0, nullptr);
}

} // namespace Vulkan
} // namespace Phyxel
