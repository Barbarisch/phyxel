#include "graphics/KinematicVoxelPipeline.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"

#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <stdexcept>
#include <array>

namespace Phyxel {
namespace Graphics {

// ============================================================================
// File helpers (local)
// ============================================================================

static std::vector<char> readShaderFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("KinematicVoxelPipeline: cannot open shader: " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

// ============================================================================
// Construction / destruction
// ============================================================================

KinematicVoxelPipeline::KinematicVoxelPipeline() {}

KinematicVoxelPipeline::~KinematicVoxelPipeline() {
    cleanup();
}

void KinematicVoxelPipeline::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_instanceBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_instanceBuffer, nullptr);
    if (m_instanceBufferMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_instanceBufferMemory, nullptr);

    m_pipeline             = VK_NULL_HANDLE;
    m_pipelineLayout       = VK_NULL_HANDLE;
    m_instanceBuffer       = VK_NULL_HANDLE;
    m_instanceBufferMemory = VK_NULL_HANDLE;
    m_device               = VK_NULL_HANDLE;
}

// ============================================================================
// Initialization
// ============================================================================

bool KinematicVoxelPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                         VkRenderPass renderPass, VkExtent2D extent,
                                         VkDescriptorSetLayout uboDescriptorSetLayout,
                                         VkDescriptorSet /*uboDescriptorSet*/)
{
    m_device         = device;
    m_physicalDevice = physicalDevice;

    try {
        createInstanceBuffer();
        createPipeline(renderPass, extent, uboDescriptorSetLayout);
    } catch (const std::exception& e) {
        LOG_ERROR("KinematicVoxelPipeline", "Initialization failed: {}", e.what());
        return false;
    }

    LOG_INFO("KinematicVoxelPipeline", "Initialized (max {} face instances)", MAX_TOTAL_FACES);
    return true;
}

void KinematicVoxelPipeline::recreatePipeline(VkRenderPass renderPass, VkExtent2D extent) {
    // Can't recreate without the descriptor set layout — caller must call initialize() again
    // after a swapchain resize. This method is kept for interface symmetry with other pipelines.
    (void)renderPass;
    (void)extent;
    LOG_WARN("KinematicVoxelPipeline", "recreatePipeline called — use initialize() after resize");
}

// ============================================================================
// Buffer management
// ============================================================================

void KinematicVoxelPipeline::createInstanceBuffer() {
    VkDeviceSize bufferSize = sizeof(Core::KinematicFaceData) * MAX_TOTAL_FACES;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = bufferSize;
    bufferInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_instanceBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create kinematic voxel instance buffer");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, m_instanceBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_instanceBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate kinematic voxel instance buffer memory");
    }

    vkBindBufferMemory(m_device, m_instanceBuffer, m_instanceBufferMemory, 0);
}

void KinematicVoxelPipeline::rebuildBuffer(
    const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects)
{
    m_objectRanges.clear();
    m_totalFaces = 0;

    // Collect all face data, recording each object's range in the buffer
    std::vector<Core::KinematicFaceData> allFaces;
    allFaces.reserve(MAX_TOTAL_FACES);

    for (const auto& [id, obj] : objects) {
        if (!obj.visible || obj.faces.empty()) continue;

        uint32_t startFace = static_cast<uint32_t>(allFaces.size());
        uint32_t faceCount = static_cast<uint32_t>(obj.faces.size());

        if (startFace + faceCount > MAX_TOTAL_FACES) {
            LOG_WARN_FMT("KinematicVoxelPipeline",
                "Face buffer full — object '" << id << "' skipped ("
                << startFace + faceCount << " > " << MAX_TOTAL_FACES << ")");
            break;
        }

        allFaces.insert(allFaces.end(), obj.faces.begin(), obj.faces.end());
        m_objectRanges[id] = {startFace, faceCount};
    }

    m_totalFaces = static_cast<uint32_t>(allFaces.size());
    if (m_totalFaces == 0) return;

    // Upload to GPU
    void* data;
    vkMapMemory(m_device, m_instanceBufferMemory, 0,
                m_totalFaces * sizeof(Core::KinematicFaceData), 0, &data);
    memcpy(data, allFaces.data(), m_totalFaces * sizeof(Core::KinematicFaceData));
    vkUnmapMemory(m_device, m_instanceBufferMemory);

    LOG_INFO_FMT("KinematicVoxelPipeline",
        "Buffer rebuilt: " << m_totalFaces << " faces across " << m_objectRanges.size() << " objects");
}

