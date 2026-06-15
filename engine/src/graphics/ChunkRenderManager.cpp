#include "graphics/ChunkRenderManager.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "core/MaterialRegistry.h"
#include "core/Types.h"
#include "utils/Logger.h"
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace Phyxel {
namespace Graphics {

ChunkRenderManager::ChunkRenderManager()
    : numInstances(0)
    , needsUpdate(false)
    , renderBuffer(VK_NULL_HANDLE, VK_NULL_HANDLE)
    , device(VK_NULL_HANDLE)
    , physicalDevice(VK_NULL_HANDLE)
{
    faces.reserve(32 * 32 * 32 * 6); // Reserve for maximum faces
}

ChunkRenderManager::~ChunkRenderManager() {
    cleanupVulkanResources();
}

ChunkRenderManager::ChunkRenderManager(ChunkRenderManager&& other) noexcept
    : faces(std::move(other.faces))
    , numInstances(other.numInstances)
    , needsUpdate(other.needsUpdate)
    , renderBuffer(std::move(other.renderBuffer))
    , device(other.device)
    , physicalDevice(other.physicalDevice)
{
    other.numInstances = 0;
    other.needsUpdate = false;
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
}

ChunkRenderManager& ChunkRenderManager::operator=(ChunkRenderManager&& other) noexcept {
    if (this != &other) {
        cleanupVulkanResources();
        
        faces = std::move(other.faces);
        numInstances = other.numInstances;
        needsUpdate = other.needsUpdate;
        renderBuffer = std::move(other.renderBuffer);
        device = other.device;
        physicalDevice = other.physicalDevice;
        
        other.numInstances = 0;
        other.needsUpdate = false;
        other.device = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;
    }
    return *this;
}

void ChunkRenderManager::initialize(VkDevice dev, VkPhysicalDevice physDev) {
    device = dev;
    physicalDevice = physDev;
    renderBuffer = ChunkRenderBuffer(device, physicalDevice);
}

void ChunkRenderManager::rebuildAllFaces(
    const std::vector<std::unique_ptr<Cube>>& cubes,
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const std::vector<std::unique_ptr<Microcube>>& microcubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube,
    int lodStep,
    bool addSkirts)
{
    faces.clear();

    // Rebuild faces for each voxel type. At LOD > full res the sub-voxels (subcubes/
    // microcubes) are skipped — they are sub-pixel at the distances LOD kicks in.
    rebuildCubeFaces(cubes, worldOrigin, getNeighborCube, lodStep, addSkirts);
    if (lodStep <= 1) {
        rebuildSubcubeFaces(subcubes, worldOrigin);
        rebuildMicrocubeFaces(microcubes, worldOrigin);
    }

    numInstances = static_cast<uint32_t>(faces.size());
    needsUpdate = true;
}

void ChunkRenderManager::rebuildCubeFaces(
    const std::vector<std::unique_ptr<Cube>>& cubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube,
    int lodStep,
    bool addSkirts)
{
    // Greedy meshing for cube faces. lodStep > 1 downsamples the chunk to a coarser grid
    // (16^3 / 8^3) before merging — distant-chunk voxel LOD. Faces are emitted in full-res
    // chunk coords (position*lodStep, extent*lodStep) via the variable-size face format.
    constexpr int NF = 32;                  // full-res grid
    if (lodStep < 1) lodStep = 1;
    const int M = NF / lodStep;             // LOD grid dimension (32 / 16 / 8)
    auto idxF = [](int x, int y, int z) { return z + y * 32 + x * 1024; };

    // Per-material face textures + flags (computed once per distinct material in chunk).
    struct MatFace { uint16_t tex[6]; uint16_t reserved; };
    std::unordered_map<std::string, int> matIdByName;
    std::vector<MatFace> matFaces;
    std::vector<uint8_t> solidF(NF * NF * NF, 0);  // full-res: 1 = visible cube occupies cell
    std::vector<int>     matF(NF * NF * NF, -1);   // full-res: index into matFaces

    auto& reg = Phyxel::Core::MaterialRegistry::instance();
    for (size_t ci = 0; ci < cubes.size(); ++ci) {
        const Cube* cube = cubes[ci].get();
        if (!cube || !cube->isVisible()) continue;
        glm::ivec3 p = cube->getPosition();
        if (p.x < 0 || p.x >= NF || p.y < 0 || p.y >= NF || p.z < 0 || p.z >= NF) continue;
        int cell = idxF(p.x, p.y, p.z);
        solidF[cell] = 1;
        const std::string& mname = cube->getMaterialName();
        auto it = matIdByName.find(mname);
        if (it == matIdByName.end()) {
            MatFace mf{};
            for (int f = 0; f < 6; ++f) mf.tex[f] = reg.getTextureIndex(mname, f);
            const auto* md = reg.getMaterial(mname);
            bool em = md && md->emissive;
            bool tr = md && md->alpha < 0.99f;
            bool mi = md && md->isMirror;
            uint16_t qa = tr ? static_cast<uint16_t>(md->alpha * 255.0f) : 255u;
            mf.reserved = static_cast<uint16_t>((em ? 1u : 0u) | (tr ? 2u : 0u) |
                                                (qa << 2u) | (mi ? (1u << 10) : 0u));
            int newId = static_cast<int>(matFaces.size());
            matFaces.push_back(mf);
            matIdByName[mname] = newId;
            matF[cell] = newId;
        } else {
            matF[cell] = it->second;
        }
    }

    // Build the working grid at LOD resolution. For lodStep==1 it is the full grid; for
    // lodStep>1 each LOD cell is the majority of its lodStep^3 block (solidity + material).
    std::vector<uint8_t> solid;
    std::vector<int>     cmat;
    if (lodStep == 1) {
        solid = std::move(solidF);
        cmat  = std::move(matF);
    } else {
        solid.assign(M * M * M, 0);
        cmat.assign(M * M * M, -1);
        std::vector<int> tally(matFaces.size(), 0);
        const int block = lodStep * lodStep * lodStep;
        for (int i = 0; i < M; ++i)
        for (int j = 0; j < M; ++j)
        for (int k = 0; k < M; ++k) {
            int solidCount = 0, bestMat = -1, bestCount = 0;
            std::fill(tally.begin(), tally.end(), 0);
            for (int dx = 0; dx < lodStep; ++dx)
            for (int dy = 0; dy < lodStep; ++dy)
            for (int dz = 0; dz < lodStep; ++dz) {
                int cf = idxF(i * lodStep + dx, j * lodStep + dy, k * lodStep + dz);
                if (solidF[cf]) {
                    ++solidCount;
                    int m = matF[cf];
                    if (m >= 0 && ++tally[m] > bestCount) { bestCount = tally[m]; bestMat = m; }
                }
            }
            if (solidCount * 2 >= block) {       // majority solid → LOD cell is solid
                int li = k + j * M + i * M * M;
                solid[li] = 1;
                cmat[li]  = bestMat;
            }
        }
    }

    auto idxL = [M](int x, int y, int z) { return z + y * M + x * M * M; };

    // Neighbor solidity. At full res (lodStep==1) boundary faces use the cross-chunk lookup.
    // At LOD>1 the chunk boundary is treated as exposed (a boundary shell is drawn) — simple
    // and hole-free; cross-LOD-level seams are a known follow-up (skirts/stitching).
    const bool crossChunk = (lodStep == 1);
    auto neighborSolid = [&](int x, int y, int z) -> bool {
        if (x >= 0 && x < M && y >= 0 && y < M && z >= 0 && z < M)
            return solid[idxL(x, y, z)] != 0;
        if (crossChunk && getNeighborCube) {
            const Cube* nc = getNeighborCube(worldOrigin + glm::ivec3(x, y, z));
            return nc && nc->isVisible();
        }
        return false;  // boundary → face exposed
    };

    // Face direction offsets: 0=+Z, 1=-Z, 2=+X, 3=-X, 4=+Y, 5=-Y
    const int fdx[6] = {0, 0, 1, -1, 0, 0};
    const int fdy[6] = {0, 0, 0, 0, 1, -1};
    const int fdz[6] = {1, -1, 0, 0, 0, 0};

    std::vector<uint8_t> hasFace(M * M);
    std::vector<int>     faceKey(M * M);
    std::vector<int>     faceMat(M * M);
    std::vector<uint8_t> used(M * M);

    // Per face direction, greedy-merge each slice in the (u,v) plane. Axis roles
    // (u = sizeU / vertexID bit0 axis, v = sizeV / bit1 axis):
    //   +Z/-Z: normal=z, u=x, v=y    +X/-X: normal=x, u=z, v=y    +Y/-Y: normal=y, u=x, v=z
    for (int faceID = 0; faceID < 6; ++faceID) {
        for (int s = 0; s < M; ++s) {
            std::fill(hasFace.begin(), hasFace.end(), 0);
            std::fill(used.begin(), used.end(), 0);
            for (int u = 0; u < M; ++u) {
                for (int v = 0; v < M; ++v) {
                    int x, y, z;
                    if (faceID <= 1)      { x = u; y = v; z = s; }
                    else if (faceID <= 3) { x = s; y = v; z = u; }
                    else                  { x = u; y = s; z = v; }
                    int cell = idxL(x, y, z);
                    if (!solid[cell]) continue;
                    if (neighborSolid(x + fdx[faceID], y + fdy[faceID], z + fdz[faceID])) continue;
                    int m = cmat[cell];
                    if (m < 0) continue;
                    int mi = u * M + v;
                    hasFace[mi] = 1;
                    faceKey[mi] = (static_cast<int>(matFaces[m].tex[faceID]) << 16) | matFaces[m].reserved;
                    faceMat[mi] = m;
                }
            }
            for (int u = 0; u < M; ++u) {
                for (int v = 0; v < M; ++v) {
                    int mi = u * M + v;
                    if (!hasFace[mi] || used[mi]) continue;
                    int k = faceKey[mi];
                    int w = 1;
                    while (v + w < M) {
                        int t = u * M + (v + w);
                        if (!hasFace[t] || used[t] || faceKey[t] != k) break;
                        ++w;
                    }
                    int h = 1; bool ok = true;
                    while (u + h < M && ok) {
                        for (int vv = v; vv < v + w; ++vv) {
                            int t = (u + h) * M + vv;
                            if (!hasFace[t] || used[t] || faceKey[t] != k) { ok = false; break; }
                        }
                        if (ok) ++h;
                    }
                    for (int uu = u; uu < u + h; ++uu)
                        for (int vv = v; vv < v + w; ++vv) used[uu * M + vv] = 1;

                    // Rectangle origin (u,v) at slice s; sizeU = h, sizeV = w (LOD cells).
                    // Scale to full-res chunk coordinates by lodStep for the instance.
                    int ox, oy, oz;
                    if (faceID <= 1)      { ox = u; oy = v; oz = s; }
                    else if (faceID <= 3) { ox = s; oy = v; oz = u; }
                    else                  { ox = u; oy = s; oz = v; }
                    const MatFace& mf = matFaces[faceMat[mi]];
                    InstanceData inst;
                    inst.packedData = Phyxel::InstanceDataUtils::packCubeFaceDataSized(
                        ox * lodStep, oy * lodStep, oz * lodStep, faceID,
                        static_cast<uint32_t>(h * lodStep), static_cast<uint32_t>(w * lodStep));
                    inst.textureIndex = mf.tex[faceID];
                    inst.reserved = mf.reserved;
                    faces.push_back(inst);
                }
            }
        }
    }

    // LOD skirts: a full-res chunk culls its boundary faces against neighbors, but a
    // coarser LOD neighbor renders a lower/different surface, leaving see-through cracks.
    // Hang a short vertical "curtain" down from each boundary surface column (both face
    // directions, so it shows from either side) to cover the gap. Only needed at full res
    // (LOD>1 chunks already draw a full boundary shell), and only when LOD is active.
    if (addSkirts && lodStep == 1) {
        const int skirtDepth = 8;  // voxels below the surface to hang the curtain
        auto emitSkirt = [&](int x, int z, int outFace, int inFace) {
            int surfaceY = -1;
            for (int y = M - 1; y >= 0; --y) { if (solid[idxL(x, y, z)]) { surfaceY = y; break; } }
            if (surfaceY < 0) return;
            int top   = surfaceY + 1;
            int oy    = std::max(0, top - skirtDepth);
            int sizeY = top - oy;                 // 1..skirtDepth
            int m = cmat[idxL(x, surfaceY, z)];
            if (sizeY <= 0 || m < 0) return;
            for (int f : {outFace, inFace}) {
                InstanceData inst;
                inst.packedData = Phyxel::InstanceDataUtils::packCubeFaceDataSized(
                    x, oy, z, f, 1u, static_cast<uint32_t>(sizeY));
                inst.textureIndex = matFaces[m].tex[f];
                inst.reserved = matFaces[m].reserved;
                faces.push_back(inst);
            }
        };
        for (int l = 0; l < M; ++l) {
            emitSkirt(0,     l, 3, 2);  // -X boundary (lateral = z)
            emitSkirt(M - 1, l, 2, 3);  // +X boundary
            emitSkirt(l, 0,     1, 0);  // -Z boundary (lateral = x)
            emitSkirt(l, M - 1, 0, 1);  // +Z boundary
        }
    }
}

void ChunkRenderManager::rebuildSubcubeFaces(
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // Process subcubes (from subdivided cubes)
    for (const auto& subcube : subcubes) {
        // Skip broken or hidden subcubes
        if (!subcube || subcube->isBroken() || !subcube->isVisible()) {
            continue;
        }
        
        // Get subcube properties
        glm::ivec3 parentPos = subcube->getPosition();     // Parent cube's world position
        glm::ivec3 localPos = subcube->getLocalPosition(); // 0-2 for each axis within parent
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentChunkPos = parentPos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentChunkPos.x < 0 || parentChunkPos.x >= 32 ||
            parentChunkPos.y < 0 || parentChunkPos.y >= 32 ||
            parentChunkPos.z < 0 || parentChunkPos.z >= 32) {
            continue; // Skip subcubes with invalid parent positions
        }
        
        // For now, assume all subcube faces are visible (can optimize with culling later)
        bool faceVisible[6] = {true, true, true, true, true, true};
        
        // Generate instance data for each visible face of the subcube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack parent cube position, face ID, and subcube local position
                // Scale level 1 = subcube
                faceInstance.packedData = Phyxel::InstanceDataUtils::packSubcubeFaceData(
                    parentChunkPos.x, parentChunkPos.y, parentChunkPos.z,
                    faceID,
                    localPos.x, localPos.y, localPos.z
                );
                
                // Assign texture based on material and face ID
                faceInstance.textureIndex = Phyxel::Core::MaterialRegistry::instance().getTextureIndex(subcube->getMaterialName(), faceID);
                {
                    const auto* matDef = Phyxel::Core::MaterialRegistry::instance().getMaterial(subcube->getMaterialName());
                    bool isEmissive    = matDef && matDef->emissive;
                    bool isTransparent = matDef && matDef->alpha < 0.99f;
                    bool isMirror      = matDef && matDef->isMirror;
                    uint16_t quantAlpha = isTransparent ? static_cast<uint16_t>(matDef->alpha * 255.0f) : 255u;
                    faceInstance.reserved = static_cast<uint16_t>(
                        (isEmissive ? 1u : 0u) | (isTransparent ? 2u : 0u) | (quantAlpha << 2u) | (isMirror ? (1u << 10) : 0u));
                }
                faces.push_back(faceInstance);
            }
        }
    }
}

