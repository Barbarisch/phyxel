#include "graphics/ChunkRenderBuffer.h"
#include "graphics/ChunkArenaSystem.h"       // Phase 4.3 arena mode (docs/RegionArenaPlan.md)
#include "graphics/ChunkUpdatePerf.h"        // B0 diagnostic timers (docs/ChunkUpdateHitchPlan.md)
#include "graphics/DeferredBufferReclaim.h"  // B1 deferred free (docs/ChunkUpdateHitchPlan.md)
#include "graphics/GpuAllocStats.h"          // Phase 4 attribution (docs/LargeWorldScalePlan.md)
#include "core/Types.h"
#include "utils/Logger.h"
#include <atomic>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <string>

namespace Phyxel {
namespace Graphics {

namespace {
// Phase 4 attribution helpers (docs/LargeWorldScalePlan.md blocker D). Diagnostic only.
uint32_t allocLimit(VkPhysicalDevice pd) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(pd, &props);
    return props.limits.maxMemoryAllocationCount;
}

// Log the device's ceiling once, so the live counter has a denominator in the log.
void logAllocLimitOnce(VkPhysicalDevice pd) {
    static std::atomic<bool> done{false};
    bool expected = false;
    if (!done.compare_exchange_strong(expected, true)) return;
    LOG_INFO("ChunkRenderBuffer", "GPU allocation ceiling: maxMemoryAllocationCount=" +
             std::to_string(allocLimit(pd)) +
             " (this engine makes 3 bare allocations per chunk: faces/grass/foliage)");
}

// Count a successful allocation and log every 256th. Polling the counter over the API is useless
// under exactly the load we care about (queueAndWait times out while the loop is busy streaming),
// and a crash kills the process before any poll — but the log is already on disk. So the climb
// toward the ceiling is recorded here.
void noteAlloc(VkPhysicalDevice pd) {
    const uint32_t n = gpualloc::note();
    if ((n & 0xFF) == 0) {  // every 256 live allocations
        LOG_INFO("ChunkRenderBuffer", "live chunk GPU allocations=" + std::to_string(n) +
                 " / maxMemoryAllocationCount=" + std::to_string(allocLimit(pd)) +
                 " (~" + std::to_string(n / 3) + " chunks at 3 buffers each)");
    }
}

// The allocation that kills us is silent today: ChunkRenderBuffer throws and nothing on the main
// thread catches it → std::terminate with no log. Log the numbers BEFORE throwing so a crash is
// attributable: live near the ceiling => blocker D; far from it => blocker C (host RAM).
void logAllocFailure(VkPhysicalDevice pd, VkDeviceSize bytes, const char* what) {
    LOG_ERROR("ChunkRenderBuffer",
              std::string("vkAllocateMemory FAILED (") + what + "): requested " +
              std::to_string(static_cast<uint64_t>(bytes)) + " bytes; live chunk allocations=" +
              std::to_string(gpualloc::live().load()) + " (peak " +
              std::to_string(gpualloc::peak().load()) + ") vs device maxMemoryAllocationCount=" +
              std::to_string(allocLimit(pd)) +
              ". Near the ceiling => blocker D (allocation-count limit); far from it => blocker C "
              "(host memory exhaustion). See docs/LargeWorldScalePlan.md.");
}
}  // namespace

// B1 toggle: when true (default), reallocateBuffer defers the old buffer/memory free by
// > MAX_FRAMES_IN_FLIGHT frames instead of freeing inline — fixes the in-flight use-after-free and
// the realloc stall. OFF reproduces the old inline-free behaviour byte-for-byte for A/B.
bool ChunkRenderBuffer::s_deferBufferFree = true;

// Phase 4.3 (docs/RegionArenaPlan.md): DEFAULT ON since A3. Gates: A2 visual
// identity (bitwise-stable per config; 2 leaf-edge pixels <=2/255 across modes);
// A3 on the 1:1 world (Release) — allocations 4,693 -> 38 blocks at ~4k chunks,
// OFF/ON idle soaks both clean (stable ~9 GB plateau, 83-112 FPS), validation
// run shows zero buffer/memory VUIDs (remaining hits = pre-existing debug-line
// pipeline interface + boot semaphore quirks). OFF reproduces per-chunk buffers
// for A/B via POST /api/debug/region_arenas.
bool ChunkRenderBuffer::s_regionArenas = true;

ChunkRenderBuffer::ChunkRenderBuffer(VkDevice device, VkPhysicalDevice physicalDevice)
    : device(device)
    , physicalDevice(physicalDevice)
    , instanceBuffer(VK_NULL_HANDLE)
    , instanceMemory(VK_NULL_HANDLE)
    , mappedMemory(nullptr)
    , bufferCapacity(0)
    , maxInstancesUsed(0)
    , elementSize(sizeof(InstanceData))
{
}

ChunkRenderBuffer::~ChunkRenderBuffer() {
    cleanup();
}

ChunkRenderBuffer::ChunkRenderBuffer(ChunkRenderBuffer&& other) noexcept
    : device(other.device)
    , physicalDevice(other.physicalDevice)
    , instanceBuffer(other.instanceBuffer)
    , instanceMemory(other.instanceMemory)
    , mappedMemory(other.mappedMemory)
    , bufferCapacity(other.bufferCapacity)
    , maxInstancesUsed(other.maxInstancesUsed)
    , elementSize(other.elementSize)
    , m_span(other.m_span)
    , m_regionKey(other.m_regionKey)
    , m_arenaMode(other.m_arenaMode)
{
    other.instanceBuffer = VK_NULL_HANDLE;
    other.instanceMemory = VK_NULL_HANDLE;
    other.mappedMemory = nullptr;
    other.bufferCapacity = 0;
    other.maxInstancesUsed = 0;
    other.m_span = {};
    other.m_arenaMode = false;
}

ChunkRenderBuffer& ChunkRenderBuffer::operator=(ChunkRenderBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        
        device = other.device;
        physicalDevice = other.physicalDevice;
        instanceBuffer = other.instanceBuffer;
        instanceMemory = other.instanceMemory;
        mappedMemory = other.mappedMemory;
        bufferCapacity = other.bufferCapacity;
        maxInstancesUsed = other.maxInstancesUsed;
        elementSize = other.elementSize;
        m_span = other.m_span;
        m_regionKey = other.m_regionKey;
        m_arenaMode = other.m_arenaMode;

        other.instanceBuffer = VK_NULL_HANDLE;
        other.instanceMemory = VK_NULL_HANDLE;
        other.mappedMemory = nullptr;
        other.bufferCapacity = 0;
        other.maxInstancesUsed = 0;
        other.m_span = {};
        other.m_arenaMode = false;
    }
    return *this;
}

