#include "ui/MenuDefinition.h"
#include "ui/HudDataContext.h"
#include "ui/UISystem.h"
#include "ui/DialogueSystem.h"
#include "utils/Logger.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>

namespace Phyxel {
namespace UI {

Anchor MenuDefinition::parseAnchor(const std::string& str) {
    if (str == "TopLeft")      return Anchor::TopLeft;
    if (str == "TopCenter")    return Anchor::TopCenter;
    if (str == "TopRight")     return Anchor::TopRight;
    if (str == "CenterLeft")   return Anchor::CenterLeft;
    if (str == "Center")       return Anchor::Center;
    if (str == "CenterRight")  return Anchor::CenterRight;
    if (str == "BottomLeft")   return Anchor::BottomLeft;
    if (str == "BottomCenter") return Anchor::BottomCenter;
    if (str == "BottomRight")  return Anchor::BottomRight;
    return Anchor::Center;
}

std::unique_ptr<UIWidget> MenuDefinition::buildWidget(const nlohmann::json& j) {
    std::string type = j.value("type", "");

    if (type == "label") {
        auto w = std::make_unique<UILabel>();
        w->id = j.value("id", "");
        w->text = j.value("text", "");
        w->isTitle = j.value("isTitle", false);
        w->wrapWidth = j.value("wrapWidth", 0.0f);
        // "align": "center" -> text centered ON position.x; "right" -> ends at it.
        // Default left (start at position.x) preserves every existing layout.
        const std::string al = j.value("align", "left");
        if (al == "center")      w->align = UILabel::HAlign::Center;
        else if (al == "right")  w->align = UILabel::HAlign::Right;
        w->bind = j.value("bind", "");
        w->visibleWhen = j.value("visibleWhen", "");
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2) {
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        }
        // An authored width finally MEANS something: wrap to it unless the
        // author set an explicit wrapWidth. Ends the "text runs out of its
        // box" default — labels with no size stay unbounded single-line.
        if (w->wrapWidth <= 0.0f && w->size.x > 0.0f) w->wrapWidth = w->size.x;
        return w;
    }

    if (type == "button") {
        auto w = std::make_unique<UIButton>();
        w->id = j.value("id", "");
        w->text = j.value("text", "");
        w->bind = j.value("bind", "");
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2) {
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        }
        return w;
    }

    if (type == "slider") {
        auto w = std::make_unique<UISlider>();
        w->id = j.value("id", "");
        w->label = j.value("label", "");
        w->bind = j.value("bind", "");
        w->value = j.value("value", 0.5f);
        w->minVal = j.value("min", 0.0f);
        w->maxVal = j.value("max", 1.0f);
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2) {
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        }
        return w;
    }

    if (type == "checkbox") {
        auto w = std::make_unique<UICheckbox>();
        w->id = j.value("id", "");
        w->label = j.value("label", "");
        w->checked = j.value("checked", false);
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2) {
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        }
        return w;
    }

    if (type == "dropdown") {
        auto w = std::make_unique<UIDropdown>();
        w->id = j.value("id", "");
        w->label = j.value("label", "");
        w->selectedIndex = j.value("selected", 0);
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("options") && j["options"].is_array()) {
            for (auto& opt : j["options"]) {
                w->options.push_back(opt.get<std::string>());
            }
        }
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2) {
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        }
        return w;
    }

    if (type == "textinput") {
        auto w = std::make_unique<UITextInput>();
        w->id = j.value("id", "");
        w->placeholder = j.value("placeholder", "");
        w->maxLength = j.value("maxLength", 255);
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2)
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2)
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        return w;
    }

    if (type == "image") {
        auto w = std::make_unique<UIImage>();
        w->id = j.value("id", "");
        w->imagePath = j.value("image", j.value("imagePath", ""));
        w->bind = j.value("bind", "");
        w->visibleWhen = j.value("visibleWhen", "");
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2) {
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        }
        if (j.contains("tint") && j["tint"].is_array() && j["tint"].size() >= 4) {
            auto& t = j["tint"];
            w->tintColor = {t[0].get<float>(), t[1].get<float>(),
                            t[2].get<float>(), t[3].get<float>()};
        }
        return w;
    }

    if (type == "progressbar" || type == "bar") {
        auto w = std::make_unique<UIProgressBar>();
        w->id = j.value("id", "");
        w->label = j.value("label", "");
        w->bind = j.value("bind", "");
        w->visibleWhen = j.value("visibleWhen", "");
        w->value = j.value("value", 1.0f);
        w->minVal = j.value("min", 0.0f);
        w->maxVal = j.value("max", 1.0f);
        w->showValueText = j.value("showValueText", true);
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        auto parseColor = [&](const char* key, glm::vec4& dst) {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4) {
                auto& c = j[key];
                dst = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
            }
        };
        parseColor("fillColor", w->fillColor);
        parseColor("trackColor", w->trackColor);
        parseColor("borderColor", w->borderColor);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2) {
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        }
        return w;
    }

    if (type == "repeater") {
        auto w = std::make_unique<UIRepeater>();
        w->id = j.value("id", "");
        w->bind = j.value("bind", "");
        w->visibleWhen = j.value("visibleWhen", "");
        w->itemSpacing = j.value("itemSpacing", 4.0f);
        w->horizontal = j.value("horizontal", false);
        if (j.contains("item")) w->itemTemplateJson = j["item"].dump();
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2) {
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        }
        return w;
    }

    if (type == "panel") {
        auto w = std::make_unique<UIPanel>();
        w->id = j.value("id", "");
        w->title = j.value("title", "");
        w->showBackground = j.value("showBackground", true);
        w->freeLayout = j.value("freeLayout", false);
        w->clipChildren = j.value("clip", true);
        w->scrollable = j.value("scrollable", false);
        w->visibleWhen = j.value("visibleWhen", "");
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2) {
            w->position = {j["position"][0].get<float>(), j["position"][1].get<float>()};
        }
        if (j.contains("anchor")) w->anchor = parseAnchor(j["anchor"].get<std::string>());
        if (j.contains("children") && j["children"].is_array()) {
            for (auto& childJ : j["children"]) {
                auto child = buildWidget(childJ);
                if (child) w->addChild(std::move(child));
            }
        }
        return w;
    }

    return nullptr;
}