void ChunkRenderManager::rebuildMicrocubeFaces(
    const std::vector<std::unique_ptr<Microcube>>& microcubes,
    const glm::ivec3& worldOrigin)
{
    // Process microcubes (from subdivided subcubes)
    for (const auto& microcube : microcubes) {
        // Skip broken or hidden microcubes
        if (!microcube || microcube->isBroken() || !microcube->isVisible()) {
            continue;
        }
        
        // Get microcube properties
        glm::ivec3 parentPos = microcube->getParentCubePosition();     // Parent cube's world position
        glm::ivec3 subcubePos = microcube->getSubcubeLocalPosition();  // 0-2 for each axis within parent cube
        glm::ivec3 microcubePos = microcube->getMicrocubeLocalPosition(); // 0-2 for each axis within parent subcube
        
        // Convert parent world position to chunk-relative position
        glm::ivec3 parentChunkPos = parentPos - worldOrigin;
        
        // Validate parent position is within chunk bounds
        if (parentChunkPos.x < 0 || parentChunkPos.x >= 32 ||
            parentChunkPos.y < 0 || parentChunkPos.y >= 32 ||
            parentChunkPos.z < 0 || parentChunkPos.z >= 32) {
            continue;
        }
        
        // Validate subcube position
        if (subcubePos.x < 0 || subcubePos.x >= 3 ||
            subcubePos.y < 0 || subcubePos.y >= 3 ||
            subcubePos.z < 0 || subcubePos.z >= 3) {
            continue;
        }
        
        // Validate microcube position
        if (microcubePos.x < 0 || microcubePos.x >= 3 ||
            microcubePos.y < 0 || microcubePos.y >= 3 ||
            microcubePos.z < 0 || microcubePos.z >= 3) {
            continue;
        }
        
        // For now, assume all microcube faces are visible (can optimize with culling later)
        bool faceVisible[6] = {true, true, true, true, true, true};
        
        // Generate instance data for each visible face of the microcube
        for (int faceID = 0; faceID < 6; ++faceID) {
            if (faceVisible[faceID]) {
                InstanceData faceInstance;
                
                // Pack parent cube position, face ID, subcube position, and microcube position
                // Scale level 2 = microcube
                faceInstance.packedData = Phyxel::InstanceDataUtils::packMicrocubeFaceData(
                    parentChunkPos.x, parentChunkPos.y, parentChunkPos.z,
                    faceID,
                    subcubePos.x, subcubePos.y, subcubePos.z,
                    microcubePos.x, microcubePos.y, microcubePos.z
                );
                
                // Assign texture based on material and face ID
                faceInstance.textureIndex = Phyxel::Core::MaterialRegistry::instance().getTextureIndex(microcube->getMaterialName(), faceID);
                {
                    const auto* matDef = Phyxel::Core::MaterialRegistry::instance().getMaterial(microcube->getMaterialName());
                    bool isEmissive    = matDef && matDef->emissive;
                    bool isTransparent = matDef && matDef->alpha < 0.99f;
                    bool isMirror      = matDef && matDef->isMirror;
                    uint16_t quantAlpha = isTransparent ? static_cast<uint16_t>(matDef->alpha * 255.0f) : 255u;
                    faceInstance.reserved = static_cast<uint16_t>(
                        (isEmissive ? 1u : 0u) | (isTransparent ? 2u : 0u) | (quantAlpha << 2u) | (isMirror ? (1u << 10) : 0u));
                }
                faces.push_back(faceInstance);
            }
        }
    }
}

