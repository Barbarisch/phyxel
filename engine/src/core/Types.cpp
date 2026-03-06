#include "core/Types.h"
#include "utils/Logger.h"
#include <vulkan/vulkan.h>
#include <iostream>

namespace Phyxel {

glm::ivec3 VoxelLocation::getAdjacentPlacementPosition() const {
    LOG_INFO_FMT("VoxelLocation", "[getAdjacentPlacementPosition] Called with worldPos=(" 
              << worldPos.x << "," << worldPos.y << "," << worldPos.z 
              << ") hitFace=" << hitFace);
    
    // TEST: Verify GLM constructor order
    glm::ivec3 test1(10, 20, 30);
    LOG_INFO_FMT("VoxelLocation", "[GLM TEST] glm::ivec3(10,20,30) -> x=" << test1.x << " y=" << test1.y << " z=" << test1.z);
    
    if (hitFace < 0) {
        LOG_INFO("VoxelLocation", "[getAdjacentPlacementPosition] No face data, returning worldPos");
        return worldPos;
    }
    
    // Face normals match raycaster: 0=left(-X), 1=right(+X), 2=bottom(-Y), 3=top(+Y), 4=back(-Z), 5=front(+Z)
    glm::ivec3 faceOffset;
    switch (hitFace) {
        case 0: faceOffset = glm::ivec3(-1, 0, 0); break;  // left (-X face)
        case 1: faceOffset = glm::ivec3(1, 0, 0); break;   // right (+X face)
        case 2: faceOffset = glm::ivec3(0, -1, 0); break;  // bottom (-Y face)
        case 3: faceOffset = glm::ivec3(0, 1, 0); break;   // top (+Y face)
        case 4: faceOffset = glm::ivec3(0, 0, -1); break;  // back (-Z face)
        case 5: faceOffset = glm::ivec3(0, 0, 1); break;   // front (+Z face)
        default: 
            LOG_WARN_FMT("VoxelLocation", "[getAdjacentPlacementPosition] Invalid hitFace=" << hitFace);
            return worldPos;
    }
    
    glm::ivec3 result = worldPos + faceOffset;
    LOG_INFO_FMT("VoxelLocation", "[getAdjacentPlacementPosition] BEFORE addition:");
    LOG_INFO_FMT("VoxelLocation", "  worldPos: x=" << worldPos.x << " y=" << worldPos.y << " z=" << worldPos.z);
    LOG_INFO_FMT("VoxelLocation", "  faceOffset: x=" << faceOffset.x << " y=" << faceOffset.y << " z=" << faceOffset.z);
    LOG_INFO_FMT("VoxelLocation", "[getAdjacentPlacementPosition] AFTER addition:");
    LOG_INFO_FMT("VoxelLocation", "  result: x=" << result.x << " y=" << result.y << " z=" << result.z);
    
    return result;
}

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

std::array<VkVertexInputAttributeDescription, 6> DynamicSubcubeInstanceData::getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 6> desc{};
    
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
    
    // Scale (vec3)
    desc[3].binding = 1;
    desc[3].location = 4;  // layout(location = 4) in vec3 inScale
    desc[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    desc[3].offset = offsetof(DynamicSubcubeInstanceData, scale);
    
    // Rotation quaternion (vec4)
    desc[4].binding = 1;
    desc[4].location = 5;  // layout(location = 5) in vec4 inRotation
    desc[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    desc[4].offset = offsetof(DynamicSubcubeInstanceData, rotation);
    
    // Local position (ivec3)
    desc[5].binding = 1;
    desc[5].location = 6;  // layout(location = 6) in ivec3 inLocalPosition
    desc[5].format = VK_FORMAT_R32G32B32_SINT;
    desc[5].offset = offsetof(DynamicSubcubeInstanceData, localPosition);
    
    return desc;
}

} // namespace Phyxel
