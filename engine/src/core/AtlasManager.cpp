#include "core/AtlasManager.h"
#include "core/MaterialRegistry.h"
#include "vulkan/VulkanDevice.h"
#include "utils/Logger.h"

#include "stb_image.h"
#include "stb_image_write.h"
#include "bc7enc.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <atomic>
#include <filesystem>
#include <fstream>

namespace Phyxel {
namespace Core {

AtlasManager::AtlasManager() = default;
AtlasManager::~AtlasManager() = default;

void AtlasManager::calcAtlasDimensions(int textureCount, int& outWidth, int& outHeight) {
    // Rows needed: ceil(textureCount / TEXTURES_PER_ROW)
    int rows = (textureCount + TEXTURES_PER_ROW - 1) / TEXTURES_PER_ROW;
    outWidth  = TEXTURES_PER_ROW * CELL_SIZE; // 6 * 20 = 120 minimum
    outHeight = rows * CELL_SIZE;

    // Round up to power of 2 for GPU friendliness
    auto nextPow2 = [](int v) -> int {
        int p = 1;
        while (p < v) p <<= 1;
        return p;
    };
    int maxDim = std::max(outWidth, outHeight);
    int atlasSize = nextPow2(maxDim);
    // Clamp to reasonable minimum
    if (atlasSize < 128) atlasSize = 128;
    if (atlasSize > MAX_ATLAS_SIZE) atlasSize = MAX_ATLAS_SIZE;
    outWidth = atlasSize;
    outHeight = atlasSize;
}

std::vector<uint8_t> AtlasManager::loadPNG(const std::string& path) const {
    int w, h, channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!data) {
        return {};
    }

    std::vector<uint8_t> result(static_cast<size_t>(TEXTURE_SIZE) * TEXTURE_SIZE * 4, 0);

    if (w == TEXTURE_SIZE && h == TEXTURE_SIZE) {
        memcpy(result.data(), data, result.size());
    } else {
        // Bilinear-resample any source resolution to the array layer size. Lets low-res
        // hand-authored sources coexist with native high-res (e.g. CC0 512px) tiles.
        auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            float sy = (y + 0.5f) * h / TEXTURE_SIZE - 0.5f;
            int y0 = static_cast<int>(std::floor(sy));
            float fy = sy - y0;
            int y0c = clampi(y0, 0, h - 1), y1c = clampi(y0 + 1, 0, h - 1);
            for (int x = 0; x < TEXTURE_SIZE; x++) {
                float sx = (x + 0.5f) * w / TEXTURE_SIZE - 0.5f;
                int x0 = static_cast<int>(std::floor(sx));
                float fx = sx - x0;
                int x0c = clampi(x0, 0, w - 1), x1c = clampi(x0 + 1, 0, w - 1);
                for (int c = 0; c < 4; c++) {
                    float p00 = data[(y0c * w + x0c) * 4 + c];
                    float p10 = data[(y0c * w + x1c) * 4 + c];
                    float p01 = data[(y1c * w + x0c) * 4 + c];
                    float p11 = data[(y1c * w + x1c) * 4 + c];
                    float top = p00 + (p10 - p00) * fx;
                    float bot = p01 + (p11 - p01) * fx;
                    result[(y * TEXTURE_SIZE + x) * 4 + c] =
                        static_cast<uint8_t>(top + (bot - top) * fy + 0.5f);
                }
            }
        }
    }

    stbi_image_free(data);
    return result;
}

std::vector<uint8_t> AtlasManager::generateFallbackTexture(int slotIndex) const {
    std::vector<uint8_t> pixels(TEXTURE_SIZE * TEXTURE_SIZE * 4);
    // Magenta/black checkerboard
    for (int y = 0; y < TEXTURE_SIZE; y++) {
        for (int x = 0; x < TEXTURE_SIZE; x++) {
            int idx = (y * TEXTURE_SIZE + x) * 4;
            bool checker = ((x / 4) + (y / 4)) % 2;
            pixels[idx + 0] = checker ? 255 : 0;    // R
            pixels[idx + 1] = 0;                     // G
            pixels[idx + 2] = checker ? 255 : 0;     // B
            pixels[idx + 3] = 255;                    // A
        }
    }
    return pixels;
}

