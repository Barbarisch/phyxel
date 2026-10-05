#include "vulkan/ComputePipeline.h"
#include "utils/Logger.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace Phyxel {
namespace Vulkan {

std::vector<char> ComputePipeline::loadSpv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("ComputePipeline: failed to open shader: " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), static_cast<std::streamsize>(size));
    return buf;
}

bool ComputePipeline::create(VkDevice device, const std::string& spvPath,
                              uint32_t bindingCount, uint32_t pushConstSize, uint32_t setCount) {
    m_device        = device;
    m_bindingCount  = bindingCount;
    m_pushConstSize = pushConstSize;
    setCount        = std::max(setCount, 1u);
    m_bindings.assign(setCount, std::vector<BufferBinding>(bindingCount));

    // --- Descriptor set layout: one STORAGE_BUFFER binding per slot ---
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings(bindingCount);
    for (uint32_t i = 0; i < bindingCount; ++i) {
        layoutBindings[i].binding            = i;
        layoutBindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        layoutBindings[i].descriptorCount    = 1;
        layoutBindings[i].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
        layoutBindings[i].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo dsLayoutInfo{};
    dsLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsLayoutInfo.bindingCount = bindingCount;
    dsLayoutInfo.pBindings    = layoutBindings.empty() ? nullptr : layoutBindings.data();

    if (vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &m_dsLayout) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to create descriptor set layout");
        return false;
    }

    // --- Descriptor pool ---
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = std::max(bindingCount, 1u) * setCount; // must be > 0

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets       = setCount;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_dsPool) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to create descriptor pool");
        return false;
    }

    // --- Allocate descriptor sets (one per frame slot) ---
    std::vector<VkDescriptorSetLayout> layouts(setCount, m_dsLayout);
    m_sets.assign(setCount, VK_NULL_HANDLE);
    VkDescriptorSetAllocateInfo dsAllocInfo{};
    dsAllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool     = m_dsPool;
    dsAllocInfo.descriptorSetCount = setCount;
    dsAllocInfo.pSetLayouts        = layouts.data();

    if (vkAllocateDescriptorSets(device, &dsAllocInfo, m_sets.data()) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to allocate descriptor sets");
        return false;
    }

    // --- Pipeline layout ---
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &m_dsLayout;

    VkPushConstantRange pcRange{};
    if (pushConstSize > 0) {
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset     = 0;
        pcRange.size       = pushConstSize;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &pcRange;
    }

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to create pipeline layout");
        return false;
    }

    // --- Shader module ---
    std::vector<char> code;
    try {
        code = loadSpv(spvPath);
    } catch (const std::exception& e) {
        LOG_ERROR_FMT("ComputePipeline", "Shader load failed: " << e.what());
        return false;
    }

    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = code.size();
    shaderInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to create shader module");
        return false;
    }

    // --- Compute pipeline ---
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout             = m_pipelineLayout;
    pipelineInfo.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module       = shaderModule;
    pipelineInfo.stage.pName        = "main";

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr); // safe to destroy after pipeline creation

    if (result != VK_SUCCESS) {
        LOG_ERROR_FMT("ComputePipeline", "Failed to create compute pipeline (VkResult=" << result << "): " << spvPath);
        return false;
    }

    LOG_INFO_FMT("ComputePipeline", "Created: " << spvPath << " (" << bindingCount << " bindings)");
    return true;
}

void ComputePipeline::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_dsPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_dsPool, nullptr); // also frees the sets
        m_dsPool = VK_NULL_HANDLE;
        m_sets.clear();
    }
    if (m_dsLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_dsLayout, nullptr);
        m_dsLayout = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

void ComputePipeline::bindBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset) {
    for (uint32_t set = 0; set < m_bindings.size(); ++set) bindBufferInSet(set, binding, buffer, size, offset);
}

void ComputePipeline::bindBufferInSet(uint32_t set, uint32_t binding, VkBuffer buffer, VkDeviceSize size,
                                      VkDeviceSize offset) {
    if (set < m_bindings.size() && binding < m_bindings[set].size()) {
        m_bindings[set][binding] = { binding, buffer, size, offset };
    }
}

void ComputePipeline::updateDescriptors() {
    const size_t total = m_sets.size() * m_bindingCount;
    std::vector<VkDescriptorBufferInfo> bufInfos(total);
    std::vector<VkWriteDescriptorSet>   writes(total);

    for (uint32_t set = 0; set < m_sets.size(); ++set) {
        for (uint32_t i = 0; i < m_bindingCount; ++i) {
            const size_t w = set * m_bindingCount + i;
            const BufferBinding& b = m_bindings[set][i];
            bufInfos[w].buffer = b.buffer;
            bufInfos[w].offset = b.offset;
            bufInfos[w].range  = b.size > 0 ? b.size : VK_WHOLE_SIZE;

            writes[w].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[w].pNext           = nullptr;
            writes[w].dstSet          = m_sets[set];
            writes[w].dstBinding      = i;
            writes[w].dstArrayElement = 0;
            writes[w].descriptorCount = 1;
            writes[w].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[w].pBufferInfo     = &bufInfos[w];
        }
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void ComputePipeline::bind(VkCommandBuffer cmd, uint32_t set) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    const VkDescriptorSet ds = m_sets[set < m_sets.size() ? set : 0];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, 1, &ds, 0, nullptr);
}

void ComputePipeline::pushConstants(VkCommandBuffer cmd, const void* data, uint32_t size) const {
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, size, data);
}

void ComputePipeline::dispatch(VkCommandBuffer cmd, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) const {
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
}

} // namespace Vulkan
} // namespace Phyxel