void ChunkRenderBuffer::createBuffer(const std::vector<InstanceData>& initialData, size_t capacity) {
    createBufferRaw(initialData.data(), initialData.size(), sizeof(InstanceData), capacity);
}

void ChunkRenderBuffer::createBufferRaw(const void* initialData, size_t count, size_t elemSize, size_t capacity) {
    // Phase 4.3 arena mode (docs/RegionArenaPlan.md A1): suballocate a span from the
    // region's shared block instead of creating a per-chunk VkBuffer. Works headless
    // (test-mode arena) — this branch never touches the device directly.
    if (s_regionArenas) {
        auto& arena = ChunkArenaSystem::instance();
        if (!arena.initialized() && device != VK_NULL_HANDLE) {
            arena.initialize(device, physicalDevice);
        }
        if (arena.initialized()) {
            if (m_arenaMode && m_span.valid()) {
                arena.allocator()->retire(m_span);   // re-create over an existing span
                m_span = {};
            }
            elementSize = elemSize;
            // NO 25000-instance floor: size to the data (or the caller's explicit
            // capacity), plus ~12.5% headroom so small remesh growth doesn't churn.
            const size_t wantInstances = std::max<size_t>(1, std::max(count, capacity));
            const size_t bytes = wantInstances * elemSize;
            m_span = arena.allocator()->allocate(m_regionKey, bytes + bytes / 8);
            if (m_span.valid()) {
                m_arenaMode = true;
                bufferCapacity = m_span.capacity / elemSize;
                instanceBuffer = arena.blockBuffer(m_span.blockId);  // block's VkBuffer
                instanceMemory = VK_NULL_HANDLE;                     // block owns memory
                mappedMemory = static_cast<uint8_t*>(arena.blockMapped(m_span.blockId)) +
                               m_span.offset;
                if (initialData && count > 0) {
                    memcpy(mappedMemory, initialData, elemSize * count);
                }
                return;
            }
            // Span allocation failed (backend failure): fall through to legacy.
        }
    }

    if (device == VK_NULL_HANDLE) {
        throw std::runtime_error("ChunkRenderBuffer not initialized with valid Vulkan device!");
    }

    elementSize = elemSize;

    // Use provided capacity, or calculate based on initial data
    if (capacity == 0) {
        capacity = std::max(DEFAULT_BUFFER_CAPACITY, count);
    }

    VkDeviceSize bufferSize = elementSize * capacity;
    bufferCapacity = capacity;

    // Create buffer with fixed capacity
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &instanceBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create chunk instance buffer!");
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, instanceBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    logAllocLimitOnce(physicalDevice);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &instanceMemory) != VK_SUCCESS) {
        logAllocFailure(physicalDevice, allocInfo.allocationSize, "create");
        throw std::runtime_error("Failed to allocate chunk instance buffer memory!");
    }
    noteAlloc(physicalDevice);

    vkBindBufferMemory(device, instanceBuffer, instanceMemory, 0);

    // Map memory persistently for easy updates
    vkMapMemory(device, instanceMemory, 0, bufferSize, 0, &mappedMemory);
    
    // Copy initial data (only the used portion)
    if (initialData && count > 0) {
        VkDeviceSize usedSize = elementSize * count;
        memcpy(mappedMemory, initialData, usedSize);
    }
}

