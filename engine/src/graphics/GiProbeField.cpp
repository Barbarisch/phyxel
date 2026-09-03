#include "graphics/GiProbeField.h"

#include "vulkan/VulkanDevice.h"
#include "core/AssetManager.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"

#include <cmath>
#include <vector>

namespace Phyxel {
namespace Graphics {

namespace {
// Must match the GiPush block in gi_probe.comp, field for field.
struct GiPush {
    glm::vec4  originAndSpacing;
    glm::ivec4 dims;
    glm::vec4  ambientColor;
    glm::ivec4 occBox;
    glm::vec4  sunDirection;   // M5.2: lights the surface a bounce ray lands on
    glm::vec4  sunColor;
};
}  // namespace

glm::vec4 GiProbeField::gridFor(const glm::vec3& viewerWorld) {
    // Snap the grid origin to the probe lattice. Without this the field slides continuously under
    // the sampler as the camera moves and every probe's value changes every frame -- which reads as
    // a crawling shimmer over every surface, and is the classic way a probe field looks broken
    // while every individual probe is correct.
    const float s = kSpacing;
    const glm::vec3 half(kDimX * 0.5f * s, kDimY * 0.5f * s, kDimZ * 0.5f * s);
    glm::vec3 o = viewerWorld - half;
    o.x = std::floor(o.x / s) * s;
    o.y = std::floor(o.y / s) * s;
    o.z = std::floor(o.z / s) * s;
    return glm::vec4(o, s);
}

bool GiProbeField::initialize(Vulkan::VulkanDevice* device) {
    if (!device) return false;
    m_device = device;
    VkDevice dev = device->getDevice();

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(kProbeCount) * sizeof(glm::vec4);
    device->createBuffer(bytes,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_buffer, m_memory);
    if (m_buffer == VK_NULL_HANDLE) {
        LOG_ERROR("GiProbeField", "probe buffer allocation failed");
        return false;
    }

    // Pipeline layout: the SHARED set-0 layout (occupancy.glsl hardcodes bindings 11/12, and the
    // light SSBO is binding 3) plus the push block.
    VkDescriptorSetLayout setLayout = device->getDescriptorSetLayout();
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(GiPush);

    VkPipelineLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &setLayout;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(dev, &lci, nullptr, &m_layout) != VK_SUCCESS) {
        LOG_ERROR("GiProbeField", "pipeline layout creation failed");
        return false;
    }

    std::vector<char> code;
    try {
        code = Utils::readFile(Core::AssetManager::instance().resolveShader("gi_probe.comp.spv"));
    } catch (const std::exception& e) {
        LOG_ERROR_FMT("GiProbeField", "gi_probe.comp.spv load failed: " << e.what());
        return false;
    }

    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = code.size();
    smi.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &smi, nullptr, &module) != VK_SUCCESS) {
        LOG_ERROR("GiProbeField", "shader module creation failed");
        return false;
    }

    VkComputePipelineCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.layout = m_layout;
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = module;
    cpi.stage.pName = "main";
    const VkResult res = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &m_pipeline);
    vkDestroyShaderModule(dev, module, nullptr);
    if (res != VK_SUCCESS) {
        LOG_ERROR_FMT("GiProbeField", "compute pipeline creation failed: " << res);
        return false;
    }

    LOG_INFO_FMT("GiProbeField", "M5.1 probe field ready: " << kProbeCount << " probes ("
                 << (bytes / 1024) << " KB), spacing " << kSpacing);
    return true;
}

void GiProbeField::recordUpdate(VkCommandBuffer cmd, VkDescriptorSet set,
                                const glm::vec3& gridOrigin, const glm::vec3& ambientColor,
                                const glm::ivec4& occBox,
                                const glm::vec3& sunDirection, const glm::vec3& sunColor) {
    if (m_pipeline == VK_NULL_HANDLE || set == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &set, 0, nullptr);

    GiPush push{};
    push.originAndSpacing = glm::vec4(gridOrigin, kSpacing);
    // M5.3: w carries this frame's refresh PHASE. Each probe is updated once every kPhases
    // frames and the dispatch shrinks to match, which is what makes 18 directions affordable.
    push.dims = glm::ivec4(kDimX, kDimY, kDimZ, static_cast<int>(m_phase));
    push.ambientColor = glm::vec4(ambientColor, 0.0f);
    push.occBox = occBox;
    push.sunDirection = glm::vec4(sunDirection, 0.0f);
    push.sunColor = glm::vec4(sunColor, 0.0f);
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);

    // Only 1/kPhases of the grid this frame. Must match kPhases in gi_probe.comp.
    const uint32_t slice = (kProbeCount + kPhases - 1) / kPhases;
    vkCmdDispatch(cmd, (slice + 63) / 64, 1, 1);
    m_phase = (m_phase + 1) % kPhases;

    // READ-AFTER-WRITE, and it is spelled out because this plan has already been bitten once:
    // the bloom blur ping-ponged ten passes with no dependency at all, and the result was bloom
    // that was not a deterministic function of its input (D20/U5). The fragment shaders sample
    // this buffer later in the frame, so the compute write must be made available and visible.
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void GiProbeField::cleanup() {
    if (!m_device) return;
    VkDevice dev = m_device->getDevice();
    if (m_pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(dev, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
    if (m_layout   != VK_NULL_HANDLE) { vkDestroyPipelineLayout(dev, m_layout, nullptr); m_layout = VK_NULL_HANDLE; }
    if (m_buffer   != VK_NULL_HANDLE) { vkDestroyBuffer(dev, m_buffer, nullptr); m_buffer = VK_NULL_HANDLE; }
    if (m_memory   != VK_NULL_HANDLE) { vkFreeMemory(dev, m_memory, nullptr); m_memory = VK_NULL_HANDLE; }
    m_device = nullptr;
}

}  // namespace Graphics
}  // namespace Phyxel
