#include "ui/GameMenuRenderer.h"
#include "vulkan/VulkanDevice.h"
#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Phyxel {
namespace UI {

// ============================================================================
// Constructor / Destructor
// ============================================================================

GameMenuRenderer::GameMenuRenderer() = default;
GameMenuRenderer::~GameMenuRenderer() = default;

// ============================================================================
// Layout loading
// ============================================================================

void GameMenuRenderer::load(const nlohmann::json& layout, Vulkan::VulkanDevice* vulkanDevice) {
    layout_      = layout;
    device_      = vulkanDevice;
    elapsed_     = 0.0f;
    bgTextureId_ = nullptr;
    submenuStack_.clear();
    animStartTimes_.clear();
    overrides_.clear();
    imageTextureCache_.clear();

    // Load fonts declared in the layout. These must be called before ImGui renders
    // the first frame so the atlas can be rebuilt in time.
    if (layout_.contains("fonts") && layout_["fonts"].is_array()) {
        ImGuiIO& io = ImGui::GetIO();
        bool needsRebuild = false;
        for (const auto& fontDef : layout_["fonts"]) {
            std::string id   = fontDef.value("id", "");
            std::string file = fontDef.value("file", "");
            float size       = fontDef.value("size", 24.0f);
            if (id.empty() || file.empty()) continue;
            if (fontRegistry_.count(id)) continue; // already loaded
            ImFont* font = io.Fonts->AddFontFromFileTTF(file.c_str(), size);
            if (font) {
                fontRegistry_[id] = font;
                needsRebuild = true;
                LOG_INFO("GameMenuRenderer", "Loaded font '{}' from '{}' @ {}px", id, file, size);
            } else {
                LOG_WARN("GameMenuRenderer", "Failed to load font '{}' from '{}'", id, file);
            }
        }
        // In ImGui 1.92+, font atlas is rebuilt automatically by the backend on next frame.
        (void)needsRebuild;
    }

    // Preload background image
    if (layout_.value("background_type", "solid") == "image") {
        std::string imgPath = layout_.value("background_image", "");
        if (!imgPath.empty() && device_) {
            bgTextureId_ = device_->loadImGuiTexture(imgPath);
        }
    }
}

void GameMenuRenderer::unload() {
    layout_ = nlohmann::json{};
    device_ = nullptr;
    bgTextureId_ = nullptr;
    imageTextureCache_.clear();
    submenuStack_.clear();
    animStartTimes_.clear();
    overrides_.clear();
    elapsed_ = 0.0f;
    // Note: fonts stay in ImGui's atlas for the session lifetime.
}

// ============================================================================
// Per-frame render
// ============================================================================

bool GameMenuRenderer::render(float dt) {
    if (layout_.is_null()) return false;

    elapsed_ += dt;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 0 || displaySize.y <= 0) return false;

    // Full-screen invisible window so we can render into its DrawList
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar   | ImGuiWindowFlags_NoResize    |
                          ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoCollapse   | ImGuiWindowFlags_NoNav       |
                          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDecoration |
                          ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##GameMenuBackground", nullptr, wf);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float scaleX = displaySize.x / kVirtualW;
    float scaleY = displaySize.y / kVirtualH;

    bool anyClick = renderInternal(dl, displaySize, scaleX, scaleY, dt);

    ImGui::End();
    return anyClick;
}

