#include "graphics/TreeLodMeshRegistry.h"

#include "graphics/TreeSpeciesTable.h"
#include "core/PlacedObjectManager.h"   // full InteractionPointDef for VoxelTemplate's vector
#include "core/VoxelTemplate.h"
#include "utils/Logger.h"

#include <chrono>
#include <unordered_set>

namespace Phyxel {
namespace Graphics {

namespace {
inline uint64_t key3(const glm::ivec3& p) {
    constexpr int kOff = 1 << 20;
    return (uint64_t(uint32_t(p.x + kOff)) << 42) |
           (uint64_t(uint32_t(p.y + kOff)) << 21) |
            uint64_t(uint32_t(p.z + kOff));
}
} // namespace

glm::vec3 TreeLodMeshRegistry::stampAnchorFor(const VoxelTemplate& t) {
    // Mirror of decorateChunk's centering (base = worldPos - maxExtent/2, integer halves —
    // same rule as ObjectTemplateManager::templateFootprintRadius) shifted by the instance's
    // column-center placement (+0.5). Using anything else — e.g. centering each level's own
    // cell bbox — puts the LOD mesh half a footprint off the stamped tree.
    glm::ivec3 mx(0);
    for (const auto& c : t.cubes)      mx = glm::max(mx, c.relativePos);
    for (const auto& s : t.subcubes)   mx = glm::max(mx, s.parentRelativePos);
    for (const auto& m : t.microcubes) mx = glm::max(mx, m.parentRelativePos);
    return glm::vec3(-float(mx.x / 2) - 0.5f, 0.0f, -float(mx.z / 2) - 0.5f);
}

TreeLodMeshRegistry::CpuMesh TreeLodMeshRegistry::buildLevelMesh(
    const Core::TemplateLodChain::Level& level, const FarMaterialResolver& resolveTex,
    const glm::vec3& anchor) {
    CpuMesh mesh;
    if (level.cells.empty() || level.cellSizeMicros <= 0) return mesh;

    std::unordered_set<uint64_t> occ;
    occ.reserve(level.cells.size() * 2);
    for (const auto& c : level.cells) occ.insert(key3(c.pos));

    const float s = float(level.cellSizeMicros) / 9.0f;   // cell edge, world units

    // Face tables matching FarVertex faceID: 0=+Z 1=-Z 2=+X 3=-X 4=+Y 5=-Y.
    static const glm::ivec3 kDir[6] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};

    auto emitQuad = [&](const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                        const glm::vec3& v3, uint32_t faceID, uint16_t tex) {
        const uint32_t base = uint32_t(mesh.vertices.size());
        const uint32_t packed = packFarVertex(tex, faceID);
        mesh.vertices.push_back({v0, packed});
        mesh.vertices.push_back({v1, packed});
        mesh.vertices.push_back({v2, packed});
        mesh.vertices.push_back({v3, packed});
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    };

    for (const auto& c : level.cells) {
        const uint16_t tex = resolveTex(c.material, 0);
        const glm::vec3 lo = glm::vec3(c.pos) * s + anchor;
        const glm::vec3 hi = lo + glm::vec3(s);
        for (int f = 0; f < 6; ++f) {
            if (occ.count(key3(c.pos + kDir[f]))) continue;   // interior face — culled
            switch (f) {
                case 0: emitQuad({hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, lo.y, hi.z}, 0, tex); break;
                case 1: emitQuad({lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, lo.y, lo.z}, 1, tex); break;
                case 2: emitQuad({hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, 2, tex); break;
                case 3: emitQuad({lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}, {lo.x, lo.y, lo.z}, 3, tex); break;
                case 4: emitQuad({lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, 4, tex); break;
                case 5: emitQuad({lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}, 5, tex); break;
            }
        }
    }
    return mesh;
}

TreeLodMeshRegistry::TreeLodMeshRegistry(VkDevice device, VkPhysicalDevice physicalDevice)
    : m_device(device), m_physicalDevice(physicalDevice) {}

TreeLodMeshRegistry::~TreeLodMeshRegistry() { cleanup(); }

void TreeLodMeshRegistry::stopBuilder() {
    if (!m_builder.joinable()) return;
    m_stop = true;
    m_queueCv.notify_all();
    m_builder.join();
}

void TreeLodMeshRegistry::cleanup() {
    stopBuilder();
    if (m_device == VK_NULL_HANDLE) return;
    for (auto& [id, sm] : m_species) {
        for (auto& l : sm.levels) {
            if (l.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, l.vertexBuffer, nullptr);
            if (l.vertexMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, l.vertexMemory, nullptr);
            if (l.indexBuffer  != VK_NULL_HANDLE) vkDestroyBuffer(m_device, l.indexBuffer, nullptr);
            if (l.indexMemory  != VK_NULL_HANDLE) vkFreeMemory(m_device, l.indexMemory, nullptr);
            l = GpuLevel{};
        }
    }
    m_species.clear();
}

const TreeLodMeshRegistry::GpuLevel* TreeLodMeshRegistry::level(int speciesId, int levelIdx) {
    if (levelIdx < 0 || levelIdx >= Core::TemplateLodChain::kLevelCount) return nullptr;
    auto it = m_species.find(speciesId);
    if (it == m_species.end()) {
        // Not built: queue for the background builder (once) and let cards cover meanwhile.
        if (ready() && !m_queued.count(speciesId)) {
            m_queued.insert(speciesId);
            if (!m_builder.joinable()) {
                m_stop = false;
                m_builder = std::thread([this] { builderLoop(); });
            }
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_buildQueue.push_back(speciesId);
            }
            m_queueCv.notify_one();
        }
        return nullptr;
    }
    const GpuLevel& l = it->second.levels[size_t(levelIdx)];
    return (l.vertexBuffer != VK_NULL_HANDLE && l.indexCount > 0) ? &l : nullptr;
}

void TreeLodMeshRegistry::builderLoop() {
    for (;;) {
        int speciesId = -1;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this] { return m_stop || !m_buildQueue.empty(); });
            if (m_stop) return;
            speciesId = m_buildQueue.front();
            m_buildQueue.pop_front();
        }