void ChunkRenderManager::updateVulkanBuffer() {
    if (faces.empty()) return;
    if (!renderBuffer.getMappedMemory()) return;

    // ensureBufferCapacity may call reallocateBuffer() which remaps memory —
    // fetch the pointer AFTER this call so we never write to a freed mapping.
    ensureBufferCapacity(faces.size());

    void* mappedMem = renderBuffer.getMappedMemory();
    if (!mappedMem) return;

    // Track peak usage for analysis
    renderBuffer.updateMaxUsage(faces.size());

    // Copy data to GPU buffer (only the used portion)
    VkDeviceSize copySize = sizeof(InstanceData) * faces.size();
    memcpy(mappedMem, faces.data(), copySize);
    needsUpdate = false;
    
    // Periodic utilization logging
    static int updateCount = 0;
    if (++updateCount % 50 == 0) {
        logBufferUtilization();
    }
}

void ChunkRenderManager::updateSingleCubeTexture(
    const glm::ivec3& localPos,
    uint16_t textureIndex,
    const std::vector<std::unique_ptr<Cube>>& cubes)
{
    // Find the cube
    const Cube* cube = getCubeAtPosition(localPos, cubes);
    if (!cube) return;
    
    // Efficiently update only the affected faces in the buffer
    if (!renderBuffer.getMappedMemory()) return;
    
    bool updatedAnyFaces = false;
    
    // Find all face instances for this cube and update their texture indices
    for (size_t i = 0; i < faces.size(); ++i) {
        InstanceData& face = faces[i];
        
        // Extract position from packed data
        int faceX = face.packedData & 0x1F;
        int faceY = (face.packedData >> 5) & 0x1F;
        int faceZ = (face.packedData >> 10) & 0x1F;
        
        // Check if this face belongs to our cube
        if (faceX == localPos.x && faceY == localPos.y && faceZ == localPos.z) {
            // Update the texture index in the faces vector
            faces[i].textureIndex = textureIndex;
            
            // Update the GPU buffer directly (partial update)
            VkDeviceSize offset = i * sizeof(InstanceData) + offsetof(InstanceData, textureIndex);
            memcpy(static_cast<char*>(renderBuffer.getMappedMemory()) + offset, &textureIndex, sizeof(uint16_t));
            
            updatedAnyFaces = true;
        }
    }
}

