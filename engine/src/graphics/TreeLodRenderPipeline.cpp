#include "graphics/DepthConvention.h"
#include "graphics/TreeLodRenderPipeline.h"
#include "graphics/FarTerrainTypes.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"

#include <array>
#include <fstream>
#include <stdexcept>

namespace Phyxel {
namespace Graphics {

// Push constant layout — MUST match far_tree_mesh.vert.
struct TreeMeshPush {
    glm::vec2 tileOriginRel;
    glm::vec2 tileOriginAbs;
    glm::vec2 fadeIn;
    float     baseHeight;
    float     minFade = 0.0f;   ///< residency handoff floor (1 = stay fully solid)
    glm::vec2 levelBand{0.0f, 3.0e8f};   ///< distance window this draw's level owns
};

static std::vector<char> readShaderFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("TreeLodRenderPipeline: cannot open shader: " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

TreeLodRenderPipeline::TreeLodRenderPipeline() {}
TreeLodRenderPipeline::~TreeLodRenderPipeline() { cleanup(); }

void TreeLodRenderPipeline::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_shadowPipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
    if (m_shadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_shadowPipelineLayout, nullptr);
    m_pipeline       = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_shadowPipeline       = VK_NULL_HANDLE;
    m_shadowPipelineLayout = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
}

bool TreeLodRenderPipeline::initializeShadow(VkRenderPass shadowRenderPass,
                                             VkExtent2D shadowExtent,
                                             VkDescriptorSetLayout uboLayout) {
    if (m_device == VK_NULL_HANDLE) return false;
    try {
        auto vertCode = readShaderFile(
            Core::AssetManager::instance().resolveShader("far_tree_mesh_shadow.vert.spv"));
        VkShaderModule vertModule;
        VkShaderModuleCreateInfo smInfo{};
        smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smInfo.codeSize = vertCode.size();
        smInfo.pCode    = reinterpret_cast<const uint32_t*>(vertCode.data());
        vkCreateShaderModule(m_device, &smInfo, nullptr, &vertModule);

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stage.module = vertModule;
        stage.pName  = "main";

        std::array<VkVertexInputBindingDescription, 2> bindings{};
        bindings[0].binding   = 0;
        bindings[0].stride    = sizeof(FarVertex);
        bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindings[1].binding   = 1;
        bindings[1].stride    = sizeof(FarTreeInstance);
        bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        std::array<VkVertexInputAttributeDescription, 5> attrs{};
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(FarVertex, pos)};
        attrs[1] = {1, 0, VK_FORMAT_R32_UINT, offsetof(FarVertex, packed)};
        attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(FarTreeInstance, localX)};
        attrs[3] = {3, 1, VK_FORMAT_R32_SFLOAT, offsetof(FarTreeInstance, canopyR)};
        attrs[4] = {4, 1, VK_FORMAT_R32_UINT, offsetof(FarTreeInstance, packed)};
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount   = uint32_t(bindings.size());
        vertexInput.pVertexBindingDescriptions      = bindings.data();
        vertexInput.vertexAttributeDescriptionCount = uint32_t(attrs.size());
        vertexInput.pVertexAttributeDescriptions    = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{0.0f, 0.0f, float(shadowExtent.width), float(shadowExtent.height),
                            0.0f, 1.0f};
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
        rasterizer.cullMode    = VK_CULL_MODE_NONE;
        rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_TRUE;   // acne control, same tuning as ShadowMap
        rasterizer.depthBiasConstantFactor = 1.25f;
        rasterizer.depthBiasSlopeFactor = 1.75f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // ⚠️ Shadow passes are FORWARD-Z (clear 1.0): compare must be LESS, never
        // DepthConvention::sceneDepthCompareOp() — the exact trap that silenced foliage
        // shadows for a day (project memory).
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable  = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 0;   // depth-only render pass

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset     = 0;
        pushRange.size       = sizeof(TreeMeshPush);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount         = 1;
        layoutInfo.pSetLayouts            = &uboLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &pushRange;
        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("TreeLod shadow pipeline layout");

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = 1;    // vertex only — depth pass needs no fragments
        pipelineInfo.pStages             = &stage;
        pipelineInfo.pVertexInputState   = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState   = &multisample;
        pipelineInfo.pDepthStencilState  = &depthStencil;
        pipelineInfo.pColorBlendState    = &colorBlend;
        pipelineInfo.layout              = m_shadowPipelineLayout;
        pipelineInfo.renderPass          = shadowRenderPass;
        pipelineInfo.subpass             = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                      &m_shadowPipeline) != VK_SUCCESS)
            throw std::runtime_error("TreeLod shadow pipeline");
        vkDestroyShaderModule(m_device, vertModule, nullptr);
    } catch (const std::exception& e) {
        LOG_ERROR("TreeLodRenderPipeline", "Shadow variant init failed: {}", e.what());
        return false;
    }
    LOG_INFO("TreeLodRenderPipeline", "Far-cascade shadow caster variant initialized");
    return true;
}