void AtlasManager::blitToAtlas(int slotIndex, const uint8_t* texPixels) {
    // Layer-major: each texture occupies one contiguous TEXTURE_SIZE² RGBA block,
    // indexed by slotIndex (== texture array layer == textureIndex).
    const size_t layerBytes = static_cast<size_t>(TEXTURE_SIZE) * TEXTURE_SIZE * 4;
    const size_t dstOffset = static_cast<size_t>(slotIndex) * layerBytes;
    if (dstOffset + layerBytes <= atlasInfo_.pixels.size()) {
        memcpy(atlasInfo_.pixels.data() + dstOffset, texPixels, layerBytes);
    }
}

bool AtlasManager::buildAtlas() {
    auto& registry = MaterialRegistry::instance();
    int textureCount = registry.getTextureCount();
    if (textureCount == 0) {
        LOG_ERROR("AtlasManager", "No textures in MaterialRegistry");
        return false;
    }

    // Texture array: one TEXTURE_SIZE² RGBA layer per texture, stored layer-major.
    // layer index == textureIndex == MaterialRegistry atlas index.
    const size_t layerBytes = static_cast<size_t>(TEXTURE_SIZE) * TEXTURE_SIZE * 4;
    atlasInfo_.atlasWidth = TEXTURE_SIZE;
    atlasInfo_.atlasHeight = TEXTURE_SIZE;
    atlasInfo_.textureCount = textureCount;
    atlasInfo_.layerCount = textureCount;
    atlasInfo_.pixels.assign(layerBytes * textureCount, 0); // clear to transparent black

    // uvBounds is no longer used for sampling (each layer is a full 0..1 tile), but the
    // fragment shader still reads textureCount/fallbackIndex from the same SSBO, so we keep
    // a full-tile entry per layer.
    atlasInfo_.uvBounds.assign(textureCount, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

    // Iterate all materials and all faces, load each source PNG into its layer.
    const auto& materials = registry.getAllMaterials();
    for (const auto& mat : materials) {
        int matID = registry.getMaterialID(mat.name);
        if (matID < 0) continue;

        const std::string faceFiles[6] = {
            mat.textures.sideN(), mat.textures.sideS(),
            mat.textures.sideE(), mat.textures.sideW(),
            mat.textures.top(), mat.textures.bottom()
        };

        for (int faceID = 0; faceID < 6; faceID++) {
            uint16_t atlasIdx = registry.getTextureIndex(matID, faceID);
            if (atlasIdx == MaterialRegistry::INVALID_TEXTURE_INDEX) continue;

            std::string path = sourceDirectory_ + "/" + faceFiles[faceID];
            auto texPixels = loadPNG(path);
            if (texPixels.empty()) {
                LOG_WARN("AtlasManager", "Missing texture: {}, using fallback", path);
                texPixels = generateFallbackTexture(atlasIdx);
            }

            blitToAtlas(atlasIdx, texPixels.data()); // blit into layer
        }
    }

    LOG_INFO("AtlasManager", "Built texture array: {} layers @ {}x{} from {} materials",
             textureCount, TEXTURE_SIZE, TEXTURE_SIZE, materials.size());
    return true;
}

bool AtlasManager::updateTextureSlot(int slotIndex, const uint8_t* pixels) {
    if (slotIndex < 0 || slotIndex >= atlasInfo_.textureCount) return false;
    if (!pixels) return false;
    blitToAtlas(slotIndex, pixels);
    return true;
}

namespace {
// Box-downsample an RGBA image (srcW x srcH) to (dstW x dstH), averaging the covered source
// texels per destination texel. Used for CPU mip generation before BC7 encode.
void downsampleRGBA(const uint8_t* src, int srcW, int srcH,
                    uint8_t* dst, int dstW, int dstH) {
    for (int y = 0; y < dstH; y++) {
        int sy0 = y * srcH / dstH, sy1 = std::max(sy0 + 1, (y + 1) * srcH / dstH);
        for (int x = 0; x < dstW; x++) {
            int sx0 = x * srcW / dstW, sx1 = std::max(sx0 + 1, (x + 1) * srcW / dstW);
            int acc[4] = {0, 0, 0, 0}, n = 0;
            for (int sy = sy0; sy < sy1; sy++)
                for (int sx = sx0; sx < sx1; sx++) {
                    const uint8_t* p = src + (sy * srcW + sx) * 4;
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3]; n++;
                }
            uint8_t* d = dst + (y * dstW + x) * 4;
            for (int c = 0; c < 4; c++) d[c] = static_cast<uint8_t>(acc[c] / std::max(1, n));
        }
    }
}
} // namespace