void ChunkRenderManager::updateSingleSubcubeTexture(
    const glm::ivec3& parentLocalPos,
    const glm::ivec3& subcubePos,
    uint16_t textureIndex,
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // Validate positions
    if (subcubePos.x < 0 || subcubePos.x >= 3 || 
        subcubePos.y < 0 || subcubePos.y >= 3 || 
        subcubePos.z < 0 || subcubePos.z >= 3) return;
    
    // Efficiently update only the affected faces in the buffer
    if (!renderBuffer.getMappedMemory()) return;
    
    bool updatedAnyFaces = false;
    
    // Find all face instances for this subcube and update their texture indices
    for (size_t i = 0; i < faces.size(); ++i) {
        InstanceData& face = faces[i];
        
        // Extract data from packed format for subcubes
        int parentX = face.packedData & 0x1F;
        int parentY = (face.packedData >> 5) & 0x1F;
        int parentZ = (face.packedData >> 10) & 0x1F;
        uint32_t subcubeData = (face.packedData >> 18);
        bool isSubcubeFace = (subcubeData & 0x1) != 0;
        
        // Check if this is a subcube face belonging to our specific subcube
        if (isSubcubeFace && 
            parentX == parentLocalPos.x && parentY == parentLocalPos.y && parentZ == parentLocalPos.z) {
            
            // Extract subcube local position from packed data
            int localX = (subcubeData >> 1) & 0x3;
            int localY = (subcubeData >> 3) & 0x3;
            int localZ = (subcubeData >> 5) & 0x3;
            
            // Check if this face belongs to our specific subcube
            if (localX == subcubePos.x && localY == subcubePos.y && localZ == subcubePos.z) {
                // Update the texture index in the faces vector
                faces[i].textureIndex = textureIndex;
                
                // Update the GPU buffer directly (partial update)
                VkDeviceSize offset = i * sizeof(InstanceData) + offsetof(InstanceData, textureIndex);
                memcpy(static_cast<char*>(renderBuffer.getMappedMemory()) + offset, &textureIndex, sizeof(uint16_t));
                
                updatedAnyFaces = true;
            }
        }
    }
}

