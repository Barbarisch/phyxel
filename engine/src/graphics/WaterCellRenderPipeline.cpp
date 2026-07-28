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
// (offsetX, offsetZ, vtype, edge): vtype 0 = top-face corner height; 2 = side-face top
// (corner height); 1 = side-face bottom (skirt[edge]). Side faces (vtype 1/2) are nudged
// slightly outward along `edge` (0=+x,1=-x,2=+z,3=-z) to avoid z-fighting the cliff/terrain
// behind a falling-water curtain. Cull is disabled (visible both sides).
static const std::array<glm::vec4, 30> BOX_VERTICES = {
    // Top face (vtype 0).
    glm::vec4(-0.5f, -0.5f, 0, 0), glm::vec4( 0.5f, -0.5f, 0, 0), glm::vec4( 0.5f,  0.5f, 0, 0),
    glm::vec4( 0.5f,  0.5f, 0, 0), glm::vec4(-0.5f,  0.5f, 0, 0), glm::vec4(-0.5f, -0.5f, 0, 0),
    // +x side (edge 0): tops vtype 2, bottoms vtype 1.
    glm::vec4( 0.5f, -0.5f, 2, 0), glm::vec4( 0.5f,  0.5f, 2, 0), glm::vec4( 0.5f,  0.5f, 1, 0),
    glm::vec4( 0.5f,  0.5f, 1, 0), glm::vec4( 0.5f, -0.5f, 1, 0), glm::vec4( 0.5f, -0.5f, 2, 0),
    // -x side (edge 1).
    glm::vec4(-0.5f, -0.5f, 2, 1), glm::vec4(-0.5f,  0.5f, 2, 1), glm::vec4(-0.5f,  0.5f, 1, 1),
    glm::vec4(-0.5f,  0.5f, 1, 1), glm::vec4(-0.5f, -0.5f, 1, 1), glm::vec4(-0.5f, -0.5f, 2, 1),
    // +z side (edge 2).
    glm::vec4(-0.5f,  0.5f, 2, 2), glm::vec4( 0.5f,  0.5f, 2, 2), glm::vec4( 0.5f,  0.5f, 1, 2),
    glm::vec4( 0.5f,  0.5f, 1, 2), glm::vec4(-0.5f,  0.5f, 1, 2), glm::vec4(-0.5f,  0.5f, 2, 2),
    // -z side (edge 3).
    glm::vec4(-0.5f, -0.5f, 2, 3), glm::vec4( 0.5f, -0.5f, 2, 3), glm::vec4( 0.5f, -0.5f, 1, 3),
    glm::vec4( 0.5f, -0.5f, 1, 3), glm::vec4(-0.5f, -0.5f, 1, 3), glm::vec4(-0.5f, -0.5f, 2, 3),
};

// 96 bytes — under the 128-byte guaranteed minimum. Must match the block declared in BOTH
// water_cell.vert and water_cell.frag. Sun/ambient are NOT here: they come from the shared scene
// UBO at set 0 (WaterSystemV3 Phase 1), which also keeps this under the push-constant limit.
struct WaterCellPush {
    glm::mat4 viewProj;   // 64
    glm::vec4 camPosTime; // 16
    glm::vec4 screen;     // 16  (screen width, height, 0, 0) — screen-space refraction/depth taps
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
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    }
}

void WaterCellRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                         VkRenderPass renderPass, VkExtent2D swapChainExtent,
                                         VkDescriptorSetLayout uboLayout) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    createBuffers();
    createDescriptorSetLayout(uboLayout);
    createDescriptorPool();
    createPipeline(renderPass, swapChainExtent);
}

