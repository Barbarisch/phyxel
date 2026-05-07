#include "MenuEditorPanel.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Phyxel::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

MenuElementType MenuEditorPanel::typeFromString(const std::string& s) {
    if (s == "button") return MenuElementType::Button;
    if (s == "label")  return MenuElementType::Label;
    return MenuElementType::Panel;
}

const char* MenuEditorPanel::typeToString(MenuElementType t) {
    switch (t) {
        case MenuElementType::Button: return "button";
        case MenuElementType::Label:  return "label";
        case MenuElementType::Panel:  return "panel";
    }
    return "button";
}

std::string MenuEditorPanel::nextElementId(const char* prefix) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s_%d", prefix, m_nextId++);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

MenuEditorPanel::MenuEditorPanel() = default;

// ─────────────────────────────────────────────────────────────────────────────
// JSON I/O
// ─────────────────────────────────────────────────────────────────────────────

void MenuEditorPanel::loadFromJson(const nlohmann::json& j) {
    m_elements.clear();
    m_selectedIdx = -1;
    m_nextId      = 1;

    // Background
    std::string bgType = j.value("background_type", "solid");
    m_backgroundType = (bgType == "world_scene") ? MenuBackgroundType::WorldScene
                                                  : MenuBackgroundType::SolidColor;
    if (j.contains("background_color") && j["background_color"].is_array()
            && j["background_color"].size() >= 4) {
        auto& a = j["background_color"];
        m_backgroundColor = {a[0].get<float>(), a[1].get<float>(),
                             a[2].get<float>(), a[3].get<float>()};
    }

    // Elements from "children" array (MenuDefinition schema)
    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& child : j["children"]) {
            MenuElementDef el;
            el.id     = child.value("id", nextElementId("elem"));
            el.type   = typeFromString(child.value("type", "button"));
            el.text   = child.value("text", child.value("label", ""));

            // Position and size from "position" array [x, y] and "size" array [w, h]
            if (child.contains("position") && child["position"].is_array()) {
                el.x = child["position"][0].get<float>();
                el.y = child["position"][1].get<float>();
            }
            if (child.contains("size") && child["size"].is_array()) {
                el.width  = child["size"][0].get<float>();
                el.height = child["size"][1].get<float>();
            }
            if (child.contains("color") && child["color"].is_array()) {
                auto& c = child["color"];
                el.color = {c[0].get<float>(), c[1].get<float>(),
                            c[2].get<float>(), c[3].get<float>()};
            }
            if (child.contains("text_color") && child["text_color"].is_array()) {
                auto& c = child["text_color"];
                el.textColor = {c[0].get<float>(), c[1].get<float>(),
                                c[2].get<float>(), c[3].get<float>()};
            }
            if (child.contains("action")) {
                el.action.type   = child["action"].value("type", "");
                el.action.target = child["action"].value("target", "");
            }
            m_elements.push_back(std::move(el));
        }
    }
}

