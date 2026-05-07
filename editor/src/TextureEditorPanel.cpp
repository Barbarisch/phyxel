#include "TextureEditorPanel.h"
#include "core/AtlasManager.h"
#include "core/MaterialRegistry.h"
#include "vulkan/VulkanDevice.h"
#include "utils/Logger.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <queue>
#include <fstream>

// Pull Logger into scope
using Phyxel::Utils::Logger;

// stb_image for loading preview PNGs from pack directories
#include "stb_image.h"
// stb_image_write for saving individual PNGs
#include "stb_image_write.h"

namespace Phyxel::Editor {

constexpr const char* TextureEditorPanel::FACE_NAMES[6];

void TextureEditorPanel::render(bool* open) {
    fprintf(stderr, "TEX: render() start\n"); fflush(stderr);

    // Load pack names from config once on first render
    if (knownPacks_.empty()) {
        fprintf(stderr, "TEX: parsing mc_texture_map.json\n"); fflush(stderr);
        // Simple string scan — avoid heavy JSON template instantiation at startup
        try {
            std::ifstream f("resources/mc_texture_map.json");
            if (f.is_open()) {
                std::string content((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
                // Find "packs" section and extract keys
                size_t packsPos = content.find("\"packs\"");
                if (packsPos != std::string::npos) {
                    size_t braceOpen = content.find('{', packsPos);
                    size_t braceClose = content.find('}', braceOpen);
                    if (braceOpen != std::string::npos && braceClose != std::string::npos) {
                        std::string packsBlock = content.substr(braceOpen, braceClose - braceOpen);
                        size_t pos = 0;
                        while ((pos = packsBlock.find('"', pos)) != std::string::npos) {
                            size_t keyEnd = packsBlock.find('"', pos + 1);
                            if (keyEnd == std::string::npos) break;
                            std::string key = packsBlock.substr(pos + 1, keyEnd - pos - 1);
                            // Skip if this looks like a path value (contains /)
                            if (key.find('/') == std::string::npos && key.find('\\') == std::string::npos && !key.empty()) {
                                knownPacks_.push_back(key);
                            }
                            pos = keyEnd + 1;
                        }
                    }
                }
            }
        } catch (...) {}
        if (knownPacks_.empty()) {
            // Fallback if file unavailable
            knownPacks_ = { "realistico", "rtxbeta", "umsoea" };
        }
        fprintf(stderr, "TEX: packs loaded: %zu\n", knownPacks_.size()); fflush(stderr);
    }

    fprintf(stderr, "TEX: calling ImGui::Begin\n"); fflush(stderr);
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Texture Editor", open, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }
    fprintf(stderr, "TEX: Begin ok, rendering menu bar\n"); fflush(stderr);

    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Source PNG", "Ctrl+S", false, hasUnsavedChanges_)) {
                saveSourcePNG();
            }
            if (ImGui::MenuItem("Apply to Atlas", nullptr, false, hasUnsavedChanges_)) {
                applyToAtlas();
            }
            if (ImGui::MenuItem("Save & Apply")) {
                saveSourcePNG();
                applyToAtlas();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reload from Disk")) {
                loadCurrentTexture();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Reload")) {
            bool hasMat = (selectedMaterialID_ >= 0);
            if (ImGui::MenuItem("Reload Material from Disk", nullptr, false, hasMat && vulkanDevice_ != nullptr)) {
                reloadMaterialFromDisk();
            }
            if (ImGui::IsItemHovered() && !hasMat) {
                ImGui::SetTooltip("Select a material first");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Hot-Reload All Materials", nullptr, false, vulkanDevice_ != nullptr)) {
                hotReloadAll();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    fprintf(stderr, "TEX: menu bar done\n"); fflush(stderr);

    // Left column: material selector + face selector + tools + color picker
    // Material list gets a fixed-height scrollable sub-child so it stays visible
    // while the color picker occupies the remaining space.
    ImGui::BeginChild("LeftPane", ImVec2(190, 0), true);
    fprintf(stderr, "TEX: LeftPane begin\n"); fflush(stderr);

    // --- Material list (fixed height, independently scrollable) ---
    ImGui::Text("Material");
    float matListH = ImGui::GetTextLineHeightWithSpacing() *
                     static_cast<float>(Core::MaterialRegistry::instance().getMaterialCount())
                     + 6.0f;
    // Cap so it doesn't consume more than half the pane height
    float paneH = ImGui::GetContentRegionAvail().y;
    matListH = std::min(matListH, paneH * 0.45f);
    // Ensure a minimum height so BeginChild doesn't get 0 or negative
    matListH = std::max(matListH, 20.0f);
    fprintf(stderr, "TEX: MatList height={:.1f} %s\n", std::to_string(matListH).c_str()); fflush(stderr);
    ImGui::BeginChild("MatList", ImVec2(0, matListH), false);
    renderMaterialSelector();
    ImGui::EndChild();
    fprintf(stderr, "TEX: MatList done, rendering faces\n"); fflush(stderr);

    ImGui::Separator();
    renderFaceSelector();
    fprintf(stderr, "TEX: faces done, rendering pack compare\n"); fflush(stderr);
    ImGui::Separator();
    renderPackCompare();
    fprintf(stderr, "TEX: pack compare done, rendering toolbar\n"); fflush(stderr);
    ImGui::Separator();
    renderToolbar();
    fprintf(stderr, "TEX: toolbar done, rendering color picker\n"); fflush(stderr);
    ImGui::Separator();
    renderColorPicker();
    fprintf(stderr, "TEX: color picker done\n"); fflush(stderr);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right column: canvas + preview
    fprintf(stderr, "TEX: RightPane begin\n"); fflush(stderr);
    ImGui::BeginChild("RightPane", ImVec2(0, 0), false);
    renderCanvas();
    fprintf(stderr, "TEX: canvas done\n"); fflush(stderr);
    ImGui::Separator();
    renderPreview();
    fprintf(stderr, "TEX: preview done\n"); fflush(stderr);
    ImGui::EndChild();

    ImGui::End();
    LOG_DEBUG("TextureEditor", "render() complete");
}

void TextureEditorPanel::renderMaterialSelector() {
    auto& reg = Core::MaterialRegistry::instance();
    const auto& materials = reg.getAllMaterials();

    ImGui::Text("Material");
    for (int i = 0; i < static_cast<int>(materials.size()); i++) {
        int matID = reg.getMaterialID(materials[i].name);
        bool selected = (matID == selectedMaterialID_);
        if (ImGui::Selectable(materials[i].name.c_str(), selected)) {
            selectedMaterialID_ = matID;
            selectedMaterialName_ = materials[i].name;
            loadCurrentTexture();
        }
    }
}

void TextureEditorPanel::renderFaceSelector() {
    ImGui::Text("Face");
    for (int i = 0; i < 6; i++) {
        bool selected = (i == selectedFaceID_);
        if (ImGui::Selectable(FACE_NAMES[i], selected)) {
            selectedFaceID_ = i;
            loadCurrentTexture();
        }
    }
}

void TextureEditorPanel::renderToolbar() {
    ImGui::Text("Tool");

    auto toolBtn = [&](const char* label, Tool t) {
        bool active = (currentTool_ == t);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        if (ImGui::Button(label, ImVec2(38, 24))) currentTool_ = t;
        if (active) ImGui::PopStyleColor();
    };

    toolBtn("Pen", Tool::Pencil);
    ImGui::SameLine();
    toolBtn("Era", Tool::Eraser);
    ImGui::SameLine();
    toolBtn("Fill", Tool::Fill);
    ImGui::SameLine();
    toolBtn("Eye", Tool::Eyedropper);

    ImGui::SliderInt("Zoom", &canvasZoom_, 4, 16);
}

void TextureEditorPanel::renderColorPicker() {
    ImGui::Text("Color");
    ImGui::ColorPicker4("##TexColor", currentColor_,
        ImGuiColorEditFlags_NoSidePreview |
        ImGuiColorEditFlags_AlphaBar |
        ImGuiColorEditFlags_PickerHueBar);
}

void TextureEditorPanel::renderCanvas() {
    if (selectedMaterialID_ < 0) {
        ImGui::TextWrapped("Select a material and face to begin editing.");
        return;
    }

    ImGui::Text("%s - %s %s", selectedMaterialName_.c_str(), FACE_NAMES[selectedFaceID_],
                hasUnsavedChanges_ ? "(modified)" : "");

    int canvasPixels = TEX_SIZE * canvasZoom_;

    // Reserve space FIRST, THEN draw into it — ensures valid screen coords on every frame
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##canvas", ImVec2((float)canvasPixels, (float)canvasPixels));

    bool hovered = ImGui::IsItemHovered();
    bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Draw background (checkerboard for transparency)
    for (int cy = 0; cy < TEX_SIZE; cy++) {
        for (int cx = 0; cx < TEX_SIZE; cx++) {
            float x0 = canvasPos.x + cx * canvasZoom_;
            float y0 = canvasPos.y + cy * canvasZoom_;
            float x1 = x0 + canvasZoom_;
            float y1 = y0 + canvasZoom_;

            // Checkerboard background
            bool checker = ((cx / 2) + (cy / 2)) % 2;
            ImU32 bgCol = checker ? IM_COL32(200, 200, 200, 255) : IM_COL32(150, 150, 150, 255);
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), bgCol);

            // Pixel color (alpha blended)
            uint32_t px = getPixel(cx, cy);
            uint8_t r = px & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF, a = (px >> 24) & 0xFF;
            if (a > 0) {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(r, g, b, a));
            }

            // Grid lines (only draw when zoomed in enough to be useful)
            if (canvasZoom_ >= 6) {
                drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(80, 80, 80, 60));
            }
        }
    }

