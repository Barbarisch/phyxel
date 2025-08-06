/*
 * Multi-Chunk Rendering System for VulkanCube
 * 
 * This example demonstrates how to use the new ChunkManager to render
 * multiple 32x32x32 chunks efficiently with separate Vulkan buffers.
 * 
 * Key Benefits:
 * - Easy to scale from 10 to 50+ chunks
 * - Each chunk has its own Vulkan buffer and draw call
 * - Maintains 5-bit shader compatibility (relative positions 0-31)
 * - World-space positioning via chunk origins
 */

#include "core/ChunkManager.h"
#include "examples/MultiChunkDemo.h"

namespace VulkanCube {

// Example integration in your Application class
void Application::initializeChunkSystem() {
    // Create the chunk manager
    chunkManager = std::make_unique<ChunkManager>();
    
    // Initialize with Vulkan device handles
    chunkManager->initialize(
        vulkanDevice->getDevice(), 
        vulkanDevice->getPhysicalDevice()
    );
    
    // Example 1: Create 10 chunks in a line
    auto linearOrigins = MultiChunkDemo::createLinearChunks(10);
    chunkManager->createChunks(linearOrigins);
    
    // Example 2: Create a 5x5 grid of chunks (25 total)
    // auto gridOrigins = MultiChunkDemo::createGridChunks(5, 5);
    // chunkManager->createChunks(gridOrigins);
    
    // Example 3: Create a 3D grid 3x3x3 (27 chunks)
    // auto grid3DOrigins = MultiChunkDemo::create3DGridChunks(3, 3, 3);
    // chunkManager->createChunks(grid3DOrigins);
}

// Example rendering loop modification
void Application::renderChunks(VkCommandBuffer commandBuffer) {
    // Bind your vertex buffer (cube geometry) once
    VkBuffer vertexBuffers[] = {/* your vertex buffer */};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    
    // Bind your index buffer once
    // vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    
    // Render each chunk with its own instance buffer and draw call
    for (size_t i = 0; i < chunkManager->chunks.size(); ++i) {
        const Chunk& chunk = chunkManager->chunks[i];
        
        // Bind this chunk's instance buffer
        VkBuffer instanceBuffers[] = {chunk.instanceBuffer};
        VkDeviceSize instanceOffsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 1, 1, instanceBuffers, instanceOffsets);
        
        // Update push constants with chunk world origin for shader
        struct PushConstants {
            glm::ivec3 chunkOrigin;
            // ... other push constant data
        } pushConstants;
        
        pushConstants.chunkOrigin = chunk.worldOrigin;
        vkCmdPushConstants(commandBuffer, pipelineLayout, 
                          VK_SHADER_STAGE_VERTEX_BIT, 0, 
                          sizeof(PushConstants), &pushConstants);
        
        // Draw this chunk (32,768 instances)
        vkCmdDraw(commandBuffer, 8 /* vertices per cube */, chunk.numInstances, 0, 0);
        // OR if using indexed drawing:
        // vkCmdDrawIndexed(commandBuffer, indexCount, chunk.numInstances, 0, 0, 0);
    }
}

// Example shader modification (cube.vert)
/*
#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    uint numInstances;
} ubo;

layout(push_constant) uniform PushConstants {
    ivec3 chunkOrigin;  // World-space chunk origin
} pc;

layout(location = 0) in uint vertexID;
layout(location = 1) in uint packedData;  // Instance data
layout(location = 2) in vec3 color;

void main() {
    // Extract relative position (0-31) from packed data (your existing logic)
    uint relativeX = packedData & 0x1F;
    uint relativeY = (packedData >> 5) & 0x1F;
    uint relativeZ = (packedData >> 10) & 0x1F;
    
    // Convert to world position by adding chunk origin
    vec3 worldPos = vec3(relativeX, relativeY, relativeZ) + vec3(pc.chunkOrigin);
    
    // Your existing vertex shader logic...
}
*/

} // namespace VulkanCube

/*
 * Usage Summary:
 * 
 * 1. Initialize ChunkManager with Vulkan device
 * 2. Create chunks using provided demo patterns or custom origins
 * 3. Modify render loop to draw each chunk separately
 * 4. Update shader to use chunk origin from push constants
 * 
 * Performance Notes:
 * - Each chunk = 1 draw call (scalable)
 * - 10 chunks = 327,680 cubes total
 * - 50 chunks = 1,638,400 cubes total
 * - Memory usage: ~0.5MB per chunk (32K instances × 16 bytes)
 */