void ChunkRenderManager::updateSingleCubeColor(
    const glm::ivec3& localPos,
    const glm::vec3& newColor,
    const std::vector<std::unique_ptr<Cube>>& cubes)
{
    // Color updates would require rebuilding faces since colors are baked into vertex data
    // For now, this is a placeholder - actual implementation depends on rendering architecture
    // TODO: Implement color updates if needed
}

void ChunkRenderManager::updateSingleSubcubeColor(
    const glm::ivec3& localPos,
    const glm::ivec3& subcubePos,
    const glm::vec3& newColor,
    const std::vector<std::unique_ptr<Subcube>>& subcubes,
    const glm::ivec3& worldOrigin)
{
    // Color updates would require rebuilding faces since colors are baked into vertex data
    // For now, this is a placeholder - actual implementation depends on rendering architecture
    // TODO: Implement color updates if needed
}

void ChunkRenderManager::createVulkanBuffer() {
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("ChunkRenderManager::createVulkanBuffer() called before initialize()!");
    }
    renderBuffer.createBuffer(faces);
}

void ChunkRenderManager::cleanupVulkanResources() {
    renderBuffer.cleanup();
}

void ChunkRenderManager::ensureBufferCapacity(size_t requiredInstances) {
    if (requiredInstances > renderBuffer.getCapacity()) {
        renderBuffer.reallocateBuffer(requiredInstances);
    }
}

