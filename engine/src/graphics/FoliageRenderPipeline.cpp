#include "graphics/DepthConvention.h"
#include "graphics/FoliageRenderPipeline.h"
#include "core/AssetManager.h"
#include "core/Types.h"
#include "utils/Logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <stdexcept>
#include <array>
#include <algorithm>

namespace Phyxel {
namespace Graphics {

// Push constant layout — MUST match foliage.vert AND foliage_shadow.vert. Time + camera come
// from the UBO; the wind-field scalars come from the shared WindSystem via Params::wind.
struct FoliagePush {
    glm::vec3 chunkBaseOffset;   // CAMERA-RELATIVE chunk origin (docs/CameraRelativeRendering.md)
    float     cardSize;
    float     windStrength;
    float     radius;
    uint32_t  cardsPerVoxel;
    float     windDirX;
    float     windDirZ;
    float     windBase;
    float     gustAmp;
    float     gustScale;
    float     gustSpeed;
    // ABSOLUTE chunk origin, float-exact — hash/wind-phase seeds must not be camera-relative.
    float     absBaseX;
    float     absBaseY;
    float     absBaseZ;
};
static_assert(sizeof(FoliagePush) == 64, "FoliagePush must match the foliage.vert push-constant block");

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
    if (m_kinPipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_kinPipeline, nullptr);
    if (m_kinPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_kinPipelineLayout, nullptr);
    if (m_kinBuffer         != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_kinBuffer, nullptr);
    if (m_kinBufferMemory   != VK_NULL_HANDLE) vkFreeMemory(m_device, m_kinBufferMemory, nullptr);
    m_pipeline       = VK_NULL_HANDLE;
    m_shadowPipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_kinPipeline       = VK_NULL_HANDLE;
    m_kinPipelineLayout = VK_NULL_HANDLE;
    m_kinBuffer         = VK_NULL_HANDLE;
    m_kinBufferMemory   = VK_NULL_HANDLE;
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
    depthStencil.depthCompareOp   = Graphics::DepthConvention::sceneDepthCompareOp();

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
        // The SHADOW pass is FORWARD-Z (ShadowMap clears depth to 1.0 and compares LESS) even
        // though the SCENE pass is reverse-Z. This used to call sceneDepthCompareOp() —
        // GREATER — against a 1.0 clear, so no foliage fragment could ever pass the depth
        // test and canopies silently wrote NOTHING into the shadow map. Must match
        // ShadowMap::createPipeline, not the scene convention.
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
    pc.windDirX      = m_params.wind.dir.x;
    pc.windDirZ      = m_params.wind.dir.y;
    pc.windBase      = m_params.wind.base;
    pc.gustAmp       = m_params.wind.gustAmp;
    pc.gustScale     = m_params.wind.gustScale;
    pc.gustSpeed     = m_params.wind.gustSpeed;

    for (const auto& c : chunks) {
        if (c.buffer == VK_NULL_HANDLE || c.count == 0) continue;
        // camera-relative position; exact ABSOLUTE origin for the hash seeds
        pc.chunkBaseOffset = glm::vec3(glm::dvec3(c.origin) - glm::dvec3(m_cameraWorld));
        pc.absBaseX = c.origin.x;
        pc.absBaseY = c.origin.y;
        pc.absBaseZ = c.origin.z;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(FoliagePush), &pc);
        VkDeviceSize offset = c.bindOffset;  // 4.3 A2: arena span offset (0 legacy)
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
    pc.windDirX      = m_params.wind.dir.x;
    pc.windDirZ      = m_params.wind.dir.y;
    pc.windBase      = m_params.wind.base;
    pc.gustAmp       = m_params.wind.gustAmp;
    pc.gustScale     = m_params.wind.gustScale;
    pc.gustSpeed     = m_params.wind.gustSpeed;

    for (const auto& c : chunks) {
        if (c.buffer == VK_NULL_HANDLE || c.count == 0) continue;
        // camera-relative position; exact ABSOLUTE origin for the hash seeds
        pc.chunkBaseOffset = glm::vec3(glm::dvec3(c.origin) - glm::dvec3(m_cameraWorld));
        pc.absBaseX = c.origin.x;
        pc.absBaseY = c.origin.y;
        pc.absBaseZ = c.origin.z;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(FoliagePush), &pc);
        VkDeviceSize offset = c.bindOffset;  // 4.3 A2: arena span offset (0 legacy)
        vkCmdBindVertexBuffers(cmd, 0, 1, &c.buffer, &offset);
        vkCmdDraw(cmd, vertexCount, c.count, 0, 0);
    }
}

// ============================================================================
// KINEMATIC foliage (F3): leaf cards on moving coherent fragments
// ============================================================================

// Push layout — MUST match foliage_kinematic.vert.
struct KinFoliagePush {
    glm::mat4 model;
    float     cardSize;
    float     radius;
    uint32_t  cardsPerVoxel;
};
static_assert(sizeof(KinFoliagePush) == 76, "KinFoliagePush must match foliage_kinematic.vert");