    if (hovered) {
        ImVec2 mousePos = ImGui::GetMousePos();
        int px = static_cast<int>((mousePos.x - canvasPos.x) / canvasZoom_);
        int py = static_cast<int>((mousePos.y - canvasPos.y) / canvasZoom_);
        px = std::clamp(px, 0, TEX_SIZE - 1);
        py = std::clamp(py, 0, TEX_SIZE - 1);

        // Hover highlight
        float hx = canvasPos.x + px * canvasZoom_;
        float hy = canvasPos.y + py * canvasZoom_;
        drawList->AddRect(ImVec2(hx, hy), ImVec2(hx + canvasZoom_, hy + canvasZoom_),
                         IM_COL32(255, 255, 0, 200), 0, 0, 2.0f);

        ImGui::SetTooltip("(%d, %d)", px, py);

        if (mouseClicked || (mouseDown && (px != lastDrawX_ || py != lastDrawY_))) {
            switch (currentTool_) {
                case Tool::Pencil:
                    setPixel(px, py, colorFromFloat(currentColor_));
                    hasUnsavedChanges_ = true;
                    break;
                case Tool::Eraser:
                    setPixel(px, py, 0); // Transparent
                    hasUnsavedChanges_ = true;
                    break;
                case Tool::Fill:
                    if (mouseClicked) {
                        uint32_t target = getPixel(px, py);
                        uint32_t fill = colorFromFloat(currentColor_);
                        if (target != fill) {
                            floodFill(px, py, target, fill);
                            hasUnsavedChanges_ = true;
                        }
                    }
                    break;
                case Tool::Eyedropper:
                    if (mouseClicked) {
                        uint32_t px_col = getPixel(px, py);
                        currentColor_[0] = (px_col & 0xFF) / 255.0f;
                        currentColor_[1] = ((px_col >> 8) & 0xFF) / 255.0f;
                        currentColor_[2] = ((px_col >> 16) & 0xFF) / 255.0f;
                        currentColor_[3] = ((px_col >> 24) & 0xFF) / 255.0f;
                        currentTool_ = Tool::Pencil; // Switch back after pick
                    }
                    break;
            }
            lastDrawX_ = px;
            lastDrawY_ = py;
        }
    }

