#include "core/LodPyramidService.h"

#include "core/Chunk.h"
#include "core/LodBlobCodec.h"
#include "core/LodChunkMesh.h"
#include "core/WorldStorage.h"

namespace Phyxel {
namespace Core {

int LodPyramidService::buildAndPersist(const Chunk& chunk, WorldStorage& storage, int maxLevel) {
    if (maxLevel < 1) return 0;
    if (!shouldPersist(chunk)) return 0;

    std::vector<std::string> palette;
    LodVolume v = LodChunkMesh::volumeFromChunk(chunk, &palette);

    const glm::ivec3 coord = chunk.getWorldOrigin() / 32;
    int written = 0;
    for (int lod = 1; lod <= maxLevel; ++lod) {
        // Stop once the volume is a single cell -- squashing further cannot add information,
        // and storing the same 1-cell level repeatedly is pure waste.
        if (v.dim().x <= 1 && v.dim().y <= 1 && v.dim().z <= 1) break;
        v = squash(v, SquashConfig{});
        if (!storage.saveLodBlob(coord, lod, LodBlobCodec::encode(v, palette))) break;
        ++written;
    }
    return written;
}

bool LodPyramidService::invalidate(WorldStorage& storage, const glm::ivec3& chunkCoord) {
    return storage.deleteLodBlobs(chunkCoord);
}

bool LodPyramidService::facesFromStorage(WorldStorage& storage, const glm::ivec3& chunkCoord,
                                         int lod, std::vector<InstanceData>& outFaces) {
    outFaces.clear();
    std::vector<uint8_t> blob;
    if (!storage.loadLodBlob(chunkCoord, lod, blob)) return false;

    LodVolume volume;
    std::vector<std::string> palette;
    // A corrupt row must not reach the renderer. decode() already refuses malformed input; this
    // just makes the failure visible as "no geometry" rather than a half-built volume.
    if (!LodBlobCodec::decode(blob.data(), blob.size(), volume, palette)) return false;

    LodChunkMesh::emitFaces(volume, palette, outFaces);
    return true;
}

bool LodPyramidService::shouldPersist(const Chunk& chunk) {
    // See the header for why sub/micro content is the signal, and what it over-counts.
    return chunk.getStaticSubcubeCount() > 0 || chunk.getStaticMicrocubeCount() > 0;
}

bool LodPyramidService::measureServeCost(WorldStorage& storage, const glm::ivec3& chunkCoord,
                                         int lod, ServeCost& out) {
    out = ServeCost{};
    std::vector<uint8_t> blob;
    if (!storage.loadLodBlob(chunkCoord, lod, blob)) return false;
    out.blobBytes = blob.size();

    LodVolume volume;
    std::vector<std::string> palette;
    if (!LodBlobCodec::decode(blob.data(), blob.size(), volume, palette)) return false;

    const glm::ivec3 d = volume.dim();
    out.volumeBytes = size_t(d.x) * size_t(d.y) * size_t(d.z) * sizeof(LodCell);

    std::vector<InstanceData> faces;
    LodChunkMesh::emitFaces(volume, palette, faces);
    out.faceCount = faces.size();
    out.faceBytes = faces.size() * sizeof(InstanceData);
    return true;
}

} // namespace Core
} // namespace Phyxel
