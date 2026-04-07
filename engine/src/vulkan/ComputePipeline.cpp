#include "vulkan/ComputePipeline.h"
#include "utils/Logger.h"
#include <fstream>
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
                              uint32_t bindingCount, uint32_t pushConstSize) {
    m_device        = device;
    m_bindingCount  = bindingCount;
    m_pushConstSize = pushConstSize;
    m_bindings.resize(bindingCount);

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
    poolSize.descriptorCount = std::max(bindingCount, 1u); // must be > 0

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_dsPool) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to create descriptor pool");
        return false;
    }

    // --- Allocate descriptor set ---
    VkDescriptorSetAllocateInfo dsAllocInfo{};
    dsAllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool     = m_dsPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts        = &m_dsLayout;

    if (vkAllocateDescriptorSets(device, &dsAllocInfo, &m_ds) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to allocate descriptor set");
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
        vkDestroyDescriptorPool(m_device, m_dsPool, nullptr); // also frees m_ds
        m_dsPool = VK_NULL_HANDLE;
        m_ds     = VK_NULL_HANDLE;
    }
    if (m_dsLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_dsLayout, nullptr);
        m_dsLayout = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

void ComputePipeline::bindBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset) {
    if (binding < m_bindings.size()) {
        m_bindings[binding] = { binding, buffer, size, offset };
    }
}

void ComputePipeline::updateDescriptors() {
    std::vector<VkDescriptorBufferInfo> bufInfos(m_bindingCount);
    std::vector<VkWriteDescriptorSet>   writes(m_bindingCount);

    for (uint32_t i = 0; i < m_bindingCount; ++i) {
        bufInfos[i].buffer = m_bindings[i].buffer;
        bufInfos[i].offset = m_bindings[i].offset;
        bufInfos[i].range  = m_bindings[i].size > 0 ? m_bindings[i].size : VK_WHOLE_SIZE;

        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].pNext           = nullptr;
        writes[i].dstSet          = m_ds;
        writes[i].dstBinding      = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo     = &bufInfos[i];
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void ComputePipeline::bind(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipelineLayout, 0, 1, &m_ds, 0, nullptr);
}

void ComputePipeline::pushConstants(VkCommandBuffer cmd, const void* data, uint32_t size) const {
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, size, data);
}

void ComputePipeline::dispatch(VkCommandBuffer cmd, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) const {
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
}

} // namespace Vulkan
} // namespace Phyxel