bool AtlasManager::encodeBC7() {
    if (atlasInfo_.pixels.empty() || atlasInfo_.layerCount <= 0) return false;

    static std::atomic<bool> s_bc7Init{false};
    if (!s_bc7Init.exchange(true)) {
        bc7enc_compress_block_init();
    }

    const int base = TEXTURE_SIZE;
    const int layers = atlasInfo_.layerCount;
    const int mips = static_cast<int>(std::floor(std::log2(base))) + 1;
    bc7MipLevels_ = mips;

    // Per-level dimensions, block counts, per-layer byte size, and level offsets.
    std::vector<int> dim(mips), blocksW(mips), blocksH(mips);
    std::vector<size_t> layerBytes(mips);
    bc7LevelOffsets_.assign(mips, 0);
    size_t total = 0;
    for (int i = 0; i < mips; i++) {
        dim[i] = std::max(1, base >> i);
        blocksW[i] = (dim[i] + 3) / 4;
        blocksH[i] = (dim[i] + 3) / 4;
        layerBytes[i] = static_cast<size_t>(blocksW[i]) * blocksH[i] * BC7ENC_BLOCK_SIZE;
        bc7LevelOffsets_[i] = total;
        total += layerBytes[i] * static_cast<size_t>(layers);
    }
    bc7Data_.assign(total, 0);

    const size_t mip0LayerBytes = static_cast<size_t>(base) * base * 4;

    auto encodeLayer = [&](int L) {
        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);

        // Build this layer's mip chain (RGBA), starting from its mip-0 slice.
        std::vector<uint8_t> prev(atlasInfo_.pixels.begin() + L * mip0LayerBytes,
                                  atlasInfo_.pixels.begin() + (L + 1) * mip0LayerBytes);
        std::vector<uint8_t> cur;
        for (int i = 0; i < mips; i++) {
            const uint8_t* img;
            std::vector<uint8_t> owned;
            if (i == 0) {
                img = prev.data();
            } else {
                cur.assign(static_cast<size_t>(dim[i]) * dim[i] * 4, 0);
                downsampleRGBA(prev.data(), dim[i - 1], dim[i - 1], cur.data(), dim[i], dim[i]);
                prev.swap(cur);
                img = prev.data();
            }

            uint8_t* out = bc7Data_.data() + bc7LevelOffsets_[i] + static_cast<size_t>(L) * layerBytes[i];
            const int w = dim[i], h = dim[i];
            for (int by = 0; by < blocksH[i]; by++) {
                for (int bx = 0; bx < blocksW[i]; bx++) {
                    // Gather a 4x4 RGBA block, clamping to edge for partial/small mips.
                    uint8_t block[64];
                    for (int py = 0; py < 4; py++) {
                        int sy = std::min(by * 4 + py, h - 1);
                        for (int px = 0; px < 4; px++) {
                            int sx = std::min(bx * 4 + px, w - 1);
                            const uint8_t* s = img + (sy * w + sx) * 4;
                            uint8_t* d = block + (py * 4 + px) * 4;
                            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                        }
                    }
                    bc7enc_compress_block(out, block, &params);
                    out += BC7ENC_BLOCK_SIZE;
                }
            }
        }
    };

    // Parallelize across layers.
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    unsigned nThreads = std::min<unsigned>(hw, static_cast<unsigned>(layers));
    std::atomic<int> next{0};
    auto worker = [&]() {
        int L;
        while ((L = next.fetch_add(1)) < layers) encodeLayer(L);
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t + 1 < nThreads; t++) pool.emplace_back(worker);
    worker();
    for (auto& th : pool) th.join();

    LOG_INFO("AtlasManager", "BC7-encoded {} layers x {} mips -> {} MB ({} threads)",
             layers, mips, static_cast<int>(total / (1024 * 1024)), nThreads);
    return true;
}

