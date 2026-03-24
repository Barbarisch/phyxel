#include <gtest/gtest.h>
#include "ui/UIWidget.h"
#include "ui/MenuDefinition.h"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

using namespace Phyxel::UI;

// ════════════════════════════════════════════════════════════════
// Anchor resolution tests
// ════════════════════════════════════════════════════════════════

TEST(UIAnchorTest, TopLeft) {
    glm::vec2 pos = resolveAnchor(Anchor::TopLeft, {0, 0}, {800, 600}, {200, 100});
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
}

TEST(UIAnchorTest, Center) {
    glm::vec2 pos = resolveAnchor(Anchor::Center, {0, 0}, {800, 600}, {200, 100});
    EXPECT_FLOAT_EQ(pos.x, 300.0f);
    EXPECT_FLOAT_EQ(pos.y, 250.0f);
}

TEST(UIAnchorTest, BottomRight) {
    glm::vec2 pos = resolveAnchor(Anchor::BottomRight, {0, 0}, {800, 600}, {200, 100});
    EXPECT_FLOAT_EQ(pos.x, 600.0f);
    EXPECT_FLOAT_EQ(pos.y, 500.0f);
}

TEST(UIAnchorTest, CenterWithOffset) {
    glm::vec2 pos = resolveAnchor(Anchor::Center, {0, 0}, {800, 600}, {200, 100}, {10, -20});
    EXPECT_FLOAT_EQ(pos.x, 310.0f);
    EXPECT_FLOAT_EQ(pos.y, 230.0f);
}

TEST(UIAnchorTest, TopCenter) {
    glm::vec2 pos = resolveAnchor(Anchor::TopCenter, {0, 0}, {800, 600}, {200, 100});
    EXPECT_FLOAT_EQ(pos.x, 300.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
}

TEST(UIAnchorTest, BottomCenter) {
    glm::vec2 pos = resolveAnchor(Anchor::BottomCenter, {0, 0}, {800, 600}, {200, 100});
    EXPECT_FLOAT_EQ(pos.x, 300.0f);
    EXPECT_FLOAT_EQ(pos.y, 500.0f);
}

TEST(UIAnchorTest, CenterLeft) {
    glm::vec2 pos = resolveAnchor(Anchor::CenterLeft, {0, 0}, {800, 600}, {200, 100});
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 250.0f);
}

TEST(UIAnchorTest, CenterRight) {
    glm::vec2 pos = resolveAnchor(Anchor::CenterRight, {0, 0}, {800, 600}, {200, 100});
    EXPECT_FLOAT_EQ(pos.x, 600.0f);
    EXPECT_FLOAT_EQ(pos.y, 250.0f);
}

TEST(UIAnchorTest, BottomLeft) {
    glm::vec2 pos = resolveAnchor(Anchor::BottomLeft, {0, 0}, {800, 600}, {200, 100});
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 500.0f);
}