void ChunkRenderManager::logBufferUtilization() const {
    renderBuffer.logUtilization(faces.size());
}

// Helper methods

bool ChunkRenderManager::isCubeFaceVisible(
    const glm::ivec3& cubePos,
    int faceID,
    const std::vector<std::unique_ptr<Cube>>& cubes,
    const glm::ivec3& worldOrigin,
    const NeighborLookupFunc& getNeighborCube) const
{
    // This is a helper that could be used for more sophisticated culling
    // For now, it's not used, but kept for potential future optimization
    return true;
}

const Cube* ChunkRenderManager::getCubeAtPosition(
    const glm::ivec3& localPos,
    const std::vector<std::unique_ptr<Cube>>& cubes) const
{
    // PERFORMANCE CRITICAL: Use indexed lookup - cubes vector is arranged in X-major order
    // Index formula: z + y*32 + x*32*32 (must match Chunk::localToIndex)
    // DO NOT use linear search - with 32K cubes × 6 faces × N chunks = billions of lookups!
    if (localPos.x < 0 || localPos.x >= 32 ||
        localPos.y < 0 || localPos.y >= 32 ||
        localPos.z < 0 || localPos.z >= 32) {
        return nullptr;
    }
    
    size_t index = localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
    if (index >= cubes.size()) {
        return nullptr;
    }
    
    return cubes[index].get();  // Could be nullptr for deleted cubes
}

} // namespace Graphics
} // namespace Phyxel