nlohmann::json MenuEditorPanel::toJson() const {
    nlohmann::json j;
    j["title"]           = m_sceneName;
    j["anchor"]          = "TopLeft";
    j["size"]            = {kVirtualW, kVirtualH};
    j["background_type"] = (m_backgroundType == MenuBackgroundType::WorldScene)
                               ? "world_scene" : "solid";
    j["background_color"] = {m_backgroundColor.x, m_backgroundColor.y,
                              m_backgroundColor.z, m_backgroundColor.w};

    nlohmann::json children = nlohmann::json::array();
    for (const auto& el : m_elements) {
        nlohmann::json c;
        c["id"]       = el.id;
        c["type"]     = typeToString(el.type);
        c["text"]     = el.text;
        c["position"] = {el.x, el.y};
        c["size"]     = {el.width, el.height};
        c["color"]    = {el.color.x, el.color.y, el.color.z, el.color.w};
        c["text_color"] = {el.textColor.x, el.textColor.y,
                           el.textColor.z, el.textColor.w};
        if (!el.action.type.empty()) {
            c["action"]["type"]   = el.action.type;
            c["action"]["target"] = el.action.target;
        }
        children.push_back(std::move(c));
    }
    j["children"] = std::move(children);
    return j;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void MenuEditorPanel::render(bool* open) {
    if (!ImGui::Begin(m_sceneName.empty() ? "Menu Editor" :
                      ("Menu Editor — " + m_sceneName).c_str(), open)) {
        ImGui::End();
        return;
    }

    renderToolbar();
    ImGui::Separator();

    // Three-column layout: element list | canvas | properties
    float listW = 180.0f;
    float propW = 220.0f;
    float canvasW = ImGui::GetContentRegionAvail().x - listW - propW - 16.0f;
    canvasW = (canvasW < 200.0f) ? 200.0f : canvasW;

    // Left — element list
    ImGui::BeginChild("##menu_elem_list", ImVec2(listW, 0), true);
    renderElementList();
    ImGui::EndChild();

    ImGui::SameLine();

    // Center — canvas
    ImGui::BeginChild("##menu_canvas", ImVec2(canvasW, 0), true);
    renderCanvas();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right — properties
    ImGui::BeginChild("##menu_props", ImVec2(propW, 0), true);
    renderElementProperties();
    ImGui::EndChild();

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Toolbar
// ─────────────────────────────────────────────────────────────────────────────

void MenuEditorPanel::renderToolbar() {
    if (ImGui::Button("+ Button")) {
        MenuElementDef el;
        el.id     = nextElementId("btn");
        el.type   = MenuElementType::Button;
        el.text   = "New Button";
        el.x      = 540.0f;
        el.y      = 300.0f + static_cast<float>(m_elements.size()) * 60.0f;
        el.width  = 200.0f;
        el.height = 48.0f;
        m_elements.push_back(std::move(el));
        m_selectedIdx = static_cast<int>(m_elements.size()) - 1;
        m_editDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Label")) {
        MenuElementDef el;
        el.id     = nextElementId("lbl");
        el.type   = MenuElementType::Label;
        el.text   = "New Label";
        el.x      = 440.0f;
        el.y      = 200.0f;
        el.width  = 400.0f;
        el.height = 40.0f;
        el.color  = {0.0f, 0.0f, 0.0f, 0.0f}; // transparent bg
        el.textColor = {1.0f, 1.0f, 1.0f, 1.0f};
        m_elements.push_back(std::move(el));
        m_selectedIdx = static_cast<int>(m_elements.size()) - 1;
        m_editDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Panel")) {
        MenuElementDef el;
        el.id     = nextElementId("pnl");
        el.type   = MenuElementType::Panel;
        el.text   = "";
        el.x      = 340.0f;
        el.y      = 160.0f;
        el.width  = 600.0f;
        el.height = 400.0f;
        el.color  = {0.12f, 0.12f, 0.15f, 0.90f};
        m_elements.push_back(std::move(el));
        m_selectedIdx = static_cast<int>(m_elements.size()) - 1;
        m_editDirty = true;
    }
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    if (m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(m_elements.size())) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("Delete")) {
            m_elements.erase(m_elements.begin() + m_selectedIdx);
            m_selectedIdx = -1;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.20f, 1.0f));
    if (ImGui::Button("Save Layout")) {
        if (onSave) {
            onSave(toJson());
        }
    }
    ImGui::PopStyleColor();

    // Background color picker
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::Text("Background:");
    ImGui::SameLine();
    ImGui::ColorEdit4("##bg_color", reinterpret_cast<float*>(&m_backgroundColor),
                      ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);
}

// ─────────────────────────────────────────────────────────────────────────────
// Element list (left panel)
// ─────────────────────────────────────────────────────────────────────────────