TEST(UIAnchorTest, TopRight) {
    glm::vec2 pos = resolveAnchor(Anchor::TopRight, {0, 0}, {800, 600}, {200, 100});
    EXPECT_FLOAT_EQ(pos.x, 600.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
}

TEST(UIAnchorTest, NonZeroParentPos) {
    glm::vec2 pos = resolveAnchor(Anchor::Center, {100, 50}, {400, 300}, {100, 100});
    EXPECT_FLOAT_EQ(pos.x, 250.0f);
    EXPECT_FLOAT_EQ(pos.y, 150.0f);
}

// ════════════════════════════════════════════════════════════════
// Widget type tests
// ════════════════════════════════════════════════════════════════

TEST(UIWidgetTest, LabelType) {
    UILabel label;
    label.text = "Hello";
    EXPECT_EQ(label.type(), WidgetType::Label);
    EXPECT_EQ(label.text, "Hello");
    EXPECT_FALSE(label.isTitle);
}

TEST(UIWidgetTest, ButtonType) {
    UIButton btn;
    btn.text = "Click Me";
    EXPECT_EQ(btn.type(), WidgetType::Button);
    EXPECT_EQ(btn.text, "Click Me");
}

TEST(UIWidgetTest, ButtonClickCallback) {
    UIButton btn;
    btn.size = {200, 40};
    bool clicked = false;
    btn.onClick = [&]() { clicked = true; };

    UITheme theme;
    // Click inside button bounds
    EXPECT_TRUE(btn.handleClick({100, 20}, {0, 0}, theme));
    EXPECT_TRUE(clicked);
}

TEST(UIWidgetTest, ButtonClickMiss) {
    UIButton btn;
    btn.size = {200, 40};
    bool clicked = false;
    btn.onClick = [&]() { clicked = true; };

    UITheme theme;
    // Click outside
    EXPECT_FALSE(btn.handleClick({300, 20}, {0, 0}, theme));
    EXPECT_FALSE(clicked);
}

TEST(UIWidgetTest, CheckboxToggle) {
    UICheckbox cb;
    cb.size = {200, 32};
    cb.checked = false;

    UITheme theme;
    EXPECT_TRUE(cb.handleClick({10, 16}, {0, 0}, theme));
    EXPECT_TRUE(cb.checked);
    EXPECT_TRUE(cb.handleClick({10, 16}, {0, 0}, theme));
    EXPECT_FALSE(cb.checked);
}

TEST(UIWidgetTest, CheckboxCallback) {
    UICheckbox cb;
    cb.size = {200, 32};
    cb.checked = false;
    bool lastValue = false;
    cb.onChange = [&](bool v) { lastValue = v; };

    UITheme theme;
    cb.handleClick({10, 16}, {0, 0}, theme);
    EXPECT_TRUE(lastValue);
    cb.handleClick({10, 16}, {0, 0}, theme);
    EXPECT_FALSE(lastValue);
}

TEST(UIWidgetTest, SliderType) {
    UISlider slider;
    slider.label = "Vol";
    slider.value = 0.5f;
    slider.minVal = 0.0f;
    slider.maxVal = 1.0f;
    EXPECT_EQ(slider.type(), WidgetType::Slider);
}

TEST(UIWidgetTest, DropdownType) {
    UIDropdown dd;
    dd.options = {"Low", "Medium", "High"};
    dd.selectedIndex = 1;
    EXPECT_EQ(dd.type(), WidgetType::Dropdown);
    EXPECT_EQ(dd.options.size(), 3u);
}

TEST(UIWidgetTest, DropdownToggleOpen) {
    UIDropdown dd;
    dd.size = {200, 40};
    dd.options = {"A", "B", "C"};
    dd.selectedIndex = 0;
    dd.open = false;

    UITheme theme;
    // Click on dropdown box area (no label, so box starts at 0)
    EXPECT_TRUE(dd.handleClick({100, 20}, {0, 0}, theme));
    EXPECT_TRUE(dd.open);
    // Click outside items area -> close
    EXPECT_TRUE(dd.handleClick({300, 300}, {0, 0}, theme));
    EXPECT_FALSE(dd.open);
}

// ════════════════════════════════════════════════════════════════
// Panel tests
// ════════════════════════════════════════════════════════════════

TEST(UIPanelTest, AddAndFindChild) {
    UIPanel panel;
    auto btn = std::make_unique<UIButton>();
    btn->id = "my_btn";
    btn->text = "test";
    panel.addChild(std::move(btn));

    EXPECT_EQ(panel.children.size(), 1u);
    auto* found = panel.findChild("my_btn");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type(), WidgetType::Button);
}

TEST(UIPanelTest, FindChildRecursive) {
    UIPanel panel;
    auto sub = std::make_unique<UIPanel>();
    sub->id = "sub_panel";
    auto lbl = std::make_unique<UILabel>();
    lbl->id = "nested_label";
    lbl->text = "deep";
    sub->addChild(std::move(lbl));
    panel.addChild(std::move(sub));

    auto* found = panel.findChild("nested_label");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type(), WidgetType::Label);
}

TEST(UIPanelTest, FindChildNotFound) {
    UIPanel panel;
    EXPECT_EQ(panel.findChild("nonexistent"), nullptr);
}