uint64_t AtlasManager::computeSourceHash() const {
    // FNV-1a over format version + layer size + per-source-file (name, size, mtime) +
    // materials.json metadata. Cheap (no PNG decode) and detects any texture change.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    };
    uint32_t ver = BC7_CACHE_VERSION;
    int sz = TEXTURE_SIZE;
    mix(&ver, sizeof(ver));
    mix(&sz, sizeof(sz));

    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::string> names;
    if (fs::is_directory(sourceDirectory_, ec)) {
        for (const auto& e : fs::directory_iterator(sourceDirectory_, ec)) {
            if (e.is_regular_file(ec)) names.push_back(e.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());  // deterministic order
    for (const auto& name : names) {
        mix(name.data(), name.size());
        fs::path p = fs::path(sourceDirectory_) / name;
        uintmax_t fsz = fs::file_size(p, ec);
        auto wt = fs::last_write_time(p, ec).time_since_epoch().count();
        mix(&fsz, sizeof(fsz));
        mix(&wt, sizeof(wt));
    }
    // materials.json governs layer ordering — include it.
    fs::path mj = "resources/materials.json";
    uintmax_t mjsz = fs::file_size(mj, ec);
    auto mjwt = fs::last_write_time(mj, ec).time_since_epoch().count();
    mix(&mjsz, sizeof(mjsz));
    mix(&mjwt, sizeof(mjwt));
    return h;
}

namespace {
const char* kBC7CacheMagic = "PBX7";
std::string bc7CachePath() { return "cache/textures/voxel_bc7.bin"; }
struct BC7CacheHeader {
    char     magic[4];
    uint32_t version;
    uint64_t hash;
    int32_t  baseSize;
    int32_t  layerCount;
    int32_t  mipLevels;
    int32_t  _pad;
    uint64_t dataSize;
};
} // namespace

bool AtlasManager::loadBC7Cache(uint64_t hash) {
    std::ifstream f(bc7CachePath(), std::ios::binary);
    if (!f) return false;
    BC7CacheHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || std::memcmp(hdr.magic, kBC7CacheMagic, 4) != 0) return false;
    if (hdr.version != BC7_CACHE_VERSION || hdr.hash != hash) return false;
    if (hdr.baseSize != TEXTURE_SIZE || hdr.layerCount != atlasInfo_.layerCount) return false;
    if (hdr.mipLevels <= 0 || hdr.dataSize == 0) return false;

    bc7LevelOffsets_.assign(hdr.mipLevels, 0);
    f.read(reinterpret_cast<char*>(bc7LevelOffsets_.data()),
           static_cast<std::streamsize>(sizeof(uint64_t)) * hdr.mipLevels);
    if (!f) return false;
    bc7Data_.assign(hdr.dataSize, 0);
    f.read(reinterpret_cast<char*>(bc7Data_.data()), static_cast<std::streamsize>(hdr.dataSize));
    if (!f) { bc7Data_.clear(); return false; }
    bc7MipLevels_ = hdr.mipLevels;
    LOG_INFO("AtlasManager", "Loaded BC7 cache: {} layers x {} mips, {} MB",
             hdr.layerCount, hdr.mipLevels, static_cast<int>(hdr.dataSize / (1024 * 1024)));
    return true;
}

void AtlasManager::writeBC7Cache(uint64_t hash) const {
    if (bc7Data_.empty() || bc7MipLevels_ <= 0) return;
    std::error_code ec;
    std::filesystem::create_directories("cache/textures", ec);
    std::ofstream f(bc7CachePath(), std::ios::binary | std::ios::trunc);
    if (!f) { LOG_WARN("AtlasManager", "Could not write BC7 cache"); return; }
    BC7CacheHeader hdr{};
    std::memcpy(hdr.magic, kBC7CacheMagic, 4);
    hdr.version = BC7_CACHE_VERSION;
    hdr.hash = hash;
    hdr.baseSize = TEXTURE_SIZE;
    hdr.layerCount = atlasInfo_.layerCount;
    hdr.mipLevels = bc7MipLevels_;
    hdr.dataSize = bc7Data_.size();
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(bc7LevelOffsets_.data()),
            static_cast<std::streamsize>(sizeof(uint64_t)) * bc7MipLevels_);
    f.write(reinterpret_cast<const char*>(bc7Data_.data()),
            static_cast<std::streamsize>(bc7Data_.size()));
}

