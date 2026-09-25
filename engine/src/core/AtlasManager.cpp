#include "core/AtlasManager.h"
#include "core/MaterialRegistry.h"
#include "core/DamageStage.h"     // crackStyleFor - fracture character from brittleS1 (P5)
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
#include <iterator>
#include <fstream>

namespace Phyxel {
namespace Core {
namespace { std::string bc7CachePath(int baseSize, const char* tag); }

AtlasManager::AtlasManager() = default;
AtlasManager::~AtlasManager() = default;

void AtlasManager::calcAtlasDimensions(int textureCount, int& outWidth, int& outHeight) {
    // Legacy (unused by the texture-array path); retained for any old callers.
    int rows = (textureCount + TEXTURES_PER_ROW - 1) / TEXTURES_PER_ROW;
    outWidth  = TEXTURES_PER_ROW * CELL_SIZE;
    outHeight = rows * CELL_SIZE;
    auto nextPow2 = [](int v) -> int { int p = 1; while (p < v) p <<= 1; return p; };
    int atlasSize = nextPow2(std::max(outWidth, outHeight));
    if (atlasSize < 128) atlasSize = 128;
    if (atlasSize > MAX_ATLAS_SIZE) atlasSize = MAX_ATLAS_SIZE;
    outWidth = atlasSize;
    outHeight = atlasSize;
}

std::vector<uint8_t> AtlasManager::loadPNG(const std::string& path, int size) const {
    int w, h, channels;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!data) return {};

    std::vector<uint8_t> result(static_cast<size_t>(size) * size * 4, 0);

    if (w == size && h == size) {
        memcpy(result.data(), data, result.size());
    } else {
        // Bilinear-resample any source resolution to the array layer size. Lets low-res
        // hand-authored sources coexist with native high-res (e.g. CC0) tiles.
        auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
        for (int y = 0; y < size; y++) {
            float sy = (y + 0.5f) * h / size - 0.5f;
            int y0 = static_cast<int>(std::floor(sy));
            float fy = sy - y0;
            int y0c = clampi(y0, 0, h - 1), y1c = clampi(y0 + 1, 0, h - 1);
            for (int x = 0; x < size; x++) {
                float sx = (x + 0.5f) * w / size - 0.5f;
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
                    result[(y * size + x) * 4 + c] =
                        static_cast<uint8_t>(top + (bot - top) * fy + 0.5f);
                }
            }
        }
    }

    stbi_image_free(data);
    return result;
}

std::vector<uint8_t> AtlasManager::generateFallbackTexture(int size) const {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int idx = (y * size + x) * 4;
            bool checker = ((x / 4) + (y / 4)) % 2;
            pixels[idx + 0] = checker ? 255 : 0;
            pixels[idx + 1] = 0;
            pixels[idx + 2] = checker ? 255 : 0;
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}

void AtlasManager::blitLayer(std::vector<uint8_t>& dst, int baseSize, int layer, const uint8_t* texPixels) {
    const size_t layerBytes = static_cast<size_t>(baseSize) * baseSize * 4;
    const size_t dstOffset = static_cast<size_t>(layer) * layerBytes;
    if (dstOffset + layerBytes <= dst.size()) {
        memcpy(dst.data() + dstOffset, texPixels, layerBytes);
    }
}