void ChunkRenderBuffer::reallocateBuffer(size_t requiredInstances) {
    ScopedChunkPerf _perf(ChunkPerfPhase::BufferRealloc);  // B0: time the growth realloc

    // Arena mode: growth = new span + retire old (3-tick reuse margin honors the
    // frames-in-flight contract). No Vulkan create/free — the realloc stall class
    // (ChunkUpdateHitchPlan B0's ≤32 ms tail) cannot occur here. Contents are NOT
    // copied, matching the legacy contract (every caller rewrites fully after grow).
    if (m_arenaMode) {
        auto& arena = ChunkArenaSystem::instance();
        if (!arena.initialized()) return;  // shutdown race: nothing to grow into
        const size_t bytes = requiredInstances * elementSize;
        ArenaSpan grown = arena.allocator()->allocate(m_regionKey, bytes + bytes / 8);
        if (!grown.valid()) {
            throw std::runtime_error("ChunkArena: span growth failed");
        }
        arena.allocator()->retire(m_span);
        m_span = grown;
        bufferCapacity = m_span.capacity / elementSize;
        instanceBuffer = arena.blockBuffer(m_span.blockId);
        mappedMemory = static_cast<uint8_t*>(arena.blockMapped(m_span.blockId)) +
                       m_span.offset;
        return;
    }

    // Calculate new capacity with headroom (50% extra)
    size_t newCapacity = static_cast<size_t>(requiredInstances * 1.5f);

    // Release the OLD buffer. The old buffer may still be bound as a vertex buffer in a frame the
    // GPU has not finished (MAX_FRAMES_IN_FLIGHT); freeing it inline is a use-after-free and stalls
    // the driver. B1: hand it to the deferred-reclaim queue (freed after > frames-in-flight). OFF =
    // the old inline free.
    if (s_deferBufferFree) {
        deferBufferFree(device, instanceBuffer, instanceMemory, mappedMemory);
        instanceBuffer = VK_NULL_HANDLE;
        instanceMemory = VK_NULL_HANDLE;
        mappedMemory   = nullptr;
    } else {
        if (mappedMemory) {
            vkUnmapMemory(device, instanceMemory);
            mappedMemory = nullptr;
        }
        if (instanceBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, instanceBuffer, nullptr);
            instanceBuffer = VK_NULL_HANDLE;
        }
        if (instanceMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, instanceMemory, nullptr);
            gpualloc::release();   // keep the live count honest (see GpuAllocStats.h)
            instanceMemory = VK_NULL_HANDLE;
        }
    }

    // Create new larger buffer
    VkDeviceSize bufferSize = elementSize * newCapacity;
    bufferCapacity = newCapacity;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &instanceBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to reallocate chunk instance buffer!");
    }
    
    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, instanceBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    logAllocLimitOnce(physicalDevice);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &instanceMemory) != VK_SUCCESS) {
        logAllocFailure(physicalDevice, allocInfo.allocationSize, "realloc");
        throw std::runtime_error("Failed to allocate reallocated chunk instance buffer memory!");
    }
    noteAlloc(physicalDevice);

    vkBindBufferMemory(device, instanceBuffer, instanceMemory, 0);

    // Map memory persistently
    vkMapMemory(device, instanceMemory, 0, bufferSize, 0, &mappedMemory);
}