std::unique_ptr<UIPanel> MenuDefinition::buildFromJson(const std::string& jsonStr) {
    auto j = nlohmann::json::parse(jsonStr);
    return buildFromJson(j);
}

std::unique_ptr<UIPanel> MenuDefinition::buildFromJson(const nlohmann::json& j) {
    auto panel = std::make_unique<UIPanel>();
    panel->id = j.value("id", "");
    panel->title = j.value("title", "");
    panel->showBackground = j.value("showBackground", true);
    panel->freeLayout = j.value("freeLayout", false);
    panel->visibleWhen = j.value("visibleWhen", "");
    panel->visible = j.value("visible", true);
    panel->enabled = j.value("enabled", true);

    if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
        panel->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
    }
    if (j.contains("anchor")) panel->anchor = parseAnchor(j["anchor"].get<std::string>());
    if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() >= 2) {
        panel->offset = {j["offset"][0].get<float>(), j["offset"][1].get<float>()};
    }

    if (j.contains("children") && j["children"].is_array()) {
        for (auto& childJ : j["children"]) {
            auto child = buildWidget(childJ);
            if (child) panel->addChild(std::move(child));
        }
    }
    return panel;
}

std::unique_ptr<UIPanel> MenuDefinition::buildFromJson(
    const nlohmann::json& j,
    const CallbackMap& buttonCallbacks,
    const SliderCallbackMap& sliderCallbacks,
    const CheckboxCallbackMap& checkboxCallbacks,
    const DropdownCallbackMap& dropdownCallbacks)
{
    auto panel = buildFromJson(j);
    if (!panel) return nullptr;

    // Wire callbacks by ID
    for (auto& [id, cb] : buttonCallbacks) {
        auto* w = panel->findChild(id);
        if (w && w->type() == WidgetType::Button) {
            static_cast<UIButton*>(w)->onClick = cb;
        }
    }
    for (auto& [id, cb] : sliderCallbacks) {
        auto* w = panel->findChild(id);
        if (w && w->type() == WidgetType::Slider) {
            static_cast<UISlider*>(w)->onChange = cb;
        }
    }
    for (auto& [id, cb] : checkboxCallbacks) {
        auto* w = panel->findChild(id);
        if (w && w->type() == WidgetType::Checkbox) {
            static_cast<UICheckbox*>(w)->onChange = cb;
        }
    }
    for (auto& [id, cb] : dropdownCallbacks) {
        auto* w = panel->findChild(id);
        if (w && w->type() == WidgetType::Dropdown) {
            static_cast<UIDropdown*>(w)->onChange = cb;
        }
    }
    return panel;
}