bool AtlasManager::uploadToGPU(Vulkan::VulkanDevice* device) {
    if (!device || atlasInfo_.pixels.empty()) return false;

    // Preferred path: BC7-compressed array (4x less VRAM than RGBA8). The encode is slow
    // (~seconds), so cache it to disk keyed by a hash of the source textures.
    if (device->bc7Supported()) {
        uint64_t hash = computeSourceHash();
        bool ready = loadBC7Cache(hash);
        if (!ready && encodeBC7()) {
            writeBC7Cache(hash);
            ready = true;
        }
        if (ready) {
            return device->uploadTextureArrayBC7(
                bc7Data_.data(), bc7Data_.size(), bc7LevelOffsets_,
                TEXTURE_SIZE, atlasInfo_.layerCount, bc7MipLevels_);
        }
    }

    // Fallback: uncompressed RGBA with GPU-generated mips.
    return device->uploadTextureArray(
        atlasInfo_.pixels.data(),
        TEXTURE_SIZE,
        atlasInfo_.layerCount);
}

void AtlasManager::updateUVSSBO(Vulkan::VulkanDevice* device) {
    if (!device || atlasInfo_.uvBounds.empty()) return;
    auto& registry = MaterialRegistry::instance();
    device->updateAtlasUVBuffer(atlasInfo_.uvBounds, registry.getPlaceholderIndex());
}

bool AtlasManager::hotReload(Vulkan::VulkanDevice* device) {
    LOG_INFO("AtlasManager", "Hot-reloading texture atlas...");

    if (!buildAtlas()) {
        LOG_ERROR("AtlasManager", "Failed to rebuild atlas");
        return false;
    }

    if (!uploadToGPU(device)) {
        LOG_ERROR("AtlasManager", "Failed to upload atlas to GPU");
        return false;
    }

    updateUVSSBO(device);

    LOG_INFO("AtlasManager", "Hot-reload complete");
    return true;
}

bool AtlasManager::reloadMaterial(const std::string& materialName, Vulkan::VulkanDevice* device) {
    auto& registry = MaterialRegistry::instance();
    int matID = registry.getMaterialID(materialName);
    if (matID < 0) {
        LOG_WARN("AtlasManager", "reloadMaterial: unknown material '{}'", materialName);
        return false;
    }

    const auto* mat = registry.getMaterial(materialName);
    if (!mat) return false;

    const std::string faceFiles[6] = {
        mat->textures.sideN(), mat->textures.sideS(),
        mat->textures.sideE(), mat->textures.sideW(),
        mat->textures.top(),   mat->textures.bottom()
    };

    for (int faceID = 0; faceID < 6; faceID++) {
        uint16_t atlasIdx = registry.getTextureIndex(matID, faceID);
        if (atlasIdx == MaterialRegistry::INVALID_TEXTURE_INDEX) continue;

        std::string path = sourceDirectory_ + "/" + faceFiles[faceID];
        auto pixels = loadPNG(path);
        if (pixels.empty()) {
            LOG_WARN("AtlasManager", "reloadMaterial: missing PNG '{}', using fallback", path);
            pixels = generateFallbackTexture(atlasIdx);
        }
        blitToAtlas(atlasIdx, pixels.data());
    }

    if (device) {
        if (!uploadToGPU(device)) {
            LOG_ERROR("AtlasManager", "reloadMaterial: GPU upload failed for '{}'", materialName);
            return false;
        }
    }

    LOG_INFO("AtlasManager", "Reloaded material '{}' ({} faces)", materialName, 6);
    return true;
}

bool AtlasManager::saveAtlasPNG(const std::string& path) const {
    if (atlasInfo_.pixels.empty()) return false;
    // Debug dump: write all layers stacked into one tall vertical strip.
    int result = stbi_write_png(path.c_str(),
        TEXTURE_SIZE, TEXTURE_SIZE * atlasInfo_.layerCount,
        4, atlasInfo_.pixels.data(),
        TEXTURE_SIZE * 4);
    return result != 0;
}

std::vector<uint8_t> AtlasManager::getTextureSlotPixels(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= atlasInfo_.layerCount) return {};

    const size_t layerBytes = static_cast<size_t>(TEXTURE_SIZE) * TEXTURE_SIZE * 4;
    std::vector<uint8_t> result(layerBytes);
    const size_t srcOffset = static_cast<size_t>(slotIndex) * layerBytes;
    if (srcOffset + layerBytes <= atlasInfo_.pixels.size()) {
        memcpy(result.data(), atlasInfo_.pixels.data() + srcOffset, layerBytes);
    }
    return result;
}

} // namespace Core
} // namespace Phyxel