    if (!mouseDown) {
        lastDrawX_ = lastDrawY_ = -1;
    }
}

void TextureEditorPanel::renderPreview() {
    if (selectedMaterialID_ < 0) return;

    ImGui::Text("Preview (1x)");
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Draw 1x preview
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            uint32_t px = getPixel(x, y);
            uint8_t r = px & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF, a = (px >> 24) & 0xFF;
            if (a > 0) {
                dl->AddRectFilled(
                    ImVec2(pos.x + x, pos.y + y),
                    ImVec2(pos.x + x + 1, pos.y + y + 1),
                    IM_COL32(r, g, b, a));
            }
        }
    }
    ImGui::Dummy(ImVec2((float)TEX_SIZE, (float)TEX_SIZE));

    // Buttons
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button("Apply", ImVec2(60, 0))) {
        applyToAtlas();
    }
    if (ImGui::Button("Save", ImVec2(60, 0))) {
        saveSourcePNG();
    }
    if (ImGui::Button("Revert", ImVec2(60, 0))) {
        loadCurrentTexture();
    }
    ImGui::EndGroup();

    // Pack preview: shown inline when a preview is loaded
    if (hasPreview_) {
        ImGui::SameLine(0, 24);
        ImGui::BeginGroup();
        ImGui::Text("Preview: %s", previewPackName_.c_str());
        ImVec2 previewPos = ImGui::GetCursorScreenPos();
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        int pz = previewZoom_;
        for (int y = 0; y < TEX_SIZE; y++) {
            for (int x = 0; x < TEX_SIZE; x++) {
                int idx = (y * TEX_SIZE + x) * 4;
                uint8_t r = previewPixels_[idx], g = previewPixels_[idx+1],
                        b = previewPixels_[idx+2], a = previewPixels_[idx+3];
                if (a > 0) {
                    pdl->AddRectFilled(
                        ImVec2(previewPos.x + x*pz, previewPos.y + y*pz),
                        ImVec2(previewPos.x + x*pz + pz, previewPos.y + y*pz + pz),
                        IM_COL32(r, g, b, a));
                }
            }
        }
        ImGui::Dummy(ImVec2((float)(TEX_SIZE * pz), (float)(TEX_SIZE * pz)));
        if (ImGui::Button("Use This##adopt", ImVec2(80, 0))) {
            adoptPreview();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard##discard", ImVec2(60, 0))) {
            hasPreview_ = false;
            previewPackName_.clear();
        }
        ImGui::EndGroup();
    }
}