nlohmann::json MenuDefinition::toJson(const UIPanel& panel) {
    nlohmann::json j;
    j["type"] = "panel";
    j["id"] = panel.id;
    j["title"] = panel.title;
    j["showBackground"] = panel.showBackground;
    j["size"] = {panel.size.x, panel.size.y};
    j["offset"] = {panel.offset.x, panel.offset.y};

    auto anchorStr = [](Anchor a) -> std::string {
        switch (a) {
            case Anchor::TopLeft:      return "TopLeft";
            case Anchor::TopCenter:    return "TopCenter";
            case Anchor::TopRight:     return "TopRight";
            case Anchor::CenterLeft:   return "CenterLeft";
            case Anchor::Center:       return "Center";
            case Anchor::CenterRight:  return "CenterRight";
            case Anchor::BottomLeft:   return "BottomLeft";
            case Anchor::BottomCenter: return "BottomCenter";
            case Anchor::BottomRight:  return "BottomRight";
        }
        return "Center";
    };
    j["anchor"] = anchorStr(panel.anchor);

    nlohmann::json childrenArr = nlohmann::json::array();
    for (auto& child : panel.children) {
        nlohmann::json cj;
        cj["id"] = child->id;
        cj["visible"] = child->visible;
        cj["enabled"] = child->enabled;
        cj["size"] = {child->size.x, child->size.y};

        switch (child->type()) {
            case WidgetType::Label: {
                auto* w = static_cast<UILabel*>(child.get());
                cj["type"] = "label";
                cj["text"] = w->text;
                cj["isTitle"] = w->isTitle;
                break;
            }
            case WidgetType::Button: {
                auto* w = static_cast<UIButton*>(child.get());
                cj["type"] = "button";
                cj["text"] = w->text;
                break;
            }
            case WidgetType::Slider: {
                auto* w = static_cast<UISlider*>(child.get());
                cj["type"] = "slider";
                cj["label"] = w->label;
                cj["value"] = w->value;
                cj["min"] = w->minVal;
                cj["max"] = w->maxVal;
                break;
            }
            case WidgetType::Checkbox: {
                auto* w = static_cast<UICheckbox*>(child.get());
                cj["type"] = "checkbox";
                cj["label"] = w->label;
                cj["checked"] = w->checked;
                break;
            }
            case WidgetType::Dropdown: {
                auto* w = static_cast<UIDropdown*>(child.get());
                cj["type"] = "dropdown";
                cj["label"] = w->label;
                cj["options"] = w->options;
                cj["selected"] = w->selectedIndex;
                break;
            }
            case WidgetType::ProgressBar: {
                auto* w = static_cast<UIProgressBar*>(child.get());
                cj["type"] = "progressbar";
                cj["label"] = w->label;
                cj["value"] = w->value;
                cj["min"] = w->minVal;
                cj["max"] = w->maxVal;
                if (!w->bind.empty()) cj["bind"] = w->bind;
                break;
            }
            case WidgetType::Panel: {
                cj = toJson(*static_cast<UIPanel*>(child.get()));
                break;
            }
            default: break;
        }
        childrenArr.push_back(cj);
    }
    j["children"] = childrenArr;
    return j;
}

// ════════════════════════════════════════════════════════════════
// HUD data binding application
// ════════════════════════════════════════════════════════════════

// Apply a context's scalar value/visibility bindings to one widget.
static void applyScalarBind(UIWidget* w, const HudDataContext& ctx) {
    if (!w) return;
    if (!w->visibleWhen.empty()) {
        // Fail-closed: a gated element with no provider registered stays hidden
        // (so e.g. combat panels don't show empty in hosts that lack combat state).
        auto v = ctx.resolveFloat(w->visibleWhen);
        w->visible = (v && *v > 0.5f);
    }
    if (w->bind.empty()) return;
    switch (w->type()) {
        case WidgetType::ProgressBar:
            if (auto v = ctx.resolveFloat(w->bind)) static_cast<UIProgressBar*>(w)->value = *v;
            break;
        case WidgetType::Label:
            if (auto s = ctx.resolveText(w->bind)) static_cast<UILabel*>(w)->text = *s;
            else if (auto v = ctx.resolveFloat(w->bind)) {
                char buf[32]; snprintf(buf, sizeof(buf), "%g", *v);
                static_cast<UILabel*>(w)->text = buf;
            }
            break;
        case WidgetType::Button:
            if (auto s = ctx.resolveText(w->bind)) static_cast<UIButton*>(w)->text = *s;
            break;
        default: break;
    }
}

// Field name behind an "item.<field>" key (empty if not an item binding).
static std::string itemField(const std::string& key) {
    static const std::string prefix = "item.";
    return (key.rfind(prefix, 0) == 0) ? key.substr(prefix.size()) : std::string();
}

