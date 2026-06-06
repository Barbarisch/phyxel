#include "graphics/WaterCellRenderPipeline.h"
#include "graphics/Camera.h"
#include "core/AssetManager.h"
#include <array>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cstddef>

namespace Phyxel {
namespace Graphics {

// Per-cell mesh: a sloped top quad plus four vertical side "skirts" (one per edge), so
// the water reads as a solid body with closed faces at drops/cliffs. Each vertex is
// (offsetX, offsetZ, vtype, edge): vtype 0 = use the matching corner height, 1 = use the
// skirt bottom for `edge` (0=+x,1=-x,2=+z,3=-z). Cull is disabled (visible both sides).
static const std::array<glm::vec4, 30> BOX_VERTICES = {
    // Top face (vtype 0).
    glm::vec4(-0.5f, -0.5f, 0, 0), glm::vec4( 0.5f, -0.5f, 0, 0), glm::vec4( 0.5f,  0.5f, 0, 0),
    glm::vec4( 0.5f,  0.5f, 0, 0), glm::vec4(-0.5f,  0.5f, 0, 0), glm::vec4(-0.5f, -0.5f, 0, 0),
    // +x side (edge 0).
    glm::vec4( 0.5f, -0.5f, 0, 0), glm::vec4( 0.5f,  0.5f, 0, 0), glm::vec4( 0.5f,  0.5f, 1, 0),
    glm::vec4( 0.5f,  0.5f, 1, 0), glm::vec4( 0.5f, -0.5f, 1, 0), glm::vec4( 0.5f, -0.5f, 0, 0),
    // -x side (edge 1).
    glm::vec4(-0.5f, -0.5f, 0, 0), glm::vec4(-0.5f,  0.5f, 0, 0), glm::vec4(-0.5f,  0.5f, 1, 1),
    glm::vec4(-0.5f,  0.5f, 1, 1), glm::vec4(-0.5f, -0.5f, 1, 1), glm::vec4(-0.5f, -0.5f, 0, 0),
    // +z side (edge 2).
    glm::vec4(-0.5f,  0.5f, 0, 0), glm::vec4( 0.5f,  0.5f, 0, 0), glm::vec4( 0.5f,  0.5f, 1, 2),
    glm::vec4( 0.5f,  0.5f, 1, 2), glm::vec4(-0.5f,  0.5f, 1, 2), glm::vec4(-0.5f,  0.5f, 0, 0),
    // -z side (edge 3).
    glm::vec4(-0.5f, -0.5f, 0, 0), glm::vec4( 0.5f, -0.5f, 0, 0), glm::vec4( 0.5f, -0.5f, 1, 3),
    glm::vec4( 0.5f, -0.5f, 1, 3), glm::vec4(-0.5f, -0.5f, 1, 3), glm::vec4(-0.5f, -0.5f, 0, 0),
};

struct WaterCellPush {
    glm::mat4 viewProj;   // 64
    glm::vec4 camPosTime; // 16
};

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("failed to open file: " + filename);
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    throw std::runtime_error("failed to find suitable memory type!");
}

WaterCellRenderPipeline::WaterCellRenderPipeline() : m_startTime(std::chrono::high_resolution_clock::now()) {}
WaterCellRenderPipeline::~WaterCellRenderPipeline() { cleanup(); }

void WaterCellRenderPipeline::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_instanceBuffer, nullptr);
        vkFreeMemory(m_device, m_instanceBufferMemory, nullptr);
        vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    }
}

void WaterCellRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                         VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    createBuffers();
    createDescriptorSetLayout();
    createPipeline(renderPass, swapChainExtent);
}