bool GameMenuRenderer::renderInternal(ImDrawList* dl, ImVec2 screenSize,
                                      float scaleX, float scaleY, float dt) {
    // ── Background ──────────────────────────────────────────────────────────
    std::string bgType = layout_.value("background_type", "solid");
    if (bgType == "image" && bgTextureId_) {
        dl->AddImage(reinterpret_cast<ImTextureID>(bgTextureId_),
                     ImVec2(0, 0), screenSize,
                     ImVec2(0, 0), ImVec2(1, 1));
    } else {
        ImVec4 bgCol = parseColor(layout_.value("background_color", nlohmann::json{}),
                                  {0.05f, 0.05f, 0.1f, 1.0f});
        dl->AddRectFilled(ImVec2(0, 0), screenSize,
                          ImGui::ColorConvertFloat4ToU32(bgCol));
    }

    // ── Active panel ────────────────────────────────────────────────────────
    std::string panelKey = currentPanelKey();
    const nlohmann::json* panelJson = nullptr;
    if (layout_.contains("panels") && layout_["panels"].is_object()) {
        if (layout_["panels"].contains(panelKey)) {
            panelJson = &layout_["panels"][panelKey];
        }
    }
    // Fallback: root-level "children" array
    if (!panelJson && layout_.contains("children") && layout_["children"].is_array()) {
        panelJson = &layout_;
    }
    if (!panelJson) return false;

    bool anyClick = false;
    if (panelJson->contains("children") && (*panelJson)["children"].is_array()) {
        for (const auto& elem : (*panelJson)["children"]) {
            renderElement(dl, elem, ImVec2(0, 0), scaleX, scaleY, dt, anyClick);
        }
    }
    return anyClick;
}

// ============================================================================
// Element rendering
// ============================================================================

void GameMenuRenderer::renderElement(ImDrawList* dl, const nlohmann::json& elem,
                                     ImVec2 /*origin*/, float scaleX, float scaleY,
                                     float dt, bool& anyClick) {
    std::string id   = elem.value("id", "");
    std::string type = elem.value("type", "");

    // Apply overrides
    auto ovIt = overrides_.find(id);
    bool visible = elem.value("visible", true);
    if (ovIt != overrides_.end() && ovIt->second.visible.has_value())
        visible = ovIt->second.visible.value();
    if (!visible) return;

    // Animation
    float alpha  = getAnimAlpha(elem, dt);
    ImVec2 slide = getAnimOffset(elem, dt);

    // Position + size from virtual canvas
    float vx = 0.0f, vy = 0.0f, vw = 100.0f, vh = 40.0f;
    if (elem.contains("position") && elem["position"].is_array() && elem["position"].size() >= 2) {
        vx = elem["position"][0].get<float>();
        vy = elem["position"][1].get<float>();
    }
    if (elem.contains("size") && elem["size"].is_array() && elem["size"].size() >= 2) {
        vw = elem["size"][0].get<float>();
        vh = elem["size"][1].get<float>();
    }

    // Center-origin: if x is given as the horizontal center position
    // (Layout authored at 1280x720, so 640 = screen center)
    float px = vx * scaleX + slide.x * scaleX;
    float py = vy * scaleY + slide.y * scaleY;
    float sw = vw * scaleX;
    float sh = vh * scaleY;

    // Colors
    ImVec4 bgColorDef = parseColor(elem.value("color", nlohmann::json{}), {0.2f, 0.2f, 0.25f, 0.85f});
    ImVec4 bgColorHov = parseColor(elem.value("color_hover", nlohmann::json{}),
                                   {bgColorDef.x + 0.1f, bgColorDef.y + 0.1f,
                                    bgColorDef.z + 0.12f, bgColorDef.w});
    ImVec4 textColorDef = parseColor(elem.value("text_color", nlohmann::json{}), {0.95f, 0.92f, 0.85f, 1.0f});

    // Runtime text override
    std::string text = elem.value("text", "");
    if (ovIt != overrides_.end() && ovIt->second.text.has_value())
        text = ovIt->second.text.value();

    // Runtime color override
    if (ovIt != overrides_.end() && ovIt->second.color.has_value())
        bgColorDef = ovIt->second.color.value();

    // Font
    std::string fontId = elem.value("font", "");
    ImFont* font = getFont(fontId);
    float fontSize = (font ? font->LegacySize : ImGui::GetFontSize());

    // ── Per-type rendering ────────────────────────────────────────────────────

    if (type == "panel") {
        // Draw panel background
        bgColorDef.w *= alpha;
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + sw, py + sh),
                          ImGui::ColorConvertFloat4ToU32(bgColorDef), 6.0f);

        // Render children
        if (elem.contains("children") && elem["children"].is_array()) {
            for (const auto& child : elem["children"]) {
                // Children positions are relative to the panel
                // Remap child position inside panel
                nlohmann::json adjustedChild = child;
                if (child.contains("position") && child["position"].is_array() && child["position"].size() >= 2) {
                    adjustedChild["position"][0] = vx + child["position"][0].get<float>();
                    adjustedChild["position"][1] = vy + child["position"][1].get<float>();
                }
                renderElement(dl, adjustedChild, ImVec2(px, py), scaleX, scaleY, dt, anyClick);
            }
        }

    } else if (type == "label") {
        textColorDef.w *= alpha;
        if (font) ImGui::PushFont(font);
        float measW = ImGui::CalcTextSize(text.c_str()).x;
        float measH = fontSize;
        float tx = px + (sw - measW) * 0.5f;
        float ty = py + (sh - measH) * 0.5f;
        dl->AddText(font, fontSize, ImVec2(tx, ty),
                    ImGui::ColorConvertFloat4ToU32(textColorDef), text.c_str());
        if (font) ImGui::PopFont();

    } else if (type == "button") {
        const auto* action = elem.contains("action") ? &elem["action"] : nullptr;
        bgColorDef.w *= alpha;
        bgColorHov.w *= alpha;
        textColorDef.w *= alpha;

        bool clicked = renderButton(dl, ImVec2(px, py), ImVec2(sw, sh),
                                    text, bgColorDef, bgColorHov, textColorDef,
                                    font, fontSize, alpha);
        if (clicked) {
            anyClick = true;
            if (action) handleAction(*action);
        }

    } else if (type == "image") {
        std::string imgPath = elem.value("image", elem.value("imagePath", ""));
        void* texId = getImageTexture(id, imgPath);
        ImVec4 tint = parseColor(elem.value("tint", nlohmann::json{}), {1,1,1,1});
        tint.w *= alpha;
        if (texId) {
            dl->AddImage(reinterpret_cast<ImTextureID>(texId),
                         ImVec2(px, py), ImVec2(px + sw, py + sh),
                         ImVec2(0, 0), ImVec2(1, 1),
                         ImGui::ColorConvertFloat4ToU32(tint));
        } else {
            // Placeholder
            ImVec4 placeholder = {tint.x * 0.3f, tint.y * 0.3f, tint.z * 0.3f, tint.w * 0.5f};
            dl->AddRectFilled(ImVec2(px, py), ImVec2(px + sw, py + sh),
                              ImGui::ColorConvertFloat4ToU32(placeholder), 4.0f);
            dl->AddRect(ImVec2(px, py), ImVec2(px + sw, py + sh),
                        IM_COL32(120, 120, 120, static_cast<int>(alpha * 180)), 4.0f, 0, 1.5f);
        }
    }
}