// Apply one list record to a generated item subtree (binds keyed "item.<field>").
static void applyRecord(UIWidget* w, const HudRecord& rec) {
    if (!w) return;
    if (!w->visibleWhen.empty()) {
        std::string f = itemField(w->visibleWhen);
        if (!f.empty()) {
            auto it = rec.floats.find(f);
            if (it != rec.floats.end()) w->visible = (it->second > 0.5f);
        }
    }
    if (!w->bind.empty()) {
        std::string f = itemField(w->bind);
        if (!f.empty()) {
            switch (w->type()) {
                case WidgetType::ProgressBar: {
                    auto it = rec.floats.find(f);
                    if (it != rec.floats.end()) static_cast<UIProgressBar*>(w)->value = it->second;
                    break;
                }
                case WidgetType::Label: {
                    auto it = rec.texts.find(f);
                    if (it != rec.texts.end()) static_cast<UILabel*>(w)->text = it->second;
                    else {
                        auto fit = rec.floats.find(f);
                        if (fit != rec.floats.end()) {
                            char buf[32]; snprintf(buf, sizeof(buf), "%g", fit->second);
                            static_cast<UILabel*>(w)->text = buf;
                        }
                    }
                    break;
                }
                case WidgetType::Image: {
                    auto* img = static_cast<UIImage*>(w);
                    auto it = rec.texts.find(f);
                    if (it != rec.texts.end() && img->imagePath != it->second) {
                        img->imagePath = it->second;
                        img->loadedTexture = -1;  // path changed — reload (cached by renderer)
                    }
                    // Selection highlight: full-bright icon when selected, dimmed otherwise.
                    auto sel = rec.floats.find("selected");
                    float b = (sel != rec.floats.end() && sel->second > 0.5f) ? 1.0f : 0.5f;
                    img->tintColor = {b, b, b, 1.0f};
                    break;
                }
                default: break;
            }
        }
    }
    if (w->type() == WidgetType::Panel) {
        for (auto& c : static_cast<UIPanel*>(w)->children) applyRecord(c.get(), rec);
    }
}

void applyHudBindings(UIWidget* root, const HudDataContext& ctx) {
    if (!root) return;
    applyScalarBind(root, ctx);

    if (root->type() == WidgetType::Repeater) {
        auto* rep = static_cast<UIRepeater*>(root);
        std::vector<HudRecord> recs;
        if (auto l = ctx.resolveList(rep->bind)) recs = std::move(*l);
        // Rebuild item widgets only when the count changes (cheap steady state).
        if (rep->generated.size() != recs.size()) {
            rep->generated.clear();
            if (!rep->itemTemplateJson.empty()) {
                try {
                    auto tj = nlohmann::json::parse(rep->itemTemplateJson);
                    for (size_t i = 0; i < recs.size(); ++i) {
                        if (auto item = MenuDefinition::buildWidget(tj)) rep->generated.push_back(std::move(item));
                    }
                } catch (const std::exception&) { /* malformed template — skip */ }
            }
        }
        for (size_t i = 0; i < rep->generated.size() && i < recs.size(); ++i) {
            applyRecord(rep->generated[i].get(), recs[i]);
        }
        return;  // repeater has no static children to recurse
    }

    if (root->type() == WidgetType::Panel) {
        for (auto& c : static_cast<UIPanel*>(root)->children) applyHudBindings(c.get(), ctx);
    }
}

// ════════════════════════════════════════════════════════════════
// Menu loader (GameMenuRenderer schema -> UISystem screens, no ImGui)
// ════════════════════════════════════════════════════════════════

static glm::vec4 parseColorArr(const nlohmann::json& j, glm::vec4 def) {
    if (j.is_array() && j.size() >= 4)
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
    return def;
}

// Show only one screen within a "<prefix>*" namespace (submenu navigation). Works
// for any overlay namespace ("menu:", "settings:", …) so sub-panels can navigate.
static void showOnlyInNamespace(UISystem& ui, const std::string& nsPrefix,
                                const std::string& screen) {
    for (const auto& [name, vis] : ui.getScreenList()) {
        if (name.rfind(nsPrefix, 0) != 0) continue;
        if (name == screen) ui.showScreen(name); else ui.hideScreen(name);
    }
}

void unloadMenuFrom(UISystem& ui) {
    for (const auto& [name, vis] : ui.getScreenList()) {
        if (name.rfind("menu:", 0) == 0) ui.removeScreen(name);
    }
    // Restore the game HUD screens that were hidden while the menu was up.
    for (const auto& [name, vis] : ui.getScreenList()) ui.showScreen(name);
}

// Replace {{token}} occurrences in text via actions.onResolveVariable (static, at
// load time). Unresolved tokens (or no resolver) are left literal.
static std::string resolveTokens(const std::string& text, const MenuActions& actions) {
    if (!actions.onResolveVariable || text.find("{{") == std::string::npos) return text;
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t open = text.find("{{", i);
        if (open == std::string::npos) { out += text.substr(i); break; }
        out += text.substr(i, open - i);
        size_t close = text.find("}}", open + 2);
        if (close == std::string::npos) { out += text.substr(open); break; }
        std::string token = text.substr(open + 2, close - (open + 2));
        size_t a = token.find_first_not_of(" \t");
        size_t b = token.find_last_not_of(" \t");
        token = (a == std::string::npos) ? std::string() : token.substr(a, b - a + 1);
        auto val = actions.onResolveVariable(token);
        out += val ? *val : text.substr(open, close + 2 - open);  // leave literal if unresolved
        i = close + 2;
    }
    return out;
}

