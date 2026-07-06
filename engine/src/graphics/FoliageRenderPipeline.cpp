#include "graphics/FoliageRenderPipeline.h"
#include "core/AssetManager.h"
#include "core/Types.h"
#include "utils/Logger.h"

#include <fstream>
#include <stdexcept>
#include <array>

namespace Phyxel {
namespace Graphics {

// Push constant layout — MUST match foliage.vert. Time + camera come from the UBO.
struct FoliagePush {
    glm::vec3 chunkBaseOffset;
    float     cardSize;
    float     windStrength;
    float     radius;
    uint32_t  cardsPerVoxel;
    uint32_t  _pad;
};

static std::vector<char> readShaderFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("FoliageRenderPipeline: cannot open shader: " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

FoliageRenderPipeline::FoliageRenderPipeline() {}
FoliageRenderPipeline::~FoliageRenderPipeline() { cleanup(); }

void FoliageRenderPipeline::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_shadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    m_pipeline       = VK_NULL_HANDLE;
    m_shadowPipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
}

bool FoliageRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                       VkRenderPass renderPass, VkExtent2D extent,
                                       VkDescriptorSetLayout uboDescriptorSetLayout) {
    m_device         = device;
    m_physicalDevice = physicalDevice;
    try {
        createPipeline(renderPass, extent, uboDescriptorSetLayout);
    } catch (const std::exception& e) {
        LOG_ERROR("FoliageRenderPipeline", "Initialization failed: {}", e.what());
        return false;
    }
    LOG_INFO("FoliageRenderPipeline", "Initialized (leaf card layer)");
    return true;
}

void FoliageRenderPipeline::recreatePipeline(VkRenderPass renderPass, VkExtent2D extent) {
    (void)renderPass; (void)extent;
    LOG_WARN("FoliageRenderPipeline", "recreatePipeline called — use initialize() after resize");
}

void FoliageRenderPipeline::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                           VkDescriptorSetLayout uboLayout) {
    auto vertCode = readShaderFile(Core::AssetManager::instance().resolveShader("foliage.vert.spv"));
    auto fragCode = readShaderFile(Core::AssetManager::instance().resolveShader("foliage.frag.spv"));

    VkShaderModule vertModule, fragModule;
    VkShaderModuleCreateInfo smInfo{};
    smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = vertCode.size();
    smInfo.pCode    = reinterpret_cast<const uint32_t*>(vertCode.data());
    vkCreateShaderModule(m_device, &smInfo, nullptr, &vertModule);
    smInfo.codeSize = fragCode.size();
    smInfo.pCode    = reinterpret_cast<const uint32_t*>(fragCode.data());
    vkCreateShaderModule(m_device, &smInfo, nullptr, &fragModule);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding = FoliageInstanceData::getBindingDescription();
    auto attrs = FoliageInstanceData::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions    = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth   = 1.0f;
    rasterizer.cullMode    = VK_CULL_MODE_NONE;   // leaf cards are double-sided
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;      // cutout: kept fragments write depth
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_FALSE;           // alpha-tested, not blended → no OIT cost

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttach;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(FoliagePush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &uboLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create FoliageRenderPipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisample;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.layout              = m_pipelineLayout;
    pipelineInfo.renderPass          = renderPass;
    pipelineInfo.subpass             = 0;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create FoliageRenderPipeline");
    }

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}