// ============================================================================
// Button renderer
// ============================================================================

bool GameMenuRenderer::renderButton(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                                    const std::string& text, ImVec4 bgColor, ImVec4 hoverColor,
                                    ImVec4 textColor, ImFont* font, float fontSize, float alpha) {
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool hovered = (mousePos.x >= pos.x && mousePos.x <= pos.x + size.x &&
                    mousePos.y >= pos.y && mousePos.y <= pos.y + size.y);
    bool clicked = hovered && ImGui::GetIO().MouseClicked[0];

    // Draw background
    ImVec4 drawColor = hovered ? hoverColor : bgColor;
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      ImGui::ColorConvertFloat4ToU32(drawColor), 6.0f);

    // Hover border
    if (hovered) {
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                    IM_COL32(220, 200, 120, static_cast<int>(alpha * 200)), 6.0f, 0, 1.5f);
    }

    // Label
    if (!text.empty()) {
        if (font) ImGui::PushFont(font);
        float measW = ImGui::CalcTextSize(text.c_str()).x;
        float measH = fontSize;
        float tx = pos.x + (size.x - measW) * 0.5f;
        float ty = pos.y + (size.y - measH) * 0.5f;
        dl->AddText(font, fontSize, ImVec2(tx, ty),
                    ImGui::ColorConvertFloat4ToU32(textColor), text.c_str());
        if (font) ImGui::PopFont();
    }

    return clicked;
}