// ============================================================================
// Rendering
// ============================================================================

void KinematicVoxelPipeline::render(
    VkCommandBuffer cmd,
    const std::unordered_map<std::string, Core::KinematicVoxelObject>& objects,
    VkDescriptorSet uboSet)
{
    if (m_totalFaces == 0 || m_objectRanges.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Bind the per-frame UBO/atlas/shadow/lights descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &uboSet, 0, nullptr);

    for (const auto& [id, range] : m_objectRanges) {
        auto it = objects.find(id);
        if (it == objects.end() || !it->second.visible) continue;

        // Push the object's current world transform (pivot rotation baked in)
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(glm::mat4),
                           glm::value_ptr(it->second.currentTransform));

        // Bind this object's slice of the shared instance buffer
        VkDeviceSize offset = range.startFace * sizeof(Core::KinematicFaceData);
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_instanceBuffer, &offset);

        // 6 vertices per face instance (procedural quad), range.faceCount instances
        vkCmdDraw(cmd, 6, range.faceCount, 0, 0);
    }
}

// ============================================================================
// Pipeline creation
// ============================================================================

void KinematicVoxelPipeline::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                              VkDescriptorSetLayout uboLayout)
{
    auto vertCode = readShaderFile(
        Core::AssetManager::instance().resolveShader("kinematic_voxel.vert.spv"));
    auto fragCode = readShaderFile(
        Core::AssetManager::instance().resolveShader("voxel.frag.spv"));

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

    // One instance-rate binding (no separate vertex buffer — gl_VertexIndex handles vertices)
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Core::KinematicFaceData);
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    // Attribute layout must match kinematic_voxel.vert locations exactly
    std::array<VkVertexInputAttributeDescription, 5> attrs{};
    // loc 0: localPosition  (vec3, 12 bytes at offset 0)
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Core::KinematicFaceData, localPosition)};
    // loc 1: scale          (vec3, 12 bytes at offset 12)
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Core::KinematicFaceData, scale)};
    // loc 2: uvOffset       (vec2, 8 bytes at offset 24)
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,        offsetof(Core::KinematicFaceData, uvOffset)};
    // loc 3: textureIndex   (uint, 4 bytes at offset 32)
    attrs[3] = {3, 0, VK_FORMAT_R32_UINT,             offsetof(Core::KinematicFaceData, textureIndex)};
    // loc 4: faceId         (uint, 4 bytes at offset 36)
    attrs[4] = {4, 0, VK_FORMAT_R32_UINT,             offsetof(Core::KinematicFaceData, faceId)};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions    = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(extent.width);
    viewport.height   = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

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
    // Match dynamic_voxel convention: CW triangles survive
    rasterizer.cullMode    = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttach;

    // Push constant: mat4 model matrix (64 bytes)
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &uboLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create KinematicVoxelPipeline layout");
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

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline)
            != VK_SUCCESS) {
        throw std::runtime_error("failed to create KinematicVoxelPipeline");
    }

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}

// ============================================================================
// Memory helper
// ============================================================================

uint32_t KinematicVoxelPipeline::findMemoryType(uint32_t typeBits,
                                                  VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    throw std::runtime_error("KinematicVoxelPipeline: no suitable memory type");
}

} // namespace Graphics
} // namespace Phyxel