TEST(UIPanelTest, DefaultProperties) {
    UIPanel panel;
    EXPECT_EQ(panel.type(), WidgetType::Panel);
    EXPECT_TRUE(panel.showBackground);
    EXPECT_EQ(panel.anchor, Anchor::Center);
    EXPECT_TRUE(panel.visible);
    EXPECT_TRUE(panel.enabled);
}

// ════════════════════════════════════════════════════════════════
// Theme tests
// ════════════════════════════════════════════════════════════════

TEST(UIThemeTest, Defaults) {
    UITheme theme;
    EXPECT_FLOAT_EQ(theme.textScale, 2.0f);
    EXPECT_FLOAT_EQ(theme.titleScale, 3.0f);
    EXPECT_FLOAT_EQ(theme.padding, 8.0f);
    EXPECT_FLOAT_EQ(theme.itemSpacing, 6.0f);
    EXPECT_FLOAT_EQ(theme.buttonHeight, 40.0f);
    EXPECT_FLOAT_EQ(theme.sliderHeight, 24.0f);
    EXPECT_FLOAT_EQ(theme.borderWidth, 2.0f);
}

// ════════════════════════════════════════════════════════════════
// MenuDefinition JSON tests
// ════════════════════════════════════════════════════════════════

TEST(MenuDefinitionTest, BuildSimplePanel) {
    nlohmann::json j = {
        {"id", "test_panel"},
        {"title", "Test"},
        {"size", {400, 300}},
        {"anchor", "Center"},
        {"children", nlohmann::json::array({
            {{"type", "label"}, {"id", "lbl"}, {"text", "Hello"}, {"isTitle", true}},
            {{"type", "button"}, {"id", "btn"}, {"text", "OK"}, {"size", {380, 40}}}
        })}
    };

    auto panel = MenuDefinition::buildFromJson(j);
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->id, "test_panel");
    EXPECT_EQ(panel->title, "Test");
    EXPECT_FLOAT_EQ(panel->size.x, 400.0f);
    EXPECT_FLOAT_EQ(panel->size.y, 300.0f);
    EXPECT_EQ(panel->anchor, Anchor::Center);
    EXPECT_EQ(panel->children.size(), 2u);

    auto* lbl = panel->findChild("lbl");
    ASSERT_NE(lbl, nullptr);
    EXPECT_EQ(static_cast<UILabel*>(lbl)->text, "Hello");
    EXPECT_TRUE(static_cast<UILabel*>(lbl)->isTitle);

    auto* btn = panel->findChild("btn");
    ASSERT_NE(btn, nullptr);
    EXPECT_EQ(static_cast<UIButton*>(btn)->text, "OK");
}

TEST(MenuDefinitionTest, BuildAllWidgetTypes) {
    nlohmann::json j = {
        {"id", "all_types"},
        {"title", "All Types"},
        {"size", {400, 500}},
        {"children", nlohmann::json::array({
            {{"type", "label"}, {"id", "l1"}, {"text", "Label"}},
            {{"type", "button"}, {"id", "b1"}, {"text", "Button"}, {"size", {380, 40}}},
            {{"type", "slider"}, {"id", "s1"}, {"label", "Vol"}, {"value", 0.7}, {"min", 0.0}, {"max", 1.0}, {"size", {380, 32}}},
            {{"type", "checkbox"}, {"id", "c1"}, {"label", "Full"}, {"checked", true}, {"size", {380, 32}}},
            {{"type", "dropdown"}, {"id", "d1"}, {"label", "Q"}, {"options", {"Low", "Med", "High"}}, {"selected", 2}, {"size", {380, 40}}}
        })}
    };

    auto panel = MenuDefinition::buildFromJson(j);
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->children.size(), 5u);

    auto* slider = panel->findChild("s1");
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->type(), WidgetType::Slider);
    EXPECT_FLOAT_EQ(static_cast<UISlider*>(slider)->value, 0.7f);

    auto* checkbox = panel->findChild("c1");
    ASSERT_NE(checkbox, nullptr);
    EXPECT_TRUE(static_cast<UICheckbox*>(checkbox)->checked);

    auto* dropdown = panel->findChild("d1");
    ASSERT_NE(dropdown, nullptr);
    EXPECT_EQ(static_cast<UIDropdown*>(dropdown)->selectedIndex, 2);
    EXPECT_EQ(static_cast<UIDropdown*>(dropdown)->options.size(), 3u);
}