static std::unique_ptr<UIWidget> buildMenuElement(const nlohmann::json& el, float sx, float sy,
        const MenuActions& actions, UISystem& ui, const std::string& startPanel,
        const std::string& nsPrefix) {
    std::string type = el.value("type", "");
    glm::vec2 posv{0, 0}, sizev{100, 40};
    if (el.contains("position") && el["position"].is_array() && el["position"].size() >= 2)
        posv = {el["position"][0].get<float>() * sx, el["position"][1].get<float>() * sy};
    if (el.contains("size") && el["size"].is_array() && el["size"].size() >= 2)
        sizev = {el["size"][0].get<float>() * sx, el["size"][1].get<float>() * sy};

    if (type == "label") {
        auto w = std::make_unique<UILabel>();
        w->text = resolveTokens(el.value("text", ""), actions);
        w->isTitle = (el.value("font", "") == "title");
        w->position = posv; w->size = sizev;
        // Same align/wrap semantics as MenuDefinition::buildWidget — this is the
        // OTHER label path (menu scenes + intro/victory/credits/loading overlays);
        // the screenshot probe caught it missing the align parse entirely.
        const std::string al = el.value("align", "left");
        if (al == "center")      w->align = UILabel::HAlign::Center;
        else if (al == "right")  w->align = UILabel::HAlign::Right;
        w->wrapWidth = el.value("wrapWidth", 0.0f) * sx;
        if (w->wrapWidth <= 0.0f && el.contains("size") && sizev.x > 0.0f)
            w->wrapWidth = sizev.x;   // authored width bounds the text
        return w;
    }
    if (type == "image") {
        auto w = std::make_unique<UIImage>();
        w->imagePath = el.value("image", el.value("imagePath", ""));
        w->position = posv; w->size = sizev;
        return w;
    }
    if (type == "panel") {
        // Nested panel inside a menu screen — the container for scrollable
        // content (lore pages, credits rolls, long option lists). Children
        // recurse through this same builder, so anything a screen can hold,
        // a panel can hold.
        auto w = std::make_unique<UIPanel>();
        w->id = el.value("id", "");
        w->position = posv; w->size = sizev;
        w->freeLayout = true;
        w->showBackground = el.value("showBackground", true);
        w->clipChildren = el.value("clip", true);
        w->scrollable = el.value("scrollable", false);
        if (el.contains("children") && el["children"].is_array())
            for (const auto& c : el["children"])
                if (auto cw = buildMenuElement(c, sx, sy, actions, ui, startPanel, nsPrefix))
                    w->addChild(std::move(cw));
        return w;
    }
    if (type == "button") {
        auto w = std::make_unique<UIButton>();
        w->text = resolveTokens(el.value("text", ""), actions);
        w->position = posv; w->size = sizev;
        if (el.contains("action") && el["action"].is_object()) {
            const auto& a = el["action"];
            std::string at = a.value("type", "");
            std::string target = a.value("target", "");
            UISystem* uip = &ui;
            if (at == "transition_scene") { auto cb = actions.onTransitionScene; w->onClick = [cb, target] { if (cb) cb(target); }; }
            else if (at == "quit_game")    { auto cb = actions.onQuit;     w->onClick = [cb] { if (cb) cb(); }; }
            else if (at == "load_game")    { auto cb = actions.onLoadGame; w->onClick = [cb] { if (cb) cb(); }; }
            else if (at == "resume")       { auto cb = actions.onResume;   w->onClick = [cb] { if (cb) cb(); }; }
            else if (at == "open_settings"){ auto cb = actions.onSettings; w->onClick = [cb] { if (cb) cb(); }; }
            else if (at == "main_menu")    { auto cb = actions.onMainMenu; w->onClick = [cb] { if (cb) cb(); }; }
            else if (at == "show_credits") { auto cb = actions.onShowCredits; w->onClick = [cb] { if (cb) cb(); }; }
            else if (at == "start_game")   { auto cb = actions.onStartGame;   w->onClick = [cb] { if (cb) cb(); }; }
            else if (at == "back")         { auto cb = actions.onBack;        w->onClick = [cb] { if (cb) cb(); }; }
            else if (at == "open_submenu") { w->onClick = [uip, target, nsPrefix]    { showOnlyInNamespace(*uip, nsPrefix, nsPrefix + target); }; }
            else if (at == "close_submenu"){ w->onClick = [uip, startPanel, nsPrefix]{ showOnlyInNamespace(*uip, nsPrefix, nsPrefix + startPanel); }; }
            else if (at == "rebind")       { auto cb = actions.onRebindKey; std::string b = a.value("binding", ""); w->onClick = [cb, b] { if (cb) cb(b); }; }
        }
        return w;
    }
    // Settings widgets — slider / checkbox / dropdown carrying a "setting" key are
    // bound bidirectionally to the host's GameSettings via onGetSetting (initial
    // value, at load) + onSetSetting (apply on change). Floats throughout (checkbox
    // 0/1, dropdown = selected index). See loadGameScreenInto / settings_screen.json.
    if (type == "slider") {
        auto w = std::make_unique<UISlider>();
        w->label = resolveTokens(el.value("label", ""), actions);
        w->position = posv; w->size = sizev;
        w->minVal = el.value("min", 0.0f);
        w->maxVal = el.value("max", 1.0f);
        const std::string key = el.value("setting", "");
        w->value = (!key.empty() && actions.onGetSetting) ? actions.onGetSetting(key)
                                                          : el.value("value", w->minVal);
        if (!key.empty()) { auto cb = actions.onSetSetting; w->onChange = [cb, key](float v) { if (cb) cb(key, v); }; }
        return w;
    }
    if (type == "checkbox") {
        auto w = std::make_unique<UICheckbox>();
        w->label = resolveTokens(el.value("label", ""), actions);
        w->position = posv; w->size = sizev;
        const std::string key = el.value("setting", "");
        w->checked = (!key.empty() && actions.onGetSetting) ? (actions.onGetSetting(key) > 0.5f)
                                                            : el.value("checked", false);
        if (!key.empty()) { auto cb = actions.onSetSetting; w->onChange = [cb, key](bool b) { if (cb) cb(key, b ? 1.0f : 0.0f); }; }
        return w;
    }
    if (type == "dropdown") {
        auto w = std::make_unique<UIDropdown>();
        w->label = resolveTokens(el.value("label", ""), actions);
        w->position = posv; w->size = sizev;
        if (el.contains("options") && el["options"].is_array())
            for (const auto& o : el["options"]) w->options.push_back(o.get<std::string>());
        const std::string key = el.value("setting", "");
        w->selectedIndex = (!key.empty() && actions.onGetSetting)
            ? static_cast<int>(actions.onGetSetting(key) + 0.5f) : el.value("selected", 0);
        if (!key.empty()) { auto cb = actions.onSetSetting; w->onChange = [cb, key](int i) { if (cb) cb(key, static_cast<float>(i)); }; }
        return w;
    }
    if (type == "textinput") {
        auto w = std::make_unique<UITextInput>();
        w->placeholder = resolveTokens(el.value("placeholder", ""), actions);
        w->maxLength = el.value("maxLength", 255);
        w->position = posv; w->size = sizev;
        // A "setting" key binds the field to a string setting (AI model / key, etc.).
        const std::string key = el.value("setting", "");
        if (!key.empty() && actions.onGetSettingText) w->text = actions.onGetSettingText(key);
        if (!key.empty()) {
            auto cb = actions.onSetSettingText;
            w->onChange = [cb, key](const std::string& v) { if (cb) cb(key, v); };
            w->onSubmit = [cb, key](const std::string& v) { if (cb) cb(key, v); };
        }
        return w;
    }
    return nullptr;
}