// --- Actions ---

void TextureEditorPanel::loadCurrentTexture() {
    if (selectedMaterialID_ < 0) return;

    auto& reg = Core::MaterialRegistry::instance();
    uint16_t atlasIdx = reg.getTextureIndex(selectedMaterialID_, selectedFaceID_);
    if (atlasIdx == Core::MaterialRegistry::INVALID_TEXTURE_INDEX) return;

    auto& atlas = Core::AtlasManager::instance();
    auto slotPixels = atlas.getTextureSlotPixels(atlasIdx);
    if (slotPixels.size() == pixels_.size()) {
        std::memcpy(pixels_.data(), slotPixels.data(), pixels_.size());
    }
    hasUnsavedChanges_ = false;

    LOG_DEBUG("TextureEditor", "Loaded texture: {} face {} (atlas idx {})",
              selectedMaterialName_, selectedFaceID_, atlasIdx);
}

void TextureEditorPanel::applyToAtlas() {
    if (selectedMaterialID_ < 0) return;

    auto& reg = Core::MaterialRegistry::instance();
    uint16_t atlasIdx = reg.getTextureIndex(selectedMaterialID_, selectedFaceID_);
    if (atlasIdx == Core::MaterialRegistry::INVALID_TEXTURE_INDEX) return;

    auto& atlas = Core::AtlasManager::instance();
    atlas.updateTextureSlot(atlasIdx, pixels_.data());

    // Hot-reload: upload to GPU + update SSBO
    if (vulkanDevice_) {
        atlas.uploadToGPU(vulkanDevice_);
        atlas.updateUVSSBO(vulkanDevice_);
    }

    hasUnsavedChanges_ = false;
    LOG_INFO("TextureEditor", "Applied texture to atlas (slot {})", atlasIdx);
}

