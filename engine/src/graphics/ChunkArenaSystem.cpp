#include "graphics/ChunkArenaSystem.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "graphics/GpuAllocStats.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Graphics {

namespace {

// Heap-backed blocks for headless unit tests of the arena paths.
struct HeapArenaBackend : ArenaBlockBackend {
    struct Blk { void* mem = nullptr; };
    std::vector<Blk> blocks;

    int createBlock(size_t bytes) override {
        void* mem = std::malloc(bytes);
        if (!mem) return -1;
        std::memset(mem, 0, bytes);
        blocks.push_back({mem});
        return int(blocks.size()) - 1;
    }
    void destroyBlock(int handle) override {
        if (handle < 0 || size_t(handle) >= blocks.size()) return;
        std::free(blocks[size_t(handle)].mem);
        blocks[size_t(handle)].mem = nullptr;
    }
    ~HeapArenaBackend() override {
        for (Blk& b : blocks) std::free(b.mem);
    }
    void* mapped(int handle) const {
        if (handle < 0 || size_t(handle) >= blocks.size()) return nullptr;
        return blocks[size_t(handle)].mem;
    }
};

// Production blocks: one VkBuffer + VkDeviceMemory + persistent map each. The
// SAME memory profile as today's per-chunk buffers (HOST_VISIBLE|COHERENT,
// vertex-buffer usage), just 64 MB at a time. One vkAllocateMemory per block is
// the whole point (GpuAllocStats ceiling).
struct VulkanArenaBackend : ArenaBlockBackend {
    struct Blk {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::vector<Blk> blocks;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & props) == props)
                return i;
        }
        throw std::runtime_error("ChunkArenaSystem: no suitable memory type");
    }

    int createBlock(size_t bytes) override {
        Blk b;
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bytes;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &b.buffer) != VK_SUCCESS) {
            LOG_ERROR("ChunkArena", "block vkCreateBuffer failed ({} bytes)", bytes);
            return -1;
        }
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, b.buffer, &memReq);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex =
            findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &b.memory) != VK_SUCCESS) {
            LOG_ERROR("ChunkArena", "block vkAllocateMemory failed ({} bytes)", bytes);
            vkDestroyBuffer(device, b.buffer, nullptr);
            return -1;
        }
        vkBindBufferMemory(device, b.buffer, b.memory, 0);
        if (vkMapMemory(device, b.memory, 0, bytes, 0, &b.mapped) != VK_SUCCESS) {
            LOG_ERROR("ChunkArena", "block vkMapMemory failed");
            vkDestroyBuffer(device, b.buffer, nullptr);
            vkFreeMemory(device, b.memory, nullptr);
            return -1;
        }
        gpualloc::note();
        blocks.push_back(b);
        LOG_INFO("ChunkArena", "block {} created ({} MB)", blocks.size() - 1, bytes >> 20);
        return int(blocks.size()) - 1;
    }

    void destroyBlock(int handle) override {
        if (handle < 0 || size_t(handle) >= blocks.size()) return;
        Blk& b = blocks[size_t(handle)];
        if (b.buffer == VK_NULL_HANDLE) return;
        // Only called for blocks fully empty for the release window (hundreds of
        // frames) or at shutdown after vkDeviceWaitIdle — never GPU-in-flight.
        vkUnmapMemory(device, b.memory);
        vkDestroyBuffer(device, b.buffer, nullptr);
        vkFreeMemory(device, b.memory, nullptr);
        b = {};
        gpualloc::release();
    }

    VkBuffer bufferOf(int handle) const {
        if (handle < 0 || size_t(handle) >= blocks.size()) return VK_NULL_HANDLE;
        return blocks[size_t(handle)].buffer;
    }
    void* mapped(int handle) const {
        if (handle < 0 || size_t(handle) >= blocks.size()) return nullptr;
        return blocks[size_t(handle)].mapped;
    }
};

} // namespace

ChunkArenaSystem& ChunkArenaSystem::instance() {
    static ChunkArenaSystem s;
    return s;
}

void ChunkArenaSystem::initialize(VkDevice device, VkPhysicalDevice physicalDevice) {
    if (m_allocator) return;  // idempotent
    auto backend = std::make_unique<VulkanArenaBackend>();
    backend->device = device;
    backend->physicalDevice = physicalDevice;
    m_device = device;
    m_allocator = std::make_unique<ChunkArenaAllocator>(backend.get());
    m_backend = std::move(backend);
    LOG_INFO("ChunkArena", "initialized (block size {} MB)",
             ChunkArenaAllocator::kDefaultBlockSize >> 20);
}

void ChunkArenaSystem::initializeForTests(size_t blockSize) {
    m_allocator.reset();
    m_backend.reset();
    m_device = VK_NULL_HANDLE;
    auto backend = std::make_unique<HeapArenaBackend>();
    // Small empty-block window so tests can exercise release without 600 ticks.
    m_allocator = std::make_unique<ChunkArenaAllocator>(backend.get(), blockSize, 8);
    m_backend = std::move(backend);
}

void ChunkArenaSystem::shutdown() {
    // Allocator dtor destroys every live block through the backend, so order matters.
    m_allocator.reset();
    m_backend.reset();
    m_device = VK_NULL_HANDLE;
}

void ChunkArenaSystem::tick() {
    if (m_allocator) m_allocator->tick();
}

VkBuffer ChunkArenaSystem::blockBuffer(uint32_t blockId) const {
    if (!m_allocator) return VK_NULL_HANDLE;
    const int handle = m_allocator->blockHandle(blockId);
    if (auto* vk = dynamic_cast<const VulkanArenaBackend*>(m_backend.get()))
        return vk->bufferOf(handle);
    return VK_NULL_HANDLE;  // test mode
}

void* ChunkArenaSystem::blockMapped(uint32_t blockId) const {
    if (!m_allocator) return nullptr;
    const int handle = m_allocator->blockHandle(blockId);
    if (auto* vk = dynamic_cast<const VulkanArenaBackend*>(m_backend.get()))
        return vk->mapped(handle);
    if (auto* heap = dynamic_cast<const HeapArenaBackend*>(m_backend.get()))
        return heap->mapped(handle);
    return nullptr;
}

uint64_t ChunkArenaSystem::regionKeyForChunkOrigin(const glm::ivec3& worldOrigin) {
    // Chunk coord (floor division for negatives), then region coord: 8x4x8 chunks.
    auto floorDiv = [](int v, int d) { return (v >= 0) ? v / d : -((-v + d - 1) / d); };
    const int cx = floorDiv(worldOrigin.x, 32), cy = floorDiv(worldOrigin.y, 32),
              cz = floorDiv(worldOrigin.z, 32);
    const int rx = floorDiv(cx, 8), ry = floorDiv(cy, 4), rz = floorDiv(cz, 8);
    // Pack three signed region coords into 21-bit fields (offset-bias).
    const uint64_t bias = 1u << 20;
    return ((uint64_t(rx) + bias) & 0x1FFFFF) |
           (((uint64_t(ry) + bias) & 0x1FFFFF) << 21) |
           (((uint64_t(rz) + bias) & 0x1FFFFF) << 42);
}

} // namespace Graphics
} // namespace Phyxel
