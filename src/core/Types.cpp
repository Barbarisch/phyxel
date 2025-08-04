#include "core/Types.h"
#include <vulkan/vulkan.h>

namespace VulkanCube {

VkVertexInputBindingDescription Vertex::getBindingDescription() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 0;
    desc.stride = sizeof(Vertex);  
    desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return desc;
}

std::array<VkVertexInputAttributeDescription, 1> Vertex::getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 1> desc{};
    desc[0].binding = 0;
    desc[0].location = 0;
    desc[0].format = VK_FORMAT_R32_UINT;  // 32-bit unsigned int for vertex ID
    desc[0].offset = offsetof(Vertex, vertexID);
    return desc;
}

} // namespace VulkanCube