void TextureEditorPanel::reloadMaterialFromDisk() {
    if (selectedMaterialID_ < 0 || !vulkanDevice_) return;

    auto& atlas = Core::AtlasManager::instance();
    bool ok = atlas.reloadMaterial(selectedMaterialName_, vulkanDevice_);
    if (ok) {
        // Refresh canvas display from updated atlas
        loadCurrentTexture();
        LOG_INFO("TextureEditor", "Reloaded material '{}' from disk", selectedMaterialName_);
    } else {
        LOG_ERROR("TextureEditor", "Failed to reload material '{}' from disk", selectedMaterialName_);
    }
}

void TextureEditorPanel::hotReloadAll() {
    if (!vulkanDevice_) return;

    auto& atlas = Core::AtlasManager::instance();
    bool ok = atlas.hotReload(vulkanDevice_);
    if (ok) {
        loadCurrentTexture();
        LOG_INFO("TextureEditor", "Hot-reloaded all materials");
    } else {
        LOG_ERROR("TextureEditor", "Full atlas hot-reload failed");
    }
}

void TextureEditorPanel::saveSourcePNG() {
    if (selectedMaterialID_ < 0) return;

    auto& reg = Core::MaterialRegistry::instance();
    const auto* mat = reg.getMaterial(selectedMaterialName_);
    if (!mat) return;

    // Get source filename
    const std::string& filename = mat->textures.faceFiles[selectedFaceID_];
    std::string path = "resources/textures/source/" + filename;

    int result = stbi_write_png(path.c_str(), TEX_SIZE, TEX_SIZE, 4, pixels_.data(), TEX_SIZE * 4);
    if (result) {
        hasUnsavedChanges_ = false;
        LOG_INFO("TextureEditor", "Saved source PNG: {}", path);
    } else {
        LOG_ERROR("TextureEditor", "Failed to save PNG: {}", path);
    }
}

void TextureEditorPanel::floodFill(int x, int y, uint32_t targetColor, uint32_t fillColor) {
    if (targetColor == fillColor) return;

    std::queue<std::pair<int, int>> q;
    q.push({x, y});

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();

        if (cx < 0 || cx >= TEX_SIZE || cy < 0 || cy >= TEX_SIZE) continue;
        if (getPixel(cx, cy) != targetColor) continue;

        setPixel(cx, cy, fillColor);

        q.push({cx + 1, cy});
        q.push({cx - 1, cy});
        q.push({cx, cy + 1});
        q.push({cx, cy - 1});
    }
}

// --- Helpers ---

uint32_t TextureEditorPanel::getPixel(int x, int y) const {
    if (x < 0 || x >= TEX_SIZE || y < 0 || y >= TEX_SIZE) return 0;
    int idx = (y * TEX_SIZE + x) * 4;
    return pixels_[idx] | (pixels_[idx + 1] << 8) | (pixels_[idx + 2] << 16) | (pixels_[idx + 3] << 24);
}