// ============================================================================
// Animation helpers
// ============================================================================

float GameMenuRenderer::getAnimAlpha(const nlohmann::json& elem, float /*dt*/) {
    std::string anim = elem.value("animation", "");
    if (anim.empty()) return 1.0f;

    std::string id  = elem.value("id", "");
    float delay     = elem.value("animation_delay", 0.0f);
    float duration  = elem.value("animation_duration", 0.4f);

    // Record start time on first encounter
    if (!id.empty() && animStartTimes_.find(id) == animStartTimes_.end()) {
        animStartTimes_[id] = elapsed_;
    }
    float startTime = id.empty() ? 0.0f : animStartTimes_[id];
    float t = (elapsed_ - startTime - delay) / duration;
    t = std::max(0.0f, std::min(1.0f, t));
    // Ease out cubic
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

ImVec2 GameMenuRenderer::getAnimOffset(const nlohmann::json& elem, float dt) {
    std::string anim = elem.value("animation", "");
    if (anim.empty()) return {0, 0};

    std::string id  = elem.value("id", "");
    float delay     = elem.value("animation_delay", 0.0f);
    float duration  = elem.value("animation_duration", 0.4f);
    float slideAmt  = 80.0f; // virtual pixels

    float startTime = id.empty() ? 0.0f :
                      (animStartTimes_.count(id) ? animStartTimes_.at(id) : elapsed_);
    float t = (elapsed_ - startTime - delay) / duration;
    t = std::max(0.0f, std::min(1.0f, t));
    float progress = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    float remaining = (1.0f - progress);

    if (anim == "slide_in_left")  return {-slideAmt * remaining, 0};
    if (anim == "slide_in_right") return { slideAmt * remaining, 0};
    if (anim == "slide_in_up")    return {0, -slideAmt * remaining};
    return {0, 0}; // fade_in has no offset
}

// ============================================================================
// Submenu navigation
// ============================================================================

std::string GameMenuRenderer::currentPanelKey() const {
    if (!submenuStack_.empty()) return submenuStack_.back();
    return layout_.value("start_panel", "main");
}

void GameMenuRenderer::openSubmenu(const std::string& panelId) {
    submenuStack_.push_back(panelId);
    animStartTimes_.clear(); // restart animations for the new panel
    elapsed_ = 0.0f;
}

void GameMenuRenderer::closeSubmenu() {
    if (!submenuStack_.empty()) {
        submenuStack_.pop_back();
        animStartTimes_.clear();
        elapsed_ = 0.0f;
    }
}

// ============================================================================
// Action dispatch
// ============================================================================

void GameMenuRenderer::handleAction(const nlohmann::json& action) {
    std::string type   = action.value("type", "");
    std::string target = action.value("target", "");

    if (type == "transition_scene") {
        if (onTransitionScene) onTransitionScene(target);
    } else if (type == "quit_game") {
        if (onQuit) onQuit();
    } else if (type == "load_game") {
        if (onLoadGame) onLoadGame();
    } else if (type == "open_submenu") {
        openSubmenu(target);
    } else if (type == "close_submenu") {
        closeSubmenu();
    }
}

// ============================================================================
// MCP / programmatic element control
// ============================================================================

void GameMenuRenderer::setElementVisible(const std::string& id, bool visible) {
    overrides_[id].visible = visible;
}

void GameMenuRenderer::setElementText(const std::string& id, const std::string& text) {
    overrides_[id].text = text;
}

void GameMenuRenderer::setElementColor(const std::string& id, float r, float g, float b, float a) {
    overrides_[id].color = ImVec4{r, g, b, a};
}

nlohmann::json GameMenuRenderer::getElementDef(const std::string& id) const {
    const nlohmann::json* found = findElementConst(id);
    return found ? *found : nlohmann::json{};
}

void GameMenuRenderer::addElement(const nlohmann::json& elementDef, const std::string& panelId) {
    if (layout_.is_null()) return;
    std::string key = panelId.empty() ? currentPanelKey() : panelId;
    if (layout_.contains("panels") && layout_["panels"].contains(key)) {
        layout_["panels"][key]["children"].push_back(elementDef);
    } else if (layout_.contains("children")) {
        layout_["children"].push_back(elementDef);
    }
}

void GameMenuRenderer::removeElement(const std::string& id) {
    if (layout_.is_null()) return;
    // Search in panels
    if (layout_.contains("panels") && layout_["panels"].is_object()) {
        for (auto& [key, panel] : layout_["panels"].items()) {
            if (!panel.contains("children") || !panel["children"].is_array()) continue;
            auto& children = panel["children"];
            for (auto it = children.begin(); it != children.end(); ++it) {
                if (it->value("id", "") == id) {
                    children.erase(it);
                    return;
                }
            }
        }
    }
    // Fallback: root children
    if (layout_.contains("children") && layout_["children"].is_array()) {
        auto& children = layout_["children"];
        for (auto it = children.begin(); it != children.end(); ++it) {
            if (it->value("id", "") == id) {
                children.erase(it);
                return;
            }
        }
    }
}

// ============================================================================
// Element lookup helpers
// ============================================================================

static nlohmann::json* findInChildren(nlohmann::json& children, const std::string& id) {
    for (auto& child : children) {
        if (child.value("id", "") == id) return &child;
        if (child.contains("children") && child["children"].is_array()) {
            auto* found = findInChildren(child["children"], id);
            if (found) return found;
        }
    }
    return nullptr;
}

nlohmann::json* GameMenuRenderer::findElement(const std::string& id) {
    if (layout_.is_null()) return nullptr;
    if (layout_.contains("panels") && layout_["panels"].is_object()) {
        for (auto& [key, panel] : layout_["panels"].items()) {
            if (panel.contains("children") && panel["children"].is_array()) {
                auto* found = findInChildren(panel["children"], id);
                if (found) return found;
            }
        }
    }
    if (layout_.contains("children") && layout_["children"].is_array()) {
        return findInChildren(layout_["children"], id);
    }
    return nullptr;
}

const nlohmann::json* GameMenuRenderer::findElementConst(const std::string& id) const {
    return const_cast<GameMenuRenderer*>(this)->findElement(id);
}

nlohmann::json* GameMenuRenderer::findOwnerPanel(const std::string& id) {
    if (!layout_.contains("panels")) return nullptr;
    for (auto& [key, panel] : layout_["panels"].items()) {
        if (panel.contains("children") && panel["children"].is_array()) {
            for (const auto& child : panel["children"]) {
                if (child.value("id", "") == id) return &panel;
            }
        }
    }
    return nullptr;
}

// ============================================================================
// Utility
// ============================================================================

ImVec4 GameMenuRenderer::parseColor(const nlohmann::json& j, ImVec4 def) const {
    if (j.is_array() && j.size() >= 3) {
        float a = (j.size() >= 4) ? j[3].get<float>() : 1.0f;
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), a};
    }
    return def;
}

ImFont* GameMenuRenderer::getFont(const std::string& fontId) const {
    if (fontId.empty()) return nullptr;
    auto it = fontRegistry_.find(fontId);
    return (it != fontRegistry_.end()) ? it->second : nullptr;
}

void* GameMenuRenderer::getImageTexture(const std::string& elemId, const std::string& imagePath) {
    if (imagePath.empty()) return nullptr;
    auto it = imageTextureCache_.find(elemId);
    if (it != imageTextureCache_.end()) return it->second;
    // Load on first use
    void* texId = device_ ? device_->loadImGuiTexture(imagePath) : nullptr;
    imageTextureCache_[elemId] = texId;
    return texId;
}

} // namespace UI
} // namespace Phyxel