TEST(MenuDefinitionTest, BuildWithCallbacks) {
    nlohmann::json j = {
        {"id", "cb_test"},
        {"size", {400, 300}},
        {"children", nlohmann::json::array({
            {{"type", "button"}, {"id", "ok_btn"}, {"text", "OK"}, {"size", {380, 40}}},
            {{"type", "slider"}, {"id", "vol"}, {"value", 0.5}, {"min", 0}, {"max", 1}, {"size", {380, 32}}},
            {{"type", "checkbox"}, {"id", "fs"}, {"checked", false}, {"size", {380, 32}}}
        })}
    };

    bool okClicked = false;
    float lastSliderVal = -1;
    bool lastCheckVal = false;

    auto panel = MenuDefinition::buildFromJson(j,
        {{"ok_btn", [&]() { okClicked = true; }}},
        {{"vol", [&](float v) { lastSliderVal = v; }}},
        {{"fs", [&](bool v) { lastCheckVal = v; }}}
    );

    ASSERT_NE(panel, nullptr);
    auto* btn = panel->findChild("ok_btn");
    ASSERT_NE(btn, nullptr);
    static_cast<UIButton*>(btn)->onClick();
    EXPECT_TRUE(okClicked);

    auto* slider = panel->findChild("vol");
    ASSERT_NE(slider, nullptr);
    static_cast<UISlider*>(slider)->onChange(0.75f);
    EXPECT_FLOAT_EQ(lastSliderVal, 0.75f);

    auto* cb = panel->findChild("fs");
    ASSERT_NE(cb, nullptr);
    static_cast<UICheckbox*>(cb)->onChange(true);
    EXPECT_TRUE(lastCheckVal);
}

TEST(MenuDefinitionTest, BuildFromString) {
    std::string json = R"({
        "id": "str_test",
        "title": "From String",
        "size": [300, 200],
        "children": [
            {"type": "label", "id": "l", "text": "hi"}
        ]
    })";

    auto panel = MenuDefinition::buildFromJson(json);
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->id, "str_test");
    EXPECT_EQ(panel->children.size(), 1u);
}

TEST(MenuDefinitionTest, RoundTripSerialization) {
    nlohmann::json j = {
        {"id", "roundtrip"},
        {"title", "RT Test"},
        {"size", {400, 300}},
        {"anchor", "TopLeft"},
        {"offset", {10, 20}},
        {"showBackground", true},
        {"children", nlohmann::json::array({
            {{"type", "label"}, {"id", "l"}, {"text", "Label"}, {"isTitle", false}},
            {{"type", "button"}, {"id", "b"}, {"text", "Btn"}, {"size", {380, 40}}},
            {{"type", "slider"}, {"id", "s"}, {"label", "S"}, {"value", 0.3}, {"min", 0}, {"max", 1}, {"size", {380, 32}}},
            {{"type", "checkbox"}, {"id", "c"}, {"label", "C"}, {"checked", true}, {"size", {380, 32}}},
            {{"type", "dropdown"}, {"id", "d"}, {"label", "D"}, {"options", {"A", "B"}}, {"selected", 1}, {"size", {380, 40}}}
        })}
    };

    auto panel = MenuDefinition::buildFromJson(j);
    ASSERT_NE(panel, nullptr);

    auto serialized = MenuDefinition::toJson(*panel);
    EXPECT_EQ(serialized["id"], "roundtrip");
    EXPECT_EQ(serialized["title"], "RT Test");
    EXPECT_EQ(serialized["anchor"], "TopLeft");
    EXPECT_EQ(serialized["children"].size(), 5u);

    // Verify each child survived
    auto& children = serialized["children"];
    EXPECT_EQ(children[0]["type"], "label");
    EXPECT_EQ(children[0]["text"], "Label");
    EXPECT_EQ(children[1]["type"], "button");
    EXPECT_EQ(children[1]["text"], "Btn");
    EXPECT_EQ(children[2]["type"], "slider");
    EXPECT_FLOAT_EQ(children[2]["value"].get<float>(), 0.3f);
    EXPECT_EQ(children[3]["type"], "checkbox");
    EXPECT_TRUE(children[3]["checked"].get<bool>());
    EXPECT_EQ(children[4]["type"], "dropdown");
    EXPECT_EQ(children[4]["selected"], 1);
}

