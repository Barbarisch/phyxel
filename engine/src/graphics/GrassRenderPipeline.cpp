#include "graphics/GrassRenderPipeline.h"
#include "core/AssetManager.h"
#include "core/Types.h"
#include "utils/Logger.h"

#include <fstream>
#include <stdexcept>
#include <array>

namespace Phyxel {
namespace Graphics {

// Push constant layout — MUST match grass.vert. Time + camera position come from the UBO;
// the wind-field scalars come from the shared WindSystem via Params::wind.
struct GrassPush {
    glm::vec3 chunkBaseOffset;   // CAMERA-RELATIVE chunk origin (docs/CameraRelativeRendering.md)
    float     bladeHeight;
    float     windStrength;
    float     radius;
    float     fadeRange;
    float     growDuration;
    uint32_t  bladesPerVoxel;
    float     windDirX;
    float     windDirZ;
    float     windBase;
    float     gustAmp;
    float     gustScale;
    float     gustSpeed;
    uint32_t  bladeStyle;
    // ABSOLUTE chunk origin, float-exact (integers < 2^24) — the hash/clump/wind-phase seeds
    // must NOT be camera-relative or blades re-roll as the camera moves. Scalar floats so the
    // std430 offsets append tightly after bladeStyle on both sides.
    float     absBaseX;
    float     absBaseY;
    float     absBaseZ;
};
static_assert(sizeof(GrassPush) == 76, "GrassPush must match the grass.vert push-constant block");

static std::vector<char> readShaderFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("GrassRenderPipeline: cannot open shader: " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

GrassRenderPipeline::GrassRenderPipeline() {}

GrassRenderPipeline::~GrassRenderPipeline() { cleanup(); }

void GrassRenderPipeline::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    m_pipeline       = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
}

bool GrassRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                     VkRenderPass renderPass, VkExtent2D extent,
                                     VkDescriptorSetLayout uboDescriptorSetLayout) {
    m_device         = device;
    m_physicalDevice = physicalDevice;
    try {
        createPipeline(renderPass, extent, uboDescriptorSetLayout);
    } catch (const std::exception& e) {
        LOG_ERROR("GrassRenderPipeline", "Initialization failed: {}", e.what());
        return false;
    }
    LOG_INFO("GrassRenderPipeline", "Initialized (grass blade layer)");
    return true;
}

void GrassRenderPipeline::recreatePipeline(VkRenderPass renderPass, VkExtent2D extent) {
    (void)renderPass; (void)extent;
    LOG_WARN("GrassRenderPipeline", "recreatePipeline called — use initialize() after resize");
}

void GrassRenderPipeline::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                         VkDescriptorSetLayout uboLayout) {
    auto vertCode = readShaderFile(Core::AssetManager::instance().resolveShader("grass.vert.spv"));
    auto fragCode = readShaderFile(Core::AssetManager::instance().resolveShader("grass.frag.spv"));

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

    // Instance-rate binding for GrassInstanceData (no vertex buffer — gl_VertexIndex builds blades).
    VkVertexInputBindingDescription binding = GrassInstanceData::getBindingDescription();
    auto attrs = GrassInstanceData::getAttributeDescriptions();

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
    rasterizer.cullMode    = VK_CULL_MODE_NONE;    // blades are double-sided
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
    pushRange.size       = sizeof(GrassPush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &uboLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create GrassRenderPipeline layout");
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
        throw std::runtime_error("failed to create GrassRenderPipeline");
    }

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}

void GrassRenderPipeline::render(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                                 const std::vector<ChunkDraw>& chunks) {
    if (!m_params.enabled || m_pipeline == VK_NULL_HANDLE || chunks.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &uboSet, 0, nullptr);

    const uint32_t vertsPerBlade = 6;   // both styles are 1 quad; bladeStyle only changes silhouette
    const uint32_t vertexCount   = vertsPerBlade * m_params.bladesPerVoxel;

    GrassPush pc{};
    pc.bladeHeight    = m_params.bladeHeight;
    pc.windStrength   = m_params.windStrength;
    pc.radius         = m_params.radius;
    pc.fadeRange      = m_params.fadeRange;
    pc.growDuration   = m_params.growDuration;
    pc.bladesPerVoxel = m_params.bladesPerVoxel;
    pc.windDirX       = m_params.wind.dir.x;
    pc.windDirZ       = m_params.wind.dir.y;
    pc.windBase       = m_params.wind.base;
    pc.gustAmp        = m_params.wind.gustAmp;
    pc.gustScale      = m_params.wind.gustScale;
    pc.gustSpeed      = m_params.wind.gustSpeed;
    pc.bladeStyle     = m_params.bladeStyle;

    for (const auto& c : chunks) {
        if (c.buffer == VK_NULL_HANDLE || c.count == 0) continue;
        // camera-relative position; exact ABSOLUTE origin for the hash seeds
        pc.chunkBaseOffset = glm::vec3(glm::dvec3(c.origin) - glm::dvec3(m_cameraWorld));
        pc.absBaseX = c.origin.x;
        pc.absBaseY = c.origin.y;
        pc.absBaseZ = c.origin.z;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPush), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &c.buffer, &offset);
        vkCmdDraw(cmd, vertexCount, c.count, 0, 0);
    }
}

} // namespace Graphics
} // namespace Phyxel