void TreeLodRenderPipeline::renderShadow(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                                         const std::vector<MeshDraw>& draws) {
    if (!m_params.enabled || m_shadowPipeline == VK_NULL_HANDLE || draws.empty()) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipelineLayout, 0, 1,
                            &uboSet, 0, nullptr);
    for (const auto& d : draws) {
        if (d.vertexBuffer == VK_NULL_HANDLE || d.instances == VK_NULL_HANDLE ||
            d.indexCount == 0 || d.instanceCount == 0) continue;
        TreeMeshPush pc{
            glm::vec2(glm::dvec2(d.origin) - glm::dvec2(m_cameraWorld.x, m_cameraWorld.z)),
            d.origin,
            glm::vec2(m_params.fadeNear0, m_params.fadeNear1),
            d.baseHeight, d.minFade, d.levelBand};
        vkCmdPushConstants(cmd, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(TreeMeshPush), &pc);
        VkBuffer bufs[2] = {d.vertexBuffer, d.instances};
        VkDeviceSize offs[2] = {0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
        vkCmdBindIndexBuffer(cmd, d.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, d.indexCount, d.instanceCount, 0, 0, d.firstInstance);
    }
}

bool TreeLodRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                       VkRenderPass renderPass, VkExtent2D extent,
                                       VkDescriptorSetLayout uboDescriptorSetLayout) {
    m_device         = device;
    m_physicalDevice = physicalDevice;
    try {
        createPipeline(renderPass, extent, uboDescriptorSetLayout);
    } catch (const std::exception& e) {
        LOG_ERROR("TreeLodRenderPipeline", "Initialization failed: {}", e.what());
        return false;
    }
    LOG_INFO("TreeLodRenderPipeline", "Initialized (instanced far-tree LOD meshes)");
    return true;
}

void TreeLodRenderPipeline::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                           VkDescriptorSetLayout uboLayout) {
    auto vertCode = readShaderFile(Core::AssetManager::instance().resolveShader("far_tree_mesh.vert.spv"));
    // far_tree_mesh.frag = far_terrain.frag (identical atlas sampling + lighting, so far
    // trees stay pixel-compatible with the terrain) + the screen-door dither fade.
    auto fragCode = readShaderFile(Core::AssetManager::instance().resolveShader("far_tree_mesh.frag.spv"));

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

    // Binding 0: per-vertex mesh (FarVertex). Binding 1: per-instance (FarTreeInstance).
    std::array<VkVertexInputBindingDescription, 2> bindings{};
    bindings[0].binding   = 0;
    bindings[0].stride    = sizeof(FarVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding   = 1;
    bindings[1].stride    = sizeof(FarTreeInstance);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 5> attrs{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(FarVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32_UINT, offsetof(FarVertex, packed)};
    attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(FarTreeInstance, localX)};
    attrs[3] = {3, 1, VK_FORMAT_R32_SFLOAT, offsetof(FarTreeInstance, canopyR)};
    attrs[4] = {4, 1, VK_FORMAT_R32_UINT, offsetof(FarTreeInstance, packed)};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions      = bindings.data();
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
    rasterizer.cullMode    = VK_CULL_MODE_NONE;   // winding-convention risk not worth the win here
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = Graphics::DepthConvention::sceneDepthCompareOp();

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_FALSE;   // opaque voxel cells

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttach;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(TreeMeshPush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &uboLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create TreeLodRenderPipeline layout");
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
        throw std::runtime_error("failed to create TreeLodRenderPipeline");
    }

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}

void TreeLodRenderPipeline::render(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                                   const std::vector<MeshDraw>& draws) {
    if (!m_params.enabled || m_pipeline == VK_NULL_HANDLE || draws.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &uboSet, 0, nullptr);

    for (const auto& d : draws) {
        if (d.vertexBuffer == VK_NULL_HANDLE || d.instances == VK_NULL_HANDLE ||
            d.indexCount == 0 || d.instanceCount == 0) continue;
        TreeMeshPush pc{
            glm::vec2(glm::dvec2(d.origin) - glm::dvec2(m_cameraWorld.x, m_cameraWorld.z)),
            d.origin,
            glm::vec2(m_params.fadeNear0, m_params.fadeNear1),
            d.baseHeight, d.minFade, d.levelBand};
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TreeMeshPush), &pc);
        VkBuffer bufs[2] = {d.vertexBuffer, d.instances};
        VkDeviceSize offs[2] = {0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
        vkCmdBindIndexBuffer(cmd, d.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, d.indexCount, d.instanceCount, 0, 0, d.firstInstance);
    }
}

} // namespace Graphics
} // namespace Phyxel