void WaterCellRenderPipeline::setSceneTextures(VkImageView refractionView, VkSampler refractionSampler,
                                               VkImageView sceneDepthView, VkSampler sceneDepthSampler) {
    if (m_descriptorSet == VK_NULL_HANDLE) return;
    if (refractionView == VK_NULL_HANDLE || sceneDepthView == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo refr{ refractionSampler, refractionView,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    // The depth image is bound as a READ-ONLY depth attachment in the water pass, so this is the
    // layout it is in while we sample it.
    VkDescriptorImageInfo dep{ sceneDepthSampler, sceneDepthView,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

    std::array<VkWriteDescriptorSet, 2> w{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = m_descriptorSet; w[0].dstBinding = 0; w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &refr;
    w[1] = w[0]; w[1].dstBinding = 1; w[1].pImageInfo = &dep;
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    m_texturesBound = true;
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

void WaterCellRenderPipeline::createDescriptorSetLayout(VkDescriptorSetLayout uboLayout) {
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset = 0;
    pc.size = sizeof(WaterCellPush);

    // Set 1: the post-scene taps. 0 = half-res scene colour (refraction), 1 = scene depth
    // (thickness → absorption + soft shoreline). See docs/WaterSystemV3.md Phase 1.
    std::array<VkDescriptorSetLayoutBinding, 2> binds{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1] = binds[0]; binds[1].binding = 1;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(binds.size());
    li.pBindings = binds.data();
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create water-cell descriptor set layout!");

    // Set 0 = the shared scene UBO (sun/ambient/view/proj), set 1 = ours.
    std::array<VkDescriptorSetLayout, 2> sets = { uboLayout, m_descriptorSetLayout };
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = static_cast<uint32_t>(sets.size());
    pli.pSetLayouts = sets.data();
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create water-cell pipeline layout!");
}

void WaterCellRenderPipeline::createDescriptorPool() {
    VkDescriptorPoolSize size{};
    size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    size.descriptorCount = 2;

    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1; pi.pPoolSizes = &size; pi.maxSets = 1;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("failed to create water-cell descriptor pool!");

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descriptorPool;
    ai.descriptorSetCount = 1; ai.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &ai, &m_descriptorSet) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate water-cell descriptor set!");
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

    VkVertexInputAttributeDescription attrs[5];
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};                                          // mesh vert (offX,offZ,vtype,edge)
    attrs[1] = {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Core::WaterSurfaceCell, centerDepth)};
    attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Core::WaterSurfaceCell, corners)};
    attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Core::WaterSurfaceCell, skirt)};
    attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Core::WaterSurfaceCell, flow)};     // Phase 3

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = binds;
    vi.vertexAttributeDescriptionCount = 5;
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

void WaterCellRenderPipeline::render(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet,
                                     const Camera& camera, const glm::mat4& projectionMatrix,
                                     const std::vector<Core::WaterSurfaceCell>& cells,
                                     VkExtent2D screenExtent) {
    if (cells.empty()) return;
    // The fragment shader unconditionally samples set 1; drawing before setSceneTextures() has
    // pointed it at real images would read undefined descriptors.
    if (!m_texturesBound || uboSet == VK_NULL_HANDLE) return;
    uint32_t count = static_cast<uint32_t>(std::min(cells.size(), MAX_INSTANCES));

    const VkDeviceSize bytes = sizeof(Core::WaterSurfaceCell) * count;
    void* data;
    vkMapMemory(m_device, m_instanceBufferMemory, 0, bytes, 0, &data);
    memcpy(data, cells.data(), bytes);
    vkUnmapMemory(m_device, m_instanceBufferMemory);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    VkDescriptorSet sets[] = { uboSet, m_descriptorSet };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 2, sets, 0, nullptr);
    VkBuffer buffers[] = {m_vertexBuffer, m_instanceBuffer};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);

    WaterCellPush pc{};
    // NOTE: water is authored in ABSOLUTE world space (the instance data carries world cell
    // positions), so it builds its own viewProj from the camera rather than using ubo.viewProj,
    // which is the camera-RELATIVE one (see docs/CameraRelativeRendering.md). Only the UBO's
    // sun/ambient and its view/proj (rotation + projection, both convention-independent) are used.
    pc.viewProj = projectionMatrix * camera.getViewMatrix();
    float t = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_startTime).count();
    pc.camPosTime = glm::vec4(camera.getPosition(), t);
    pc.screen = glm::vec4(static_cast<float>(screenExtent.width),
                          static_cast<float>(screenExtent.height), 0.0f, 0.0f);
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
