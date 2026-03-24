#include "ui/MenuDefinition.h"
#include <stdexcept>

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
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        return w;
    }

    if (type == "button") {
        auto w = std::make_unique<UIButton>();
        w->id = j.value("id", "");
        w->text = j.value("text", "");
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
        }
        return w;
    }

    if (type == "slider") {
        auto w = std::make_unique<UISlider>();
        w->id = j.value("id", "");
        w->label = j.value("label", "");
        w->value = j.value("value", 0.5f);
        w->minVal = j.value("min", 0.0f);
        w->maxVal = j.value("max", 1.0f);
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
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
        return w;
    }

    if (type == "panel") {
        auto w = std::make_unique<UIPanel>();
        w->id = j.value("id", "");
        w->title = j.value("title", "");
        w->showBackground = j.value("showBackground", true);
        w->visible = j.value("visible", true);
        w->enabled = j.value("enabled", true);
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
            w->size = {j["size"][0].get<float>(), j["size"][1].get<float>()};
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
            case WidgetType::Panel: {
                cj = toJson(*static_cast<UIPanel*>(child.get()));
                break;
            }
        }
        childrenArr.push_back(cj);
    }
    j["children"] = childrenArr;
    return j;
}

} // namespace UI
} // namespace Phyxel