        BuiltSpecies out;
        out.speciesId = speciesId;

        size_t n = 0;
        const TreeSpecies* rows = treeSpeciesTable(n);
        if (speciesId >= 0 && size_t(speciesId) < n) {
            const VoxelTemplate* tpl = m_provider(rows[speciesId].meshTemplate);
            if (tpl) {
                const auto t0 = std::chrono::steady_clock::now();
                const auto chain = Core::TemplateLodChain::build(*tpl);
                const glm::vec3 anchor = stampAnchorFor(*tpl);
                size_t totalCells = 0, totalTris = 0;
                for (int li = 0; li < Core::TemplateLodChain::kLevelCount &&
                                 li < int(chain.size()); ++li) {
                    out.levels[size_t(li)] =
                        buildLevelMesh(chain[size_t(li)], m_resolveTex, anchor);
                    totalCells += chain[size_t(li)].cells.size();
                    totalTris += out.levels[size_t(li)].indices.size() / 3;
                }
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0).count();
                LOG_INFO("TreeLodMesh", "Species {} ('{}') LOD set built off-thread: {} cells / {} tris ({}ms)",
                         speciesId, rows[speciesId].meshTemplate, totalCells, totalTris, int(ms));
            } else {
                LOG_WARN("TreeLodMesh", "No template '{}' for species {} — its far trees stay cards",
                         rows[speciesId].meshTemplate, speciesId);
            }
        }

        std::lock_guard<std::mutex> lock(m_doneMutex);
        m_done.push_back(std::move(out));
    }
}

bool TreeLodMeshRegistry::uploadLevel(const CpuMesh& cpu, GpuLevel& out) {
    if (cpu.indices.empty()) return false;
    if (!createHostBuffer(cpu.vertices.size() * sizeof(FarVertex),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, cpu.vertices.data(),
                          out.vertexBuffer, out.vertexMemory) ||
        !createHostBuffer(cpu.indices.size() * sizeof(uint32_t),
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT, cpu.indices.data(),
                          out.indexBuffer, out.indexMemory)) {
        out = GpuLevel{};
        return false;
    }
    out.indexCount = uint32_t(cpu.indices.size());
    return true;
}

bool TreeLodMeshRegistry::createInstanceBuffer(const FarTreeInstance& inst, VkBuffer& buf,
                                               VkDeviceMemory& mem) {
    return createHostBuffer(sizeof(FarTreeInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            &inst, buf, mem);
}

void TreeLodMeshRegistry::destroyBuffer(VkBuffer buf, VkDeviceMemory mem) {
    if (m_device == VK_NULL_HANDLE) return;
    if (buf != VK_NULL_HANDLE) vkDestroyBuffer(m_device, buf, nullptr);
    if (mem != VK_NULL_HANDLE) vkFreeMemory(m_device, mem, nullptr);
}

void TreeLodMeshRegistry::tick() {
    // Main thread: finalize at most ONE finished species per frame — uploads are host-visible
    // memcpys of a few hundred KB, but bounding them keeps worst-case frames flat.
    BuiltSpecies built;
    {
        std::lock_guard<std::mutex> lock(m_doneMutex);
        if (m_done.empty()) return;
        built = std::move(m_done.back());
        m_done.pop_back();
    }

    SpeciesMeshes sm;
    sm.built = true;
    for (int li = 0; li < Core::TemplateLodChain::kLevelCount; ++li) {
        const CpuMesh& cpu = built.levels[size_t(li)];
        if (cpu.indices.empty()) continue;
        GpuLevel& gl = sm.levels[size_t(li)];
        if (!createHostBuffer(cpu.vertices.size() * sizeof(FarVertex),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, cpu.vertices.data(),
                              gl.vertexBuffer, gl.vertexMemory) ||
            !createHostBuffer(cpu.indices.size() * sizeof(uint32_t),
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT, cpu.indices.data(),
                              gl.indexBuffer, gl.indexMemory)) {
            LOG_WARN("TreeLodMesh", "GPU alloc failed for species {} level {}",
                     built.speciesId, li);
            gl = GpuLevel{};
            continue;
        }
        gl.indexCount = uint32_t(cpu.indices.size());
    }
    m_species.emplace(built.speciesId, std::move(sm));
}

bool TreeLodMeshRegistry::createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                           const void* data, VkBuffer& buffer,
                                           VkDeviceMemory& memory) {
    if (size == 0 || m_device == VK_NULL_HANDLE) return false;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, buffer, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    uint32_t typeIndex = UINT32_MAX;
    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memReq.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & want) == want) {
            typeIndex = i;
            break;
        }
    }
    if (typeIndex == UINT32_MAX) { vkDestroyBuffer(m_device, buffer, nullptr); buffer = VK_NULL_HANDLE; return false; }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = typeIndex;
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(m_device, buffer, memory, 0);

    void* mapped = nullptr;
    vkMapMemory(m_device, memory, 0, size, 0, &mapped);
    memcpy(mapped, data, size_t(size));
    vkUnmapMemory(m_device, memory);
    return true;
}

} // namespace Graphics
} // namespace Phyxel