bool FoliageRenderPipeline::initializeShadow(VkRenderPass shadowRenderPass, VkExtent2D shadowExtent) {
    if (m_device == VK_NULL_HANDLE || m_pipelineLayout == VK_NULL_HANDLE) {
        LOG_ERROR("FoliageRenderPipeline", "initializeShadow called before initialize()");
        return false;
    }
    try {
        auto vertCode = readShaderFile(Core::AssetManager::instance().resolveShader("foliage_shadow.vert.spv"));
        auto fragCode = readShaderFile(Core::AssetManager::instance().resolveShader("foliage_shadow.frag.spv"));

        VkShaderModule vertModule, fragModule;
        VkShaderModuleCreateInfo smInfo{};
        smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smInfo.codeSize = vertCode.size();
        smInfo.pCode    = reinterpret_cast<const uint32_t*>(vertCode.data());
        vkCreateShaderModule(m_device, &smInfo, nullptr, &vertModule);
        smInfo.codeSize = fragCode.size();
        smInfo.pCode    = reinterpret_cast<const uint32_t*>(fragCode.data());
        vkCreateShaderModule(m_device, &smInfo, nullptr, &fragModule);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName  = "main";

        VkVertexInputBindingDescription binding = FoliageInstanceData::getBindingDescription();
        auto attrs = FoliageInstanceData::getAttributeDescriptions();
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount   = 1;
        vertexInput.pVertexBindingDescriptions      = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vertexInput.pVertexAttributeDescriptions    = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{0.0f, 0.0f, static_cast<float>(shadowExtent.width),
                            static_cast<float>(shadowExtent.height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, shadowExtent};
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports    = &viewport;
        viewportState.scissorCount  = 1;
        viewportState.pScissors     = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth   = 1.0f;
        rasterizer.cullMode    = VK_CULL_MODE_NONE;   // double-sided cards cast from any sun angle
        rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable  = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo colorBlend{};   // depth-only: no color attachments
        colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 0;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = 2;
        pipelineInfo.pStages             = stages;
        pipelineInfo.pVertexInputState   = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState   = &multisample;
        pipelineInfo.pDepthStencilState  = &depthStencil;
        pipelineInfo.pColorBlendState    = &colorBlend;
        pipelineInfo.layout              = m_pipelineLayout;     // same set layout + push range
        pipelineInfo.renderPass          = shadowRenderPass;
        pipelineInfo.subpass             = 0;
        VkResult res = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                 nullptr, &m_shadowPipeline);
        vkDestroyShaderModule(m_device, vertModule, nullptr);
        vkDestroyShaderModule(m_device, fragModule, nullptr);
        if (res != VK_SUCCESS) {
            throw std::runtime_error("failed to create foliage shadow pipeline");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("FoliageRenderPipeline", "Shadow pipeline init failed ({}); foliage casts no shadows", e.what());
        m_shadowPipeline = VK_NULL_HANDLE;
        return false;
    }
    LOG_INFO("FoliageRenderPipeline", "Shadow caster initialized (dappled canopy shadows)");
    return true;
}


void FoliageRenderPipeline::renderShadow(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                                         const std::vector<ChunkDraw>& chunks) {
    if (!m_params.enabled || m_shadowPipeline == VK_NULL_HANDLE || chunks.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &uboSet, 0, nullptr);

    const uint32_t vertexCount = 6u * m_params.cardsPerVoxel;

    FoliagePush pc{};
    pc.cardSize      = m_params.cardSize;
    pc.windStrength  = m_params.windStrength;
    pc.radius        = m_params.radius;
    pc.cardsPerVoxel = m_params.cardsPerVoxel;

    for (const auto& c : chunks) {
        if (c.buffer == VK_NULL_HANDLE || c.count == 0) continue;
        pc.chunkBaseOffset = c.origin;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(FoliagePush), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &c.buffer, &offset);
        vkCmdDraw(cmd, vertexCount, c.count, 0, 0);
    }
}


void FoliageRenderPipeline::render(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                                   const std::vector<ChunkDraw>& chunks) {
    if (!m_params.enabled || m_pipeline == VK_NULL_HANDLE || chunks.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &uboSet, 0, nullptr);

    const uint32_t vertexCount = 6u * m_params.cardsPerVoxel;

    FoliagePush pc{};
    pc.cardSize      = m_params.cardSize;
    pc.windStrength  = m_params.windStrength;
    pc.radius        = m_params.radius;
    pc.cardsPerVoxel = m_params.cardsPerVoxel;

    for (const auto& c : chunks) {
        if (c.buffer == VK_NULL_HANDLE || c.count == 0) continue;
        pc.chunkBaseOffset = c.origin;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(FoliagePush), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &c.buffer, &offset);
        vkCmdDraw(cmd, vertexCount, c.count, 0, 0);
    }
}

} // namespace Graphics
} // namespace Phyxel