void ChunkRenderBuffer::cleanup() {
    // Arena mode: the block's VkBuffer/memory belong to ChunkArenaSystem — never
    // destroy them here. Retire the span (bytes reusable after the margin). The
    // arena may already be shut down at teardown; spans die with the allocator.
    if (m_arenaMode) {
        auto& arena = ChunkArenaSystem::instance();
        if (arena.initialized() && m_span.valid()) {
            arena.allocator()->retire(m_span);
        }
        m_span = {};
        m_arenaMode = false;
        instanceBuffer = VK_NULL_HANDLE;
        instanceMemory = VK_NULL_HANDLE;
        mappedMemory = nullptr;
        bufferCapacity = 0;
        return;
    }

    if (device != VK_NULL_HANDLE) {
        if (mappedMemory) {
            vkUnmapMemory(device, instanceMemory);
            mappedMemory = nullptr;
        }
        if (instanceBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, instanceBuffer, nullptr);
            instanceBuffer = VK_NULL_HANDLE;
        }
        if (instanceMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, instanceMemory, nullptr);
            gpualloc::release();   // keep the live count honest (see GpuAllocStats.h)
            instanceMemory = VK_NULL_HANDLE;
        }
    }
}

void ChunkRenderBuffer::updateMaxUsage(size_t currentUsage) {
    if (currentUsage > maxInstancesUsed) {
        maxInstancesUsed = currentUsage;
    }
}

void ChunkRenderBuffer::logUtilization(size_t currentFaceCount) const {
    if (bufferCapacity > 0) {
        float utilization = float(maxInstancesUsed) / float(bufferCapacity) * 100.0f;
        float currentUtilization = float(currentFaceCount) / float(bufferCapacity) * 100.0f;
        
        // std::cout << "[CHUNK] Buffer utilization - Current: " << currentUtilization 
        //           << "% (" << currentFaceCount << "/" << bufferCapacity 
        //           << "), Peak: " << utilization 
        //           << "% (" << maxInstancesUsed << "/" << bufferCapacity << ")" << std::endl;
    }
}

uint32_t ChunkRenderBuffer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type!");
}

} // namespace Graphics
} // namespace Phyxel