bool AtlasManager::buildClass(int resClass, bool forceDecode) {
    auto& registry = MaterialRegistry::instance();
    AtlasInfo& a = atlas_[resClass & 1];
    const int baseSize = (resClass == 1) ? TEXTURE_SIZE_HI : TEXTURE_SIZE;
    const int layerCount = registry.getTextureCount(resClass);

    a.baseSize = baseSize;
    a.atlasWidth = baseSize;
    a.atlasHeight = baseSize;
    a.textureCount = layerCount;
    a.layerCount = layerCount;
    a.uvBounds.assign(std::max(1, layerCount), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    a.fromCache = false;
    a.bc7Data.clear(); a.bc7LevelOffsets.clear(); a.bc7MipLevels = 0;
    a.nrBc7Data.clear(); a.nrBc7LevelOffsets.clear(); a.nrBc7MipLevels = 0;
    if (layerCount == 0) { a.pixels.clear(); a.nrPixels.clear(); return true; }

    // Cache-first (G-114): with both maps' BC7 chains on disk for these exact inputs, skip
    // the PNG decode entirely - the chains upload as they are. The decode (and the encode)
    // happen only when an input changed, and their result is written back for next time.
    if (!forceDecode) {
        const uint64_t hash = computeSourceHash(resClass);
        const bool albedoHit = loadBC7File(bc7CachePath(baseSize, "albedo"), hash, baseSize, layerCount,
                                           a.bc7Data, a.bc7LevelOffsets, a.bc7MipLevels);
        const bool nrHit = albedoHit && loadBC7File(bc7CachePath(baseSize, "nr"), hash, baseSize, layerCount,
                                                    a.nrBc7Data, a.nrBc7LevelOffsets, a.nrBc7MipLevels);
        if (albedoHit && nrHit) {
            a.pixels.clear(); a.nrPixels.clear();
            a.fromCache = true;
            LOG_INFO("AtlasManager", "Texture array class {}: BC7 cache hit, {} layers @ {}px - source decode skipped ({} MB)",
                     resClass, layerCount, baseSize,
                     static_cast<int>((a.bc7Data.size() + a.nrBc7Data.size()) / (1024 * 1024)));
            return true;
        }
        a.bc7Data.clear(); a.bc7LevelOffsets.clear(); a.bc7MipLevels = 0;
        a.nrBc7Data.clear(); a.nrBc7LevelOffsets.clear(); a.nrBc7MipLevels = 0;
    }
    decodeClassPixels(resClass);
    return true;
}

void AtlasManager::ensurePixels(int resClass) {
    AtlasInfo& a = atlas_[resClass & 1];
    if (a.layerCount > 0 && a.pixels.empty()) decodeClassPixels(resClass);
}

void AtlasManager::decodeClassPixels(int resClass) {
    auto& registry = MaterialRegistry::instance();
    AtlasInfo& a = atlas_[resClass & 1];
    const int baseSize = a.baseSize;
    const int layerCount = a.layerCount;
    if (layerCount == 0) { a.pixels.clear(); return; }

    const size_t layerBytes = static_cast<size_t>(baseSize) * baseSize * 4;
    a.pixels.assign(layerBytes * layerCount, 0);

    // Normal+roughness array: default every layer to a flat normal (0,0,1)->(128,128,255)
    // with roughness 0.9 (230). Faces with a `<albedo>_nr.png` sidecar overwrite their layer.
    a.nrPixels.assign(layerBytes * layerCount, 0);
    for (size_t p = 0; p < a.nrPixels.size(); p += 4) {
        a.nrPixels[p + 0] = 128; a.nrPixels[p + 1] = 128;
        a.nrPixels[p + 2] = 255; a.nrPixels[p + 3] = 230;
    }

    for (const auto& mat : registry.getAllMaterials()) {
        if (mat.resClass() != resClass) continue;
        int matID = registry.getMaterialID(mat.name);
        if (matID < 0) continue;

        const std::string faceFiles[6] = {
            mat.textures.sideN(), mat.textures.sideS(),
            mat.textures.sideE(), mat.textures.sideW(),
            mat.textures.top(), mat.textures.bottom()
        };
        for (int faceID = 0; faceID < 6; faceID++) {
            uint16_t idx = registry.getTextureIndex(matID, faceID);
            if (idx == MaterialRegistry::INVALID_TEXTURE_INDEX) continue;
            int layer = idx & MaterialRegistry::LAYER_MASK;

            std::string path = sourceDirectory_ + "/" + faceFiles[faceID];
            auto texPixels = loadPNG(path, baseSize);
            if (texPixels.empty()) {
                LOG_WARN("AtlasManager", "Missing texture: {}, using fallback", path);
                texPixels = generateFallbackTexture(baseSize);
            }
            blitLayer(a.pixels, baseSize, layer, texPixels.data());

            // Optional normal+roughness sidecar (<albedo-base>_nr.png).
            std::string base = faceFiles[faceID];
            size_t dot = base.find_last_of('.');
            if (dot != std::string::npos) base = base.substr(0, dot);
            auto nrPixels = loadPNG(sourceDirectory_ + "/" + base + "_nr.png", baseSize);
            if (!nrPixels.empty()) {
                blitLayer(a.nrPixels, baseSize, layer, nrPixels.data());
            }
        }
    }

    LOG_INFO("AtlasManager", "Built texture array class {}: {} layers @ {}x{} (albedo + normal/rough)",
             resClass, layerCount, baseSize, baseSize);
}

bool AtlasManager::buildAtlas(bool forceDecode) {
    auto& registry = MaterialRegistry::instance();
    if (registry.getTextureCount() == 0) {
        LOG_ERROR("AtlasManager", "No textures in MaterialRegistry");
        return false;
    }
    bool ok = true;
    for (int c = 0; c < NUM_CLASSES; c++) ok = buildClass(c, forceDecode) && ok;
    return ok;
}

bool AtlasManager::updateTextureSlot(int slotIndex, const uint8_t* pixels) {
    if (!pixels) return false;
    int cls = (slotIndex & MaterialRegistry::RES_CLASS_BIT) ? 1 : 0;
    int layer = slotIndex & MaterialRegistry::LAYER_MASK;
    if (layer < 0 || layer >= atlas_[cls].layerCount) return false;
    ensurePixels(cls);
    atlas_[cls].fromCache = false;
    blitLayer(atlas_[cls].pixels, atlas_[cls].baseSize, layer, pixels);
    return true;
}

namespace {
// Box-downsample an RGBA image (srcW x srcH) to (dstW x dstH) for CPU mip generation.
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

void AtlasManager::bc7EncodeLayers(const std::vector<uint8_t>& src, int base, int layers,
                                   std::vector<uint8_t>& outData,
                                   std::vector<size_t>& outOffsets, int& outMips) {
    static std::atomic<bool> s_bc7Init{false};
    if (!s_bc7Init.exchange(true)) bc7enc_compress_block_init();

    const int mips = static_cast<int>(std::floor(std::log2(base))) + 1;
    outMips = mips;

    std::vector<int> dim(mips), blocksW(mips), blocksH(mips);
    std::vector<size_t> layerBytes(mips);
    outOffsets.assign(mips, 0);
    size_t total = 0;
    for (int i = 0; i < mips; i++) {
        dim[i] = std::max(1, base >> i);
        blocksW[i] = (dim[i] + 3) / 4;
        blocksH[i] = (dim[i] + 3) / 4;
        layerBytes[i] = static_cast<size_t>(blocksW[i]) * blocksH[i] * BC7ENC_BLOCK_SIZE;
        outOffsets[i] = total;
        total += layerBytes[i] * static_cast<size_t>(layers);
    }
    outData.assign(total, 0);

    const size_t mip0LayerBytes = static_cast<size_t>(base) * base * 4;
    uint8_t* outBase = outData.data();
    const uint8_t* srcBase = src.data();

    auto encodeLayer = [&](int L) {
        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);

        std::vector<uint8_t> prev(srcBase + L * mip0LayerBytes,
                                  srcBase + (L + 1) * mip0LayerBytes);
        std::vector<uint8_t> cur;
        for (int i = 0; i < mips; i++) {
            const uint8_t* img;
            if (i == 0) {
                img = prev.data();
            } else {
                cur.assign(static_cast<size_t>(dim[i]) * dim[i] * 4, 0);
                downsampleRGBA(prev.data(), dim[i - 1], dim[i - 1], cur.data(), dim[i], dim[i]);
                prev.swap(cur);
                img = prev.data();
            }
            uint8_t* out = outBase + outOffsets[i] + static_cast<size_t>(L) * layerBytes[i];
            const int w = dim[i], h = dim[i];
            for (int by = 0; by < blocksH[i]; by++) {
                for (int bx = 0; bx < blocksW[i]; bx++) {
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

    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    unsigned nThreads = std::min<unsigned>(hw, static_cast<unsigned>(layers));
    std::atomic<int> next{0};
    auto worker = [&]() { int L; while ((L = next.fetch_add(1)) < layers) encodeLayer(L); };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t + 1 < nThreads; t++) pool.emplace_back(worker);
    worker();
    for (auto& th : pool) th.join();

    LOG_INFO("AtlasManager", "BC7-encoded {} layers x {} mips @ {}px -> {} MB ({} threads)",
             layers, mips, base, static_cast<int>(total / (1024 * 1024)), nThreads);
}

uint64_t AtlasManager::computeSourceHash(int resClass) const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    };
    uint32_t ver = BC7_CACHE_VERSION;
    int sz = atlas_[resClass & 1].baseSize;
    mix(&ver, sizeof(ver));
    mix(&sz, sizeof(sz));

    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::string> names;
    if (fs::is_directory(sourceDirectory_, ec)) {
        for (const auto& e : fs::directory_iterator(sourceDirectory_, ec))
            if (e.is_regular_file(ec)) names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    // CONTENT-keyed (G-114): name + size + the first and last 64 KB of each file. A build
    // copies resources/ with fresh modification times, so an mtime key missed on every
    // post-build boot (re-encoding ~550 MB of BC7); a same-size edit still changes the PNG
    // stream bytes, which the head/tail digest sees. ~526 files x 128 KB = a tenth of a
    // second, against 14 s of decode.
    auto mixFileEdges = [&](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return;
        constexpr std::streamsize kEdge = 64 * 1024;
        std::vector<char> buf(static_cast<size_t>(kEdge));
        f.read(buf.data(), kEdge);
        mix(buf.data(), static_cast<size_t>(f.gcount()));
        f.clear();
        f.seekg(0, std::ios::end);
        const std::streamoff end = f.tellg();
        if (end > kEdge) {
            f.seekg(end - kEdge, std::ios::beg);
            f.read(buf.data(), kEdge);
            mix(buf.data(), static_cast<size_t>(f.gcount()));
        }
    };
    for (const auto& name : names) {
        mix(name.data(), name.size());
        fs::path p = fs::path(sourceDirectory_) / name;
        uintmax_t fsz = fs::file_size(p, ec);
        mix(&fsz, sizeof(fsz));
        mixFileEdges(p);
    }
    {
        std::ifstream mj("resources/materials.json", std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(mj)), std::istreambuf_iterator<char>());
        mix(body.data(), body.size());
    }
    return h;
}

namespace {
const char* kBC7CacheMagic = "PBX7";
std::string bc7CachePath(int baseSize, const char* tag) {
    return "cache/textures/voxel_bc7_" + std::string(tag) + "_" + std::to_string(baseSize) + ".bin";
}
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

bool AtlasManager::loadBC7File(const std::string& path, uint64_t hash, int baseSize, int layerCount,
                               std::vector<uint8_t>& outData, std::vector<size_t>& outOffsets,
                               int& outMips) const {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    BC7CacheHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || std::memcmp(hdr.magic, kBC7CacheMagic, 4) != 0) return false;
    if (hdr.version != BC7_CACHE_VERSION || hdr.hash != hash) return false;
    if (hdr.baseSize != baseSize || hdr.layerCount != layerCount) return false;
    if (hdr.mipLevels <= 0 || hdr.dataSize == 0) return false;

    outOffsets.assign(hdr.mipLevels, 0);
    f.read(reinterpret_cast<char*>(outOffsets.data()),
           static_cast<std::streamsize>(sizeof(uint64_t)) * hdr.mipLevels);
    if (!f) return false;
    outData.assign(hdr.dataSize, 0);
    f.read(reinterpret_cast<char*>(outData.data()), static_cast<std::streamsize>(hdr.dataSize));
    if (!f) { outData.clear(); return false; }
    outMips = hdr.mipLevels;
    return true;
}

void AtlasManager::writeBC7File(const std::string& path, uint64_t hash, int baseSize, int layerCount,
                                const std::vector<uint8_t>& data, const std::vector<size_t>& offsets,
                                int mips) const {
    if (data.empty() || mips <= 0) return;
    std::error_code ec;
    std::filesystem::create_directories("cache/textures", ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { LOG_WARN("AtlasManager", "Could not write BC7 cache '{}'", path); return; }
    BC7CacheHeader hdr{};
    std::memcpy(hdr.magic, kBC7CacheMagic, 4);
    hdr.version = BC7_CACHE_VERSION;
    hdr.hash = hash;
    hdr.baseSize = baseSize;
    hdr.layerCount = layerCount;
    hdr.mipLevels = mips;
    hdr.dataSize = data.size();
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(offsets.data()),
            static_cast<std::streamsize>(sizeof(uint64_t)) * mips);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

bool AtlasManager::uploadToGPU(Vulkan::VulkanDevice* device) {
    if (!device) return false;

    bool any = false;
    for (int c = 0; c < NUM_CLASSES; c++) {
        AtlasInfo& a = atlas_[c];
        if (a.layerCount <= 0) continue;
        if (a.pixels.empty() && !a.fromCache) continue;
        const uint64_t hash = computeSourceHash(c);

        // Helper: get a map's BC7 data (load cache, else encode + write cache), then upload.
        // target: albedo = c (0/1, binding 1/5), normal+rough = c+2 (2/3, binding 6/7).
        auto doMap = [&](const char* tag, const std::vector<uint8_t>& srcPixels,
                         std::vector<uint8_t>& bc7, std::vector<size_t>& offsets, int& mips,
                         int target) {
            bool uploaded = false;
            if (device->bc7Supported()) {
                std::string path = bc7CachePath(a.baseSize, tag);
                // Chains already loaded by buildClass (cache-first) upload as they are.
                bool ready = (!bc7.empty() && mips > 0) ||
                             loadBC7File(path, hash, a.baseSize, a.layerCount, bc7, offsets, mips);
                if (ready) {
                    LOG_INFO("AtlasManager", "BC7 {} class {} ready ({} MB)",
                             tag, c, static_cast<int>(bc7.size() / (1024 * 1024)));
                } else {
                    ensurePixels(c);
                    bc7EncodeLayers(srcPixels, a.baseSize, a.layerCount, bc7, offsets, mips);
                    writeBC7File(path, hash, a.baseSize, a.layerCount, bc7, offsets, mips);
                    ready = true;
                }
                if (ready) {
                    uploaded = device->uploadTextureArrayBC7(target, bc7.data(), bc7.size(),
                                                             offsets, a.baseSize, a.layerCount, mips);
                }
            }
            if (!uploaded) {
                ensurePixels(c);
                uploaded = !srcPixels.empty() &&
                           device->uploadTextureArray(target, srcPixels.data(), a.baseSize, a.layerCount);
            }
            return uploaded;
        };

        bool albedoOk = doMap("albedo", a.pixels, a.bc7Data, a.bc7LevelOffsets, a.bc7MipLevels, c);
        doMap("nr", a.nrPixels, a.nrBc7Data, a.nrBc7LevelOffsets, a.nrBc7MipLevels, c + 2);
        any = any || albedoOk;
    }
    return any;
}

void AtlasManager::updateUVSSBO(Vulkan::VulkanDevice* device) {
    if (!device) return;
    auto& registry = MaterialRegistry::instance();
    const int c0 = registry.getTextureCount(0), c1 = registry.getTextureCount(1);

    // Per-layer material props packed into the SSBO's (repurposed) textureUVs[] array, at a
    // STRIDE OF TWO vec4s per layer (P5, docs/VoxelDamageVisualization.md §4):
    //   [gi*2 + 0]  x = metallic, y = roughness scalar, z = emissiveStrength, w = emissiveThreshold
    //               (masked emission — docs/MaskedEmissiveSpec.md; z=0 = ordinary material)
    //   [gi*2 + 1]  x = crackStyle, yzw = spare
    // Global index gi = layer (class 0) or c0+layer (class 1).
    //
    // Why a second vec4 rather than reclaiming instance bits: crackStyle is a per-MATERIAL
    // property, which is exactly what this array is for and what the 16-bit instance `reserved`
    // word is not — and that word is FULL (bit 0 emissive, 1 transparent, 2-9 alpha, 10 mirror,
    // 11-14 damage, 15 varied). The only reclaimable bits are the 8-bit alpha, and narrowing a
    // shipped feature's transparency precision to buy an appearance knob is the wrong trade.
    // Costs a few KB and leaves 3 spare floats — the first free per-material appearance slot
    // since this array was repurposed.
    const int kPropStride = 2;
    std::vector<glm::vec4> props(std::max(1, (c0 + c1) * kPropStride), glm::vec4(0.0f));
    for (size_t i = 0; i + 1 < props.size(); i += kPropStride) {
        props[i] = glm::vec4(0.0f, 0.5f, 0.0f, 0.0f);            // metallic/rough/em defaults
        props[i + 1] = glm::vec4(Core::crackStyleFor(2.5f), 0.0f, 0.0f, 0.0f);  // fallback style
    }
    for (const auto& mat : registry.getAllMaterials()) {
        int matID = registry.getMaterialID(mat.name);
        if (matID < 0) continue;
        const MaterialDef* md = registry.getMaterial(matID);
        float metallic = md ? md->physics.metallic : 0.0f;
        float roughness = md ? md->physics.roughness : 0.5f;
        float emStr = md ? md->emissiveStrength : 0.0f;
        float emThr = md ? md->emissiveThreshold : 0.55f;
        for (int f = 0; f < 6; f++) {
            uint16_t idx = registry.getTextureIndex(matID, f);
            if (idx == MaterialRegistry::INVALID_TEXTURE_INDEX) continue;
            int cls = (idx & MaterialRegistry::RES_CLASS_BIT) ? 1 : 0;
            int layer = idx & MaterialRegistry::LAYER_MASK;
            int gi = (cls == 1) ? c0 + layer : layer;
            if (gi >= 0 && (gi * kPropStride + 1) < static_cast<int>(props.size())) {
                props[gi * kPropStride] = glm::vec4(metallic, roughness, emStr, emThr);
                // brittleS1 drives fracture character; materials with no break block fall back
                // to the same 2.5 the break model uses for them (DamageSystem::responseFor).
                const float s1 = (md && md->breakProfile.hasProfile) ? md->breakProfile.brittleS1
                                                                     : 2.5f;
                props[gi * kPropStride + 1] = glm::vec4(Core::crackStyleFor(s1), 0.0f, 0.0f, 0.0f);
            }
        }
    }
    device->updateAtlasUVBuffer(props, registry.getPlaceholderIndex(), c0, c1);
}

bool AtlasManager::hotReload(Vulkan::VulkanDevice* device) {
    LOG_INFO("AtlasManager", "Hot-reloading texture arrays...");
    if (!buildAtlas(/*forceDecode=*/true)) { LOG_ERROR("AtlasManager", "Failed to rebuild atlas"); return false; }
    if (!uploadToGPU(device)) { LOG_ERROR("AtlasManager", "Failed to upload atlas to GPU"); return false; }
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
    const int cls = mat->resClass();
    const int baseSize = atlas_[cls].baseSize;
    ensurePixels(cls);
    atlas_[cls].fromCache = false;   // edited: the cached chains no longer match

    const std::string faceFiles[6] = {
        mat->textures.sideN(), mat->textures.sideS(),
        mat->textures.sideE(), mat->textures.sideW(),
        mat->textures.top(),   mat->textures.bottom()
    };
    for (int faceID = 0; faceID < 6; faceID++) {
        uint16_t idx = registry.getTextureIndex(matID, faceID);
        if (idx == MaterialRegistry::INVALID_TEXTURE_INDEX) continue;
        std::string path = sourceDirectory_ + "/" + faceFiles[faceID];
        auto pixels = loadPNG(path, baseSize);
        if (pixels.empty()) pixels = generateFallbackTexture(baseSize);
        blitLayer(atlas_[cls].pixels, baseSize, idx & MaterialRegistry::LAYER_MASK, pixels.data());
    }

    if (device && !uploadToGPU(device)) {
        LOG_ERROR("AtlasManager", "reloadMaterial: GPU upload failed for '{}'", materialName);
        return false;
    }
    LOG_INFO("AtlasManager", "Reloaded material '{}' (class {})", materialName, cls);
    return true;
}

bool AtlasManager::saveAtlasPNG(const std::string& path) const {
    const_cast<AtlasManager*>(this)->ensurePixels(0);
    const AtlasInfo& a = atlas_[0];
    if (a.pixels.empty()) return false;
    int result = stbi_write_png(path.c_str(),
        a.baseSize, a.baseSize * a.layerCount, 4, a.pixels.data(), a.baseSize * 4);
    return result != 0;
}

std::vector<uint8_t> AtlasManager::getTextureSlotPixels(int slotIndex) const {
    int cls = (slotIndex & MaterialRegistry::RES_CLASS_BIT) ? 1 : 0;
    int layer = slotIndex & MaterialRegistry::LAYER_MASK;
    const_cast<AtlasManager*>(this)->ensurePixels(cls);
    const AtlasInfo& a = atlas_[cls];
    if (layer < 0 || layer >= a.layerCount) return {};
    const size_t layerBytes = static_cast<size_t>(a.baseSize) * a.baseSize * 4;
    std::vector<uint8_t> result(layerBytes);
    const size_t srcOffset = static_cast<size_t>(layer) * layerBytes;
    if (srcOffset + layerBytes <= a.pixels.size())
        memcpy(result.data(), a.pixels.data() + srcOffset, layerBytes);
    return result;
}

} // namespace Core
} // namespace Phyxel
