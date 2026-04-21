#include "TextureEditorPanel.h"
#include "core/AtlasManager.h"
#include "core/MaterialRegistry.h"
#include "vulkan/VulkanDevice.h"
#include "utils/Logger.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <queue>

// stb_image_write for saving individual PNGs
#include "stb_image_write.h"

namespace Phyxel::Editor {

constexpr const char* TextureEditorPanel::FACE_NAMES[6];

void TextureEditorPanel::render(bool* open) {
    if (!ImGui::Begin("Texture Editor", open, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

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
        ImGui::EndMenuBar();
    }

    // Left column: material + face selection
    ImGui::BeginChild("LeftPane", ImVec2(180, 0), true);
    renderMaterialSelector();
    ImGui::Separator();
    renderFaceSelector();
    ImGui::Separator();
    renderToolbar();
    ImGui::Separator();
    renderColorPicker();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right column: canvas + preview
    ImGui::BeginChild("RightPane", ImVec2(0, 0), false);
    renderCanvas();
    ImGui::Separator();
    renderPreview();
    ImGui::EndChild();

    ImGui::End();
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

    ImGui::SliderInt("Zoom", &canvasZoom_, 8, 32);
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
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
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

            // Grid lines
            drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(80, 80, 80, 60));
        }
    }

    // Invisible button for mouse interaction
    ImGui::InvisibleButton("##canvas", ImVec2((float)canvasPixels, (float)canvasPixels));

    bool hovered = ImGui::IsItemHovered();
    bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

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

} // namespace Phyxel::Editor