TEST(MenuDefinitionTest, AnchorParsing) {
    std::vector<std::pair<std::string, Anchor>> cases = {
        {"TopLeft", Anchor::TopLeft}, {"TopCenter", Anchor::TopCenter},
        {"TopRight", Anchor::TopRight}, {"CenterLeft", Anchor::CenterLeft},
        {"Center", Anchor::Center}, {"CenterRight", Anchor::CenterRight},
        {"BottomLeft", Anchor::BottomLeft}, {"BottomCenter", Anchor::BottomCenter},
        {"BottomRight", Anchor::BottomRight}
    };

    for (auto& [name, expected] : cases) {
        nlohmann::json j = {{"id", "a"}, {"size", {100, 100}}, {"anchor", name}};
        auto panel = MenuDefinition::buildFromJson(j);
        ASSERT_NE(panel, nullptr) << "Failed for anchor: " << name;
        EXPECT_EQ(panel->anchor, expected) << "Mismatch for anchor: " << name;
    }
}

TEST(MenuDefinitionTest, NestedPanels) {
    nlohmann::json j = {
        {"id", "outer"},
        {"size", {400, 400}},
        {"children", nlohmann::json::array({
            {{"type", "panel"}, {"id", "inner"}, {"title", "Sub"}, {"size", {380, 200}},
             {"children", nlohmann::json::array({
                 {{"type", "label"}, {"id", "deep_lbl"}, {"text", "Deep"}}
             })}}
        })}
    };

    auto panel = MenuDefinition::buildFromJson(j);
    ASSERT_NE(panel, nullptr);
    auto* inner = panel->findChild("inner");
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->type(), WidgetType::Panel);

    auto* deep = panel->findChild("deep_lbl");
    ASSERT_NE(deep, nullptr);
    EXPECT_EQ(static_cast<UILabel*>(deep)->text, "Deep");
}

TEST(MenuDefinitionTest, WidgetVisibilityAndEnabled) {
    nlohmann::json j = {
        {"id", "vis_test"},
        {"size", {400, 300}},
        {"children", nlohmann::json::array({
            {{"type", "button"}, {"id", "hidden_btn"}, {"text", "Hidden"}, {"visible", false}, {"size", {380, 40}}},
            {{"type", "button"}, {"id", "disabled_btn"}, {"text", "Disabled"}, {"enabled", false}, {"size", {380, 40}}}
        })}
    };

    auto panel = MenuDefinition::buildFromJson(j);
    auto* hidden = panel->findChild("hidden_btn");
    ASSERT_NE(hidden, nullptr);
    EXPECT_FALSE(hidden->visible);

    auto* disabled = panel->findChild("disabled_btn");
    ASSERT_NE(disabled, nullptr);
    EXPECT_FALSE(disabled->enabled);
}

TEST(MenuDefinitionTest, DisabledButtonIgnoresClick) {
    UIButton btn;
    btn.size = {200, 40};
    btn.enabled = false;
    bool clicked = false;
    btn.onClick = [&]() { clicked = true; };

    UITheme theme;
    EXPECT_FALSE(btn.handleClick({100, 20}, {0, 0}, theme));
    EXPECT_FALSE(clicked);
}

TEST(MenuDefinitionTest, InvisibleButtonIgnoresClick) {
    UIButton btn;
    btn.size = {200, 40};
    btn.visible = false;
    bool clicked = false;
    btn.onClick = [&]() { clicked = true; };

    UITheme theme;
    EXPECT_FALSE(btn.handleClick({100, 20}, {0, 0}, theme));
    EXPECT_FALSE(clicked);
}