void loadHudInto(UISystem& ui, const nlohmann::json* gameHud) {
    nlohmann::json hudDef;
    if (gameHud && !gameHud->is_null()) {
        hudDef = *gameHud;
        LOG_INFO("HUD", "Using game-defined HUD");
    } else {
        const std::string path = "resources/ui/default_hud.json";
        std::ifstream df(path);
        if (df.is_open()) {
            try { df >> hudDef; LOG_INFO("HUD", "Loaded engine default HUD ({})", path); }
            catch (const std::exception& e) { LOG_ERROR("HUD", "Failed to parse default HUD: {}", e.what()); }
        } else {
            LOG_WARN("HUD", "Default HUD not found at {} — no HUD loaded", path);
        }
    }
    if (hudDef.is_null()) return;

    auto buildPanel = [&](const nlohmann::json& panelDef) {
        try {
            auto panel = MenuDefinition::buildFromJson(panelDef);
            if (!panel) { LOG_ERROR("HUD", "Failed to build a HUD panel"); return; }
            std::string id = panelDef.value("id", "hud");
            ui.addScreen(id, std::move(panel));
            ui.showScreen(id);
            LOG_INFO("HUD", "HUD panel '{}' loaded", id);
        } catch (const std::exception& e) {
            LOG_ERROR("HUD", "Error parsing HUD panel: {}", e.what());
        }
    };
    if (hudDef.is_array()) { for (const auto& p : hudDef) buildPanel(p); }
    else if (hudDef.is_object()) { buildPanel(hudDef); }
}

