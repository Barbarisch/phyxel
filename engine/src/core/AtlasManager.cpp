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
    int col = slotIndex % TEXTURES_PER_ROW;
    int row = slotIndex / TEXTURES_PER_ROW;
    int destX = col * CELL_SIZE + PADDING;
    int destY = row * CELL_SIZE + PADDING;
    int atlasW = atlasInfo_.atlasWidth;

    for (int y = 0; y < TEXTURE_SIZE; y++) {
        int srcOffset = y * TEXTURE_SIZE * 4;
        int dstOffset = ((destY + y) * atlasW + destX) * 4;
        if (dstOffset + TEXTURE_SIZE * 4 <= static_cast<int>(atlasInfo_.pixels.size())) {
            memcpy(atlasInfo_.pixels.data() + dstOffset,
                   texPixels + srcOffset,
                   TEXTURE_SIZE * 4);
        }
    }
}

bool AtlasManager::buildAtlas() {
    auto& registry = MaterialRegistry::instance();
    int textureCount = registry.getTextureCount();
    if (textureCount == 0) {
        LOG_ERROR("AtlasManager", "No textures in MaterialRegistry");
        return false;
    }

    // Calculate atlas size
    int atlasW, atlasH;
    calcAtlasDimensions(textureCount, atlasW, atlasH);

    atlasInfo_.atlasWidth = atlasW;
    atlasInfo_.atlasHeight = atlasH;
    atlasInfo_.textureCount = textureCount;
    atlasInfo_.pixels.assign(atlasW * atlasH * 4, 0); // Clear to transparent black

    // Get material list and load each face's source PNG
    const auto& materials = registry.getAllMaterials();
    float fAtlasW = static_cast<float>(atlasW);
    float fAtlasH = static_cast<float>(atlasH);

    atlasInfo_.uvBounds.resize(textureCount);

    // Atlas texture ordering: textures are placed alphabetically by filename
    // in the atlas. The MaterialRegistry stores atlas indices that match this order.
    // We iterate by atlas slot index to fill each position.

    // Build a mapping: slot index → source PNG filename
    // MaterialRegistry stores face indices as materialID * 6 + faceID BUT
    // the actual atlas is alphabetically ordered. We need to match the existing layout.
    //
    // The face filenames in materials.json define source PNG names.
    // The atlas index for each face comes from MaterialRegistry::getTextureIndex().
    // These indices correspond to the alphabetical position in the atlas.

    // Iterate all materials and all faces, load and place each
    static const char* faceNames[6] = { "side_n", "side_s", "side_e", "side_w", "top", "bottom" };

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

            // Load source PNG
            std::string path = sourceDirectory_ + "/" + faceFiles[faceID];
            auto texPixels = loadPNG(path);
            if (texPixels.empty()) {
                LOG_WARN("AtlasManager", "Missing texture: {}, using fallback", path);
                texPixels = generateFallbackTexture(atlasIdx);
            }

            // Blit into atlas
            blitToAtlas(atlasIdx, texPixels.data());

            // Compute UV bounds
            int col = atlasIdx % TEXTURES_PER_ROW;
            int row = atlasIdx / TEXTURES_PER_ROW;
            float pixelX = static_cast<float>(col * CELL_SIZE + PADDING);
            float pixelY = static_cast<float>(row * CELL_SIZE + PADDING);
            atlasInfo_.uvBounds[atlasIdx] = glm::vec4(
                pixelX / fAtlasW, pixelY / fAtlasH,
                (pixelX + TEXTURE_SIZE) / fAtlasW, (pixelY + TEXTURE_SIZE) / fAtlasH
            );
        }
    }

    LOG_INFO("AtlasManager", "Built atlas: {}x{}, {} textures from {} materials",
             atlasW, atlasH, textureCount, materials.size());
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
    return device->uploadTextureAtlasPixels(
        atlasInfo_.pixels.data(),
        atlasInfo_.atlasWidth,
        atlasInfo_.atlasHeight);
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

bool AtlasManager::saveAtlasPNG(const std::string& path) const {
    if (atlasInfo_.pixels.empty()) return false;
    int result = stbi_write_png(path.c_str(),
        atlasInfo_.atlasWidth, atlasInfo_.atlasHeight,
        4, atlasInfo_.pixels.data(),
        atlasInfo_.atlasWidth * 4);
    return result != 0;
}

std::vector<uint8_t> AtlasManager::getTextureSlotPixels(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= atlasInfo_.textureCount) return {};

    std::vector<uint8_t> result(TEXTURE_SIZE * TEXTURE_SIZE * 4);
    int col = slotIndex % TEXTURES_PER_ROW;
    int row = slotIndex / TEXTURES_PER_ROW;
    int srcX = col * CELL_SIZE + PADDING;
    int srcY = row * CELL_SIZE + PADDING;
    int atlasW = atlasInfo_.atlasWidth;

    for (int y = 0; y < TEXTURE_SIZE; y++) {
        int srcOffset = ((srcY + y) * atlasW + srcX) * 4;
        int dstOffset = y * TEXTURE_SIZE * 4;
        if (srcOffset + TEXTURE_SIZE * 4 <= static_cast<int>(atlasInfo_.pixels.size())) {
            memcpy(result.data() + dstOffset,
                   atlasInfo_.pixels.data() + srcOffset,
                   TEXTURE_SIZE * 4);
        }
    }
    return result;
}

} // namespace Core
} // namespace Phyxel