void MenuEditorPanel::renderElementList() {
    ImGui::TextDisabled("Elements (%d)", static_cast<int>(m_elements.size()));
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(m_elements.size()); ++i) {
        auto& el = m_elements[i];
        const char* icon = (el.type == MenuElementType::Button) ? "[BTN]" :
                           (el.type == MenuElementType::Label)  ? "[LBL]" : "[PNL]";
        char label[200];
        std::snprintf(label, sizeof(label), "%s %s##elem_%d", icon, el.text.c_str(), i);
        bool selected = (m_selectedIdx == i);
        if (ImGui::Selectable(label, selected)) {
            if (m_selectedIdx != i) {
                m_selectedIdx = i;
                m_editDirty = true;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Canvas (center)
// ─────────────────────────────────────────────────────────────────────────────

void MenuEditorPanel::renderCanvas() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float scaleX = avail.x / kVirtualW;
    float scaleY = avail.y / kVirtualH;
    float scale  = (scaleX < scaleY) ? scaleX : scaleY;

    float canvasW = kVirtualW * scale;
    float canvasH = kVirtualH * scale;

    // Center the canvas within the available region
    float padX = (avail.x - canvasW) * 0.5f;
    float padY = (avail.y - canvasH) * 0.5f;
    if (padX < 0.0f) padX = 0.0f;
    if (padY < 0.0f) padY = 0.0f;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + padX,
                               ImGui::GetCursorPosY() + padY));

    ImVec2 origin = ImGui::GetCursorScreenPos();

    // Invisible button over the full canvas for input detection
    ImGui::InvisibleButton("##canvas_area", ImVec2(canvasW, canvasH));
    bool canvasHovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + canvasW, origin.y + canvasH),
                      ImGui::ColorConvertFloat4ToU32(m_backgroundColor));
    dl->AddRect(origin,
                ImVec2(origin.x + canvasW, origin.y + canvasH),
                IM_COL32(100, 100, 140, 255), 0.0f, 0, 1.5f);

    // Draw elements back-to-front
    ImVec2 mousePos = ImGui::GetIO().MousePos;

    for (int i = 0; i < static_cast<int>(m_elements.size()); ++i) {
        auto& el = m_elements[i];
        ImVec2 eMin = {origin.x + el.x * scale, origin.y + el.y * scale};
        ImVec2 eMax = {eMin.x + el.width * scale, eMin.y + el.height * scale};

        // Background rect
        dl->AddRectFilled(eMin, eMax,
                          ImGui::ColorConvertFloat4ToU32(el.color), 4.0f * scale);

        // Text
        if (!el.text.empty()) {
            float fontSize = ImGui::GetFontSize() * scale;
            // Clamp so text stays visible
            if (fontSize < 8.0f)  fontSize = 8.0f;
            if (fontSize > 24.0f) fontSize = 24.0f;
            float textY = eMin.y + (el.height * scale - fontSize) * 0.5f;
            float measW = ImGui::CalcTextSize(el.text.c_str()).x * (fontSize / ImGui::GetFontSize());
            float textX = eMin.x + (el.width * scale - measW) * 0.5f;
            dl->AddText(nullptr, fontSize,
                        ImVec2(textX, textY),
                        ImGui::ColorConvertFloat4ToU32(el.textColor),
                        el.text.c_str());
        }

        // Selection highlight
        bool isSelected = (m_selectedIdx == i);
        if (isSelected) {
            dl->AddRect(eMin, eMax, IM_COL32(255, 200, 50, 255), 4.0f * scale, 0, 2.0f);
        }

        // Click / drag handling
        if (canvasHovered && ImGui::IsMouseClicked(0)) {
            if (mousePos.x >= eMin.x && mousePos.x <= eMax.x &&
                mousePos.y >= eMin.y && mousePos.y <= eMax.y) {
                if (m_selectedIdx != i) {
                    m_selectedIdx = i;
                    m_editDirty = true;
                }
                m_dragging = true;
                m_dragOffset = {mousePos.x - eMin.x, mousePos.y - eMin.y};
            }
        }

        // Drag move
        if (m_dragging && m_selectedIdx == i && ImGui::IsMouseDown(0)) {
            float newX = (mousePos.x - origin.x - m_dragOffset.x) / scale;
            float newY = (mousePos.y - origin.y - m_dragOffset.y) / scale;
            // Clamp to canvas
            if (newX < 0.0f) newX = 0.0f;
            if (newY < 0.0f) newY = 0.0f;
            if (newX + el.width > kVirtualW) newX = kVirtualW - el.width;
            if (newY + el.height > kVirtualH) newY = kVirtualH - el.height;
            el.x = newX;
            el.y = newY;
        }
    }

    // Stop dragging
    if (!ImGui::IsMouseDown(0)) {
        m_dragging = false;
    }

    // Click on empty area deselects
    if (canvasHovered && ImGui::IsMouseClicked(0)) {
        bool hitAny = false;
        for (const auto& el : m_elements) {
            ImVec2 eMin = {origin.x + el.x * scale, origin.y + el.y * scale};
            ImVec2 eMax = {eMin.x + el.width * scale, eMin.y + el.height * scale};
            if (mousePos.x >= eMin.x && mousePos.x <= eMax.x &&
                mousePos.y >= eMin.y && mousePos.y <= eMax.y) {
                hitAny = true;
                break;
            }
        }
        if (!hitAny) {
            m_selectedIdx = -1;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Element properties (right panel)
// ─────────────────────────────────────────────────────────────────────────────

void MenuEditorPanel::renderElementProperties() {
    if (m_selectedIdx < 0 || m_selectedIdx >= static_cast<int>(m_elements.size())) {
        ImGui::TextDisabled("Select an element\nto edit properties.");
        return;
    }

    auto& el = m_elements[m_selectedIdx];

    // Repopulate buffers on selection change
    if (m_editDirty) {
        std::strncpy(m_editId,     el.id.c_str(),            sizeof(m_editId)     - 1);
        std::strncpy(m_editText,   el.text.c_str(),          sizeof(m_editText)   - 1);
        std::strncpy(m_editTarget, el.action.target.c_str(), sizeof(m_editTarget) - 1);
        m_editId[sizeof(m_editId)-1]         = '\0';
        m_editText[sizeof(m_editText)-1]     = '\0';
        m_editTarget[sizeof(m_editTarget)-1] = '\0';
        m_editDirty = false;
    }

    ImGui::TextDisabled("Properties");
    ImGui::Separator();

    // ID
    ImGui::Text("ID");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##el_id", m_editId, sizeof(m_editId),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        el.id = m_editId;
    }

    // Type (read-only display)
    ImGui::Text("Type: %s", typeToString(el.type));

    // Text / label
    ImGui::Text("Text");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##el_text", m_editText, sizeof(m_editText),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        el.text = m_editText;
    }

    ImGui::Separator();
    ImGui::Text("Position");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat2("##pos", &el.x, 1.0f, 0.0f, kVirtualW)) {}

    ImGui::Text("Size");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat2("##sz", &el.width, 1.0f, 4.0f, kVirtualW)) {}

    ImGui::Separator();
    ImGui::Text("Background Color");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::ColorEdit4("##el_col", reinterpret_cast<float*>(&el.color),
                      ImGuiColorEditFlags_NoLabel);
    ImGui::Text("Text Color");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::ColorEdit4("##el_tcol", reinterpret_cast<float*>(&el.textColor),
                      ImGuiColorEditFlags_NoLabel);

    // Button-specific: action
    if (el.type == MenuElementType::Button) {
        ImGui::Separator();
        ImGui::Text("Button Action");

        static const char* actionTypes[] = {
            "(none)", "transition_scene", "quit_game", "open_submenu"
        };
        int currentAction = 0;
        for (int i = 1; i < 4; ++i) {
            if (el.action.type == actionTypes[i]) { currentAction = i; break; }
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##action_type", &currentAction, actionTypes, 4)) {
            el.action.type = (currentAction == 0) ? "" : actionTypes[currentAction];
            m_editDirty = true;
        }
        if (currentAction > 0) {
            ImGui::Text("Target");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##action_target", m_editTarget, sizeof(m_editTarget),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                el.action.target = m_editTarget;
            }
        }
    }
}

} // namespace Phyxel::Editor