void loadMenuInto(UISystem& ui, const nlohmann::json& layout, const MenuActions& actions) {
    unloadMenuFrom(ui);
    if (!layout.is_object() || !layout.contains("panels") || !layout["panels"].is_object()) return;

    const float W = static_cast<float>(ui.width());
    const float H = static_cast<float>(ui.height());
    const float sx = W / 1280.0f, sy = H / 720.0f;  // virtual canvas -> window
    std::string bgType = layout.value("background_type", "solid");
    glm::vec4 bgColor = parseColorArr(layout.contains("background_color") ? layout["background_color"] : nlohmann::json(),
                                      {0.05f, 0.05f, 0.10f, 1.0f});
    std::string bgImage = layout.value("background_image", "");
    std::string startPanel = layout.value("start_panel", "main");

    const auto& panels = layout["panels"];
    std::vector<std::string> keys;
    for (auto it = panels.begin(); it != panels.end(); ++it) {
        keys.push_back(it.key());
        const auto& pdef = it.value();

        auto root = std::make_unique<UIPanel>();
        root->anchor = Anchor::TopLeft;
        root->offset = {0, 0};
        root->size = {W, H};
        root->showBackground = false;
        root->freeLayout = true;

        // Fullscreen background (solid color, or image when provided).
        auto bg = std::make_unique<UIImage>();
        bg->position = {0, 0};
        bg->size = {W, H};
        if (bgType == "image" && !bgImage.empty()) { bg->imagePath = bgImage; bg->tintColor = {1, 1, 1, 1}; }
        else { bg->tintColor = bgColor; }  // no path -> drawn as a solid rect
        root->addChild(std::move(bg));

        if (pdef.contains("children") && pdef["children"].is_array()) {
            for (const auto& el : pdef["children"]) {
                if (auto w = buildMenuElement(el, sx, sy, actions, ui, startPanel, "menu:")) root->addChild(std::move(w));
            }
        }
        ui.addScreen("menu:" + it.key(), std::move(root));
    }

    // Hide the game HUD while a menu is shown; show only the start panel.
    for (const auto& [name, vis] : ui.getScreenList())
        if (name.rfind("menu:", 0) != 0) ui.hideScreen(name);
    for (const auto& k : keys) ui.hideScreen("menu:" + k);
    ui.showScreen("menu:" + startPanel);
}

// Remove all screens in a "<prefix>" namespace (e.g. "pause:", "victory:").
static void removeScreensWithPrefix(UISystem& ui, const std::string& prefix) {
    for (const auto& [name, vis] : ui.getScreenList())
        if (name.rfind(prefix, 0) == 0) ui.removeScreen(name);
}

// Shared builder behind the pause overlay + the Intro/Victory/Credits game
// screens (docs/HudSystem.md §11a). Loads resources/ui/<file> into UISystem
// screens named "<nsPrefix><panelKey>" and shows the start panel. `defaultBg` is
// the scrim/background used when the JSON omits "background_color" (pause wants a
// translucent scrim over the frozen world; full screens want an opaque cover).
static void loadOverlayFromFile(UISystem& ui, const std::string& nsPrefix,
                                const std::string& file, const MenuActions& actions,
                                glm::vec4 defaultBg) {
    removeScreensWithPrefix(ui, nsPrefix);  // idempotent — never stack overlays

    const std::string path = "resources/ui/" + file;
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_WARN("UI", "Overlay not found at {} — '{}' screen not shown", path, nsPrefix);
        return;
    }
    nlohmann::json layout;
    try { f >> layout; }
    catch (const std::exception& e) {
        LOG_ERROR("UI", "Failed to parse {}: {}", path, e.what());
        return;
    }
    if (!layout.is_object() || !layout.contains("panels") || !layout["panels"].is_object()) return;

    const float W = static_cast<float>(ui.width());
    const float H = static_cast<float>(ui.height());
    const float sx = W / 1280.0f, sy = H / 720.0f;  // virtual canvas -> window
    glm::vec4 bgColor = parseColorArr(
        layout.contains("background_color") ? layout["background_color"] : nlohmann::json(),
        defaultBg);
    std::string startPanel = layout.value("start_panel", "main");

    const auto& panels = layout["panels"];
    for (auto it = panels.begin(); it != panels.end(); ++it) {
        const auto& pdef = it.value();
        auto root = std::make_unique<UIPanel>();
        root->anchor = Anchor::TopLeft;
        root->offset = {0, 0};
        root->size = {W, H};
        root->showBackground = false;
        root->freeLayout = true;

        auto bg = std::make_unique<UIImage>();
        bg->position = {0, 0};
        bg->size = {W, H};
        bg->tintColor = bgColor;  // no image path -> solid (alpha-blended) fill
        root->addChild(std::move(bg));

        if (pdef.contains("children") && pdef["children"].is_array()) {
            for (const auto& el : pdef["children"]) {
                if (auto w = buildMenuElement(el, sx, sy, actions, ui, startPanel, nsPrefix))
                    root->addChild(std::move(w));
            }
        }
        ui.addScreen(nsPrefix + it.key(), std::move(root));
    }

    // Show only the start panel (other panels stay hidden until navigated to).
    for (const auto& [name, vis] : ui.getScreenList())
        if (name.rfind(nsPrefix, 0) == 0) ui.hideScreen(name);
    ui.showScreen(nsPrefix + startPanel);
}