uint32_t FoliageRenderPipeline::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    throw std::runtime_error("FoliageRenderPipeline: no suitable memory type");
}

bool FoliageRenderPipeline::initializeKinematic(VkRenderPass renderPass, VkExtent2D extent,
                                                VkDescriptorSetLayout uboDescriptorSetLayout) {
    if (m_device == VK_NULL_HANDLE) {
        LOG_ERROR("FoliageRenderPipeline", "initializeKinematic called before initialize()");
        return false;
    }
    try {
        auto vertCode = readShaderFile(Core::AssetManager::instance().resolveShader("foliage_kinematic.vert.spv"));
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
        pushRange.size       = sizeof(KinFoliagePush);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount         = 1;
        layoutInfo.pSetLayouts            = &uboDescriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &pushRange;
        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_kinPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create kinematic foliage layout");
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
        pipelineInfo.layout              = m_kinPipelineLayout;
        pipelineInfo.renderPass          = renderPass;
        pipelineInfo.subpass             = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_kinPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create kinematic foliage pipeline");
        }

        vkDestroyShaderModule(m_device, vertModule, nullptr);
        vkDestroyShaderModule(m_device, fragModule, nullptr);

        // Shared host-visible instance buffer (persistently mapped is unnecessary —
        // rebuilds are rare: only when fragments spawn/despawn).
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size  = MAX_KINEMATIC_FOLIAGE * sizeof(FoliageInstanceData);
        bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bufInfo, nullptr, &m_kinBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create kinematic foliage buffer");
        }
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_device, m_kinBuffer, &memReq);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = memReq.size;
        alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_device, &alloc, nullptr, &m_kinBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate kinematic foliage memory");
        }
        vkBindBufferMemory(m_device, m_kinBuffer, m_kinBufferMemory, 0);
    } catch (const std::exception& e) {
        LOG_ERROR("FoliageRenderPipeline", "initializeKinematic failed: {}", e.what());
        return false;
    }
    LOG_INFO("FoliageRenderPipeline", "Kinematic foliage initialized (falling canopies keep cards)");
    return true;
}

void FoliageRenderPipeline::rebuildKinematicBuffer(
    const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects) {
    if (m_kinBuffer == VK_NULL_HANDLE) return;
    m_kinRanges.clear();

    void* mapped = nullptr;
    if (vkMapMemory(m_device, m_kinBufferMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) return;
    auto* dst = static_cast<FoliageInstanceData*>(mapped);
    uint32_t cursor = 0;
    for (const auto& [id, obj] : objects) {
        if (obj.foliage.empty()) continue;
        uint32_t n = static_cast<uint32_t>(obj.foliage.size());
        if (cursor + n > MAX_KINEMATIC_FOLIAGE) {
            LOG_WARN_FMT("FoliageRenderPipeline", "kinematic foliage cap hit; '" << id
                         << "' and later objects render without cards this rebuild");
            break;
        }
        std::copy(obj.foliage.begin(), obj.foliage.end(), dst + cursor);
        m_kinRanges[id] = { cursor, n };
        cursor += n;
    }
    vkUnmapMemory(m_device, m_kinBufferMemory);
}

void FoliageRenderPipeline::renderKinematic(
    VkCommandBuffer cmd, VkDescriptorSet uboSet,
    const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects) {
    if (!m_params.enabled || m_kinPipeline == VK_NULL_HANDLE || m_kinRanges.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_kinPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_kinPipelineLayout, 0, 1, &uboSet, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_kinBuffer, &offset);

    const uint32_t vertexCount = 6u * m_params.cardsPerVoxel;
    KinFoliagePush pc{};
    pc.cardSize      = m_params.cardSize;
    pc.radius        = m_params.radius;
    pc.cardsPerVoxel = m_params.cardsPerVoxel;

    for (const auto& [id, range] : m_kinRanges) {
        auto it = objects.find(id);
        if (it == objects.end() || !it->second.visible || range.count == 0) continue;
        pc.model = it->second.currentTransform *
                   glm::translate(glm::mat4(1.0f), it->second.foliageOrigin);
        // Camera-relative rendering (docs/CameraRelativeRendering.md, merge integration):
        // the view matrix is rotation-only with the eye at the origin, so the model's
        // world translation must become camera-relative. Same pattern as
        // KinematicVoxelPipeline; hash seeds are fragment-LOCAL by design, unaffected.
        pc.model[3] = glm::vec4(glm::vec3(pc.model[3]) - m_cameraWorld, pc.model[3].w);
        vkCmdPushConstants(cmd, m_kinPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(KinFoliagePush), &pc);
        vkCmdDraw(cmd, vertexCount, range.count, 0, range.first);
    }
}

} // namespace Graphics
} // namespace Phyxel