void TextureEditorPanel::setPixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= TEX_SIZE || y < 0 || y >= TEX_SIZE) return;
    int idx = (y * TEX_SIZE + x) * 4;
    pixels_[idx + 0] = color & 0xFF;
    pixels_[idx + 1] = (color >> 8) & 0xFF;
    pixels_[idx + 2] = (color >> 16) & 0xFF;
    pixels_[idx + 3] = (color >> 24) & 0xFF;
}

uint32_t TextureEditorPanel::colorFromFloat(const float col[4]) const {
    uint8_t r = static_cast<uint8_t>(std::clamp(col[0], 0.0f, 1.0f) * 255.0f);
    uint8_t g = static_cast<uint8_t>(std::clamp(col[1], 0.0f, 1.0f) * 255.0f);
    uint8_t b = static_cast<uint8_t>(std::clamp(col[2], 0.0f, 1.0f) * 255.0f);
    uint8_t a = static_cast<uint8_t>(std::clamp(col[3], 0.0f, 1.0f) * 255.0f);
    return r | (g << 8) | (b << 16) | (a << 24);
}

void TextureEditorPanel::loadPNG(const std::string& path,
                                  std::array<uint8_t, 64*64*4>& buf, bool& ok) {
    ok = false;
    int w, h, ch;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!data) return;
    // Nearest-neighbour scale to TEX_SIZE x TEX_SIZE
    for (int dy = 0; dy < TEX_SIZE; dy++) {
        for (int dx = 0; dx < TEX_SIZE; dx++) {
            int sx = dx * w / TEX_SIZE;
            int sy = dy * h / TEX_SIZE;
            int srcIdx = (sy * w + sx) * 4;
            int dstIdx = (dy * TEX_SIZE + dx) * 4;
            buf[dstIdx]   = data[srcIdx];
            buf[dstIdx+1] = data[srcIdx+1];
            buf[dstIdx+2] = data[srcIdx+2];
            buf[dstIdx+3] = data[srcIdx+3];
        }
    }
    stbi_image_free(data);
    ok = true;
}

void TextureEditorPanel::renderPackCompare() {
    if (selectedMaterialID_ < 0) return;

    ImGui::Text("Pack Compare");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
    for (const auto& pack : knownPacks_) {
        bool isActive = (hasPreview_ && previewPackName_ == pack);
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(pack.c_str(), ImVec2(-1, 0))) {
            loadPackPreview(pack);
        }
        if (isActive) ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    if (hasPreview_) {
        ImGui::TextDisabled("See preview on right →");
    }
}

void TextureEditorPanel::loadPackPreview(const std::string& packName) {
    // Build the command string for the preview subcommand
    // Face index → name mapping (matches FACE_NAMES order)
    static const char* faceKeys[] = { "side_n", "side_s", "side_e", "side_w", "top", "bottom" };
    const char* faceName = faceKeys[selectedFaceID_];

    std::string outPath = "resources/textures/preview_temp.png";
    std::string cmd = "python tools/mc_texture_converter.py preview "
        + selectedMaterialName_ + " " + std::string(faceName)
        + " --pack " + packName
        + " --output " + outPath;

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        LOG_WARN("TextureEditor", "Pack preview extraction failed for '{}' pack '{}'",
                 selectedMaterialName_, packName);
        return;
    }

    bool ok = false;
    loadPNG(outPath, previewPixels_, ok);
    if (ok) {
        hasPreview_ = true;
        previewPackName_ = packName;
        LOG_INFO("TextureEditor", "Loaded pack preview: {} {} from {}", selectedMaterialName_, faceName, packName);
    } else {
        LOG_WARN("TextureEditor", "Failed to load preview PNG: {}", outPath);
    }
}

void TextureEditorPanel::adoptPreview() {
    if (!hasPreview_) return;
    pixels_ = previewPixels_;
    hasUnsavedChanges_ = true;
    hasPreview_ = false;
    previewPackName_.clear();
    LOG_INFO("TextureEditor", "Adopted pack preview into canvas");
}

} // namespace Phyxel::Editor