void unloadPauseMenuFrom(UISystem& ui) {
    removeScreensWithPrefix(ui, "pause:");
    // Restore the gameplay HUD screens hidden while paused (their per-frame
    // visibleWhen re-gates them). Mirrors unloadMenuFrom.
    for (const auto& [name, vis] : ui.getScreenList()) ui.showScreen(name);
}

void loadPauseMenuInto(UISystem& ui, const MenuActions& actions) {
    // Translucent scrim over the frozen world.
    loadOverlayFromFile(ui, "pause:", "pause_menu.json", actions, {0.03f, 0.03f, 0.06f, 0.88f});
    // Suppress the gameplay HUD while paused — the scrim is translucent, so HUD
    // panels (health/hotbar/objectives/countdown/…) would otherwise show through
    // (feedback #11). Hide everything that isn't part of the pause overlay;
    // unloadPauseMenuFrom restores them.
    for (const auto& [name, vis] : ui.getScreenList())
        if (name.rfind("pause:", 0) != 0) ui.hideScreen(name);
}

void unloadGameScreenFrom(UISystem& ui, const std::string& name) {
    removeScreensWithPrefix(ui, name + ":");
}

void loadGameScreenInto(UISystem& ui, const std::string& name, const MenuActions& actions) {
    // Full-screen opaque cover (intro / victory / credits replace the view).
    loadOverlayFromFile(ui, name + ":", name + "_screen.json", actions, {0.05f, 0.05f, 0.10f, 1.0f});
}

void setupAIDialogue(UISystem& ui, HudDataContext& hud, DialogueSystem* dialogue) {
    if (!dialogue) return;

    // Visibility: only during an AI (free-text) conversation, not standard trees.
    hud.setFloat("dialogue.aiActive", [dialogue]() {
        return (dialogue->isActive() && dialogue->isAIConversation()) ? 1.0f : 0.0f;
    });
    hud.setText("dialogue.aiSpeaker", [dialogue]() -> std::string {
        const std::string& sp = dialogue->getCurrentSpeaker();
        return sp.empty() ? std::string("Conversation") : ("Talking to " + sp + "   [AI]");
    });
    // History scrollback: the most recent messages + the live typing / "thinking"
    // state, joined with newlines (the ai_history label word-wraps).
    hud.setText("dialogue.aiHistory", [dialogue]() -> std::string {
        std::string s;
        const auto& hist = dialogue->getConversationHistory();
        const size_t maxMsgs = 8;
        const size_t start = hist.size() > maxMsgs ? hist.size() - maxMsgs : 0;
        for (size_t i = start; i < hist.size(); ++i) {
            const auto& m = hist[i];
            s += (m.speaker == "Player" ? std::string("You") : m.speaker) + ": " + m.text + "\n";
        }
        switch (dialogue->getState()) {
            case DialogueState::Typing:
                s += dialogue->getCurrentSpeaker() + ": " + dialogue->getRevealedText();
                break;
            case DialogueState::AIWaitingForResponse:
                s += dialogue->getCurrentSpeaker() + " is thinking...";
                break;
            default: break;
        }
        return s;
    });

    // Wire the input field's submit: copy into the DialogueSystem's input buffer,
    // submit, and clear. (The buffer + submitPlayerMessage are unchanged from the
    // ImGui path; only the editing widget moved to the UISystem.)
    if (auto* panel = ui.getScreen("hud_ai_dialogue")) {
        if (auto* w = panel->findChild("ai_input")) {
            if (w->type() == WidgetType::TextInput) {
                auto* ti = static_cast<UITextInput*>(w);
                ti->onSubmit = [dialogue, ti](const std::string& txt) {
                    if (txt.empty()) return;
                    char* buf = dialogue->getInputBuffer();
                    std::strncpy(buf, txt.c_str(), DialogueSystem::INPUT_BUFFER_SIZE - 1);
                    buf[DialogueSystem::INPUT_BUFFER_SIZE - 1] = '\0';
                    dialogue->submitPlayerMessage();
                    ti->text.clear();
                };
            }
        }
    }
}

} // namespace UI
} // namespace Phyxel
