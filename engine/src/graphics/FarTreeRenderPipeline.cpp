#include "graphics/DepthConvention.h"
#include "graphics/FarTreeRenderPipeline.h"
#include "graphics/FarTerrainTypes.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"

#include <array>
#include <fstream>
#include <stdexcept>

namespace Phyxel {
namespace Graphics {

// Push constant layout — MUST match far_tree.vert.
struct FarTreePush {
    glm::vec2 tileOriginRel;  // (tile min corner - camera).xz, double-subtracted (clip space)
    glm::vec2 tileOriginAbs;  // exact world-space min corner (hash-stable shading inputs)
    glm::vec4 fades;          // fadeNear0, fadeNear1, fadeFar0, fadeFar1 (world units)
    glm::vec2 handoff{0.0f};  // x: residency minFade (near fade floor), y: pad
};

static std::vector<char> readShaderFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("FarTreeRenderPipeline: cannot open shader: " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

FarTreeRenderPipeline::FarTreeRenderPipeline() {}

FarTreeRenderPipeline::~FarTreeRenderPipeline() { cleanup(); }

void FarTreeRenderPipeline::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    m_pipeline       = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
}

bool FarTreeRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                       VkRenderPass renderPass, VkExtent2D extent,
                                       VkDescriptorSetLayout uboDescriptorSetLayout) {
    m_device         = device;
    m_physicalDevice = physicalDevice;
    try {
        createPipeline(renderPass, extent, uboDescriptorSetLayout);
    } catch (const std::exception& e) {
        LOG_ERROR("FarTreeRenderPipeline", "Initialization failed: {}", e.what());
        return false;
    }
    LOG_INFO("FarTreeRenderPipeline", "Initialized (far-tree impostors)");
    return true;
}

void FarTreeRenderPipeline::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                           VkDescriptorSetLayout uboLayout) {
    auto vertCode = readShaderFile(Core::AssetManager::instance().resolveShader("far_tree.vert.spv"));
    auto fragCode = readShaderFile(Core::AssetManager::instance().resolveShader("far_tree.frag.spv"));

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

    // ONE binding, per-INSTANCE: the card's 6 corners come from gl_VertexIndex.
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(FarTreeInstance);
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 3> attrs{};
    attrs[0].binding  = 0;  // localX, worldY, localZ, height
    attrs[0].location = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[0].offset   = offsetof(FarTreeInstance, localX);
    attrs[1].binding  = 0;  // canopyR
    attrs[1].location = 1;
    attrs[1].format   = VK_FORMAT_R32_SFLOAT;
    attrs[1].offset   = offsetof(FarTreeInstance, canopyR);
    attrs[2].binding  = 0;  // packed shape class + tint
    attrs[2].location = 2;
    attrs[2].format   = VK_FORMAT_R32_UINT;
    attrs[2].offset   = offsetof(FarTreeInstance, packed);

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
    // DYNAMIC viewport/scissor. The extent captured above is only a creation-time default: this
    // pipeline is created ONCE and never re-created, so a baked viewport goes stale the instant the
    // window resizes and this pass then rasterises at the OLD size while the main chunk pipeline
    // (which was always dynamic) uses the new one. That is what made tree foliage detach from its
    // trunks after a resize. Both are now set once per render pass -- see PostProcessor's
    // begin*RenderPass -- and every pipeline drawn in that pass inherits them.
    // Shadow pipelines deliberately keep a STATIC viewport: they render into a fixed-size shadow
    // map, so the baked extent is correct there.
    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dynStates;

    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth   = 1.0f;
    rasterizer.cullMode    = VK_CULL_MODE_NONE;   // billboards are two-sided by definition
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Cutout, not blended: kept fragments write depth, so cards sort correctly against far
    // terrain, each other, and the near field — same choice grass and leaf cards made.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = Graphics::DepthConvention::sceneDepthCompareOp();

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttach;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(FarTreePush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &uboLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create FarTreeRenderPipeline layout");
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
    pipelineInfo.pDynamicState = &dynState;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create FarTreeRenderPipeline");
    }

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}

void FarTreeRenderPipeline::render(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                                   const std::vector<TreeDraw>& draws) {
    if (!m_params.enabled || m_pipeline == VK_NULL_HANDLE || draws.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &uboSet, 0, nullptr);

    for (const auto& d : draws) {
        if (d.instances == VK_NULL_HANDLE || d.count == 0) continue;
        FarTreePush pc{
            glm::vec2(glm::dvec2(d.origin) - glm::dvec2(m_cameraWorld.x, m_cameraWorld.z)),
            d.origin,
            glm::vec4(m_params.fadeNear0, m_params.fadeNear1, m_params.fadeFar0, m_params.fadeFar1),
            glm::vec2(d.minFade, 0.0f)};
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(FarTreePush), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &d.instances, &offset);
        vkCmdDraw(cmd, 6, d.count, 0, d.firstInstance);   // one quad per instance
    }
}

} // namespace Graphics
} // namespace Phyxel
