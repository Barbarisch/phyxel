#include "core/AtlasManager.h"
#include "core/MaterialRegistry.h"
#include "vulkan/VulkanDevice.h"
#include "utils/Logger.h"

#include "stb_image.h"
#include "stb_image_write.h"

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
    // We expect TEXTURE_SIZE x TEXTURE_SIZE
    if (w != TEXTURE_SIZE || h != TEXTURE_SIZE) {
        LOG_WARN("AtlasManager", "Texture {} is {}x{}, expected {}x{}, will use as-is",
                 path, w, h, TEXTURE_SIZE, TEXTURE_SIZE);
    }
    // Only copy TEXTURE_SIZE x TEXTURE_SIZE pixels
    std::vector<uint8_t> result(TEXTURE_SIZE * TEXTURE_SIZE * 4, 0);
    int copyW = std::min(w, TEXTURE_SIZE);
    int copyH = std::min(h, TEXTURE_SIZE);
    for (int y = 0; y < copyH; y++) {
        memcpy(result.data() + y * TEXTURE_SIZE * 4,
               data + y * w * 4,
               copyW * 4);
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

bool AtlasManager::uploadToGPU(Vulkan::VulkanDevice* device) {
    if (!device || atlasInfo_.pixels.empty()) return false;
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
