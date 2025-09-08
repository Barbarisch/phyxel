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

// InstanceData implementation for static cubes/subcubes
VkVertexInputBindingDescription InstanceData::getBindingDescription() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 1;  // Instance data binding
    desc.stride = sizeof(InstanceData);
    desc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    return desc;
}

std::array<VkVertexInputAttributeDescription, 2> InstanceData::getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 2> desc{};
    
    // Packed data (position + face mask + future bits)
    desc[0].binding = 1;
    desc[0].location = 1;  // layout(location = 1) in uint inPackedData
    desc[0].format = VK_FORMAT_R32_UINT;  // uint32_t packedData
    desc[0].offset = offsetof(InstanceData, packedData);
    
    // Texture index
    desc[1].binding = 1;
    desc[1].location = 2;  // layout(location = 2) in uint inTextureIndex
    desc[1].format = VK_FORMAT_R16_UINT;  // uint16_t textureIndex
    desc[1].offset = offsetof(InstanceData, textureIndex);
    
    return desc;
}

// DynamicSubcubeInstanceData implementation for physics-enabled subcubes
VkVertexInputBindingDescription DynamicSubcubeInstanceData::getBindingDescription() {
    VkVertexInputBindingDescription desc{};
    desc.binding = 1;  // Instance data binding (same as regular instances)
    desc.stride = sizeof(DynamicSubcubeInstanceData);
    desc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    return desc;
}

std::array<VkVertexInputAttributeDescription, 5> DynamicSubcubeInstanceData::getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 5> desc{};
    
    // World position (vec3)
    desc[0].binding = 1;
    desc[0].location = 1;  // layout(location = 1) in vec3 inWorldPosition
    desc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    desc[0].offset = offsetof(DynamicSubcubeInstanceData, worldPosition);
    
    // Texture index (uint16)
    desc[1].binding = 1;
    desc[1].location = 2;  // layout(location = 2) in uint inTextureIndex
    desc[1].format = VK_FORMAT_R16_UINT;
    desc[1].offset = offsetof(DynamicSubcubeInstanceData, textureIndex);
    
    // Face ID (uint)
    desc[2].binding = 1;
    desc[2].location = 3;  // layout(location = 3) in uint inFaceID
    desc[2].format = VK_FORMAT_R32_UINT;
    desc[2].offset = offsetof(DynamicSubcubeInstanceData, faceID);
    
    // Scale (float)
    desc[3].binding = 1;
    desc[3].location = 4;  // layout(location = 4) in float inScale
    desc[3].format = VK_FORMAT_R32_SFLOAT;
    desc[3].offset = offsetof(DynamicSubcubeInstanceData, scale);
    
    // Rotation quaternion (vec4)
    desc[4].binding = 1;
    desc[4].location = 5;  // layout(location = 5) in vec4 inRotation
    desc[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    desc[4].offset = offsetof(DynamicSubcubeInstanceData, rotation);
    
    return desc;
}

} // namespace VulkanCube