void WaterCellRenderPipeline::createBuffers() {
    // Static per-cell mesh vertex buffer (top + 4 side skirts).
    VkDeviceSize vsize = sizeof(glm::vec4) * BOX_VERTICES.size();
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = vsize;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &m_vertexBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to create water-cell vertex buffer!");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(m_device, &ai, nullptr, &m_vertexBufferMemory) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate water-cell vertex memory!");
    vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexBufferMemory, 0);
    void* data;
    vkMapMemory(m_device, m_vertexBufferMemory, 0, vsize, 0, &data);
    memcpy(data, BOX_VERTICES.data(), (size_t)vsize);
    vkUnmapMemory(m_device, m_vertexBufferMemory);

    // Dynamic instance buffer (WaterSurfaceCell per cell: 2x vec4).
    VkDeviceSize isize = sizeof(Core::WaterSurfaceCell) * MAX_INSTANCES;
    bi.size = isize;
    if (vkCreateBuffer(m_device, &bi, nullptr, &m_instanceBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to create water-cell instance buffer!");
    vkGetBufferMemoryRequirements(m_device, m_instanceBuffer, &mr);
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(m_device, &ai, nullptr, &m_instanceBufferMemory) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate water-cell instance memory!");
    vkBindBufferMemory(m_device, m_instanceBuffer, m_instanceBufferMemory, 0);
}

void WaterCellRenderPipeline::createDescriptorSetLayout() {
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset = 0;
    pc.size = sizeof(WaterCellPush);

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 0;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create water-cell descriptor set layout!");

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &m_descriptorSetLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create water-cell pipeline layout!");
}

void WaterCellRenderPipeline::createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    auto vsCode = readFile(Core::AssetManager::instance().resolveShader("water_cell.vert.spv"));
    auto fsCode = readFile(Core::AssetManager::instance().resolveShader("water_cell.frag.spv"));

    VkShaderModule vs, fs;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = vsCode.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(vsCode.data());
    vkCreateShaderModule(m_device, &ci, nullptr, &vs);
    ci.codeSize = fsCode.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(fsCode.data());
    vkCreateShaderModule(m_device, &ci, nullptr, &fs);

    VkPipelineShaderStageCreateInfo vss{}, fss{};
    vss.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vss.stage = VK_SHADER_STAGE_VERTEX_BIT; vss.module = vs; vss.pName = "main";
    fss.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fss.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fss.module = fs; fss.pName = "main";
    VkPipelineShaderStageCreateInfo stages[] = {vss, fss};

    VkVertexInputBindingDescription binds[2];
    binds[0].binding = 0; binds[0].stride = sizeof(glm::vec4); binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    binds[1].binding = 1; binds[1].stride = sizeof(Core::WaterSurfaceCell); binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[4];
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};                                          // mesh vert (offX,offZ,vtype,edge)
    attrs[1] = {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Core::WaterSurfaceCell, centerDepth)};
    attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Core::WaterSurfaceCell, corners)};
    attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Core::WaterSurfaceCell, skirt)};

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = binds;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{}; vp.width = (float)swapChainExtent.width; vp.height = (float)swapChainExtent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    VkRect2D sc{}; sc.extent = swapChainExtent;
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_FALSE; ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2; gpi.pStages = stages;
    gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vps; gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms; gpi.pDepthStencilState = &ds; gpi.pColorBlendState = &cb;
    gpi.layout = m_pipelineLayout; gpi.renderPass = renderPass; gpi.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpi, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create water-cell graphics pipeline!");

    vkDestroyShaderModule(m_device, vs, nullptr);
    vkDestroyShaderModule(m_device, fs, nullptr);
}

void WaterCellRenderPipeline::render(VkCommandBuffer commandBuffer, const Camera& camera,
                                     const glm::mat4& projectionMatrix, const std::vector<Core::WaterSurfaceCell>& cells) {
    if (cells.empty()) return;
    uint32_t count = static_cast<uint32_t>(std::min(cells.size(), MAX_INSTANCES));

    const VkDeviceSize bytes = sizeof(Core::WaterSurfaceCell) * count;
    void* data;
    vkMapMemory(m_device, m_instanceBufferMemory, 0, bytes, 0, &data);
    memcpy(data, cells.data(), bytes);
    vkUnmapMemory(m_device, m_instanceBufferMemory);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    VkBuffer buffers[] = {m_vertexBuffer, m_instanceBuffer};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);

    WaterCellPush pc{};
    pc.viewProj = projectionMatrix * camera.getViewMatrix();
    float t = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_startTime).count();
    pc.camPosTime = glm::vec4(camera.getPosition(), t);
    vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(WaterCellPush), &pc);

    vkCmdDraw(commandBuffer, static_cast<uint32_t>(BOX_VERTICES.size()), count, 0, 0);
}

void WaterCellRenderPipeline::recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    createPipeline(renderPass, swapChainExtent);
}

} // namespace Graphics
} // namespace Phyxel
