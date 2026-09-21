#include <gtest/gtest.h>
#include <iostream>
#include "ui/UIWidget.h"
#include "ui/MenuDefinition.h"
#include "ui/UISystem.h"
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


// ============================================================================
// LAYOUT PASS + LINT (docs/game-production/CombatUiBg3.md increment 2). Ravenmere
// manual test 2026-09-11: the Objectives text ran past its 220 px panel and was
// clipped, the Initiative list lost its last rows, the spell bar overlapped the
// Initiative panel. Contract: a label's measured height is its wrapped line count; an
// auto-sized panel grows to its content and caps into a scrollable box; the lint
// reports what a fixed panel would cut off and which visible panels overlap.
// Headless: the default BitmapFont measures 8 px glyphs / 16 px lines.
// ============================================================================
#include "ui/UILayoutLint.h"
#include "ui/BitmapFont.h"

namespace {
Phyxel::UI::UITheme testTheme() { Phyxel::UI::UITheme th; th.padding = 8.0f; th.itemSpacing = 4.0f; th.textScale = 2.0f; th.titleScale = 3.0f; return th; }
}

TEST(UILayoutTest, LabelHeightIsItsWrappedLineCount) {
    Phyxel::UI::BitmapFont font;   // headless: 8x16 glyphs
    const auto th = testTheme();
    Phyxel::UI::UILabel l;
    l.text = "Speak with Reeve Aldric about the missing children";   // 50 chars
    // at scale 2: 16 px per glyph -> 800 px on one line; 320 px wide -> 20 chars per line -> 3 lines
    EXPECT_FLOAT_EQ(l.measureHeight(&font, th, 320.0f), 3.0f * font.lineHeight(2.0f));
    EXPECT_FLOAT_EQ(l.measureHeight(&font, th, 10000.0f), font.lineHeight(2.0f));
}

TEST(UILayoutTest, AutoSizedPanelGrowsToItsContentAndCapsIntoAScrollBox) {
    Phyxel::UI::BitmapFont font;
    const auto th = testTheme();
    auto panel = std::make_unique<Phyxel::UI::UIPanel>();
    panel->id = "hud_objectives"; panel->size = {340.0f, 220.0f}; panel->autoHeight = true;
    for (int i = 0; i < 4; ++i) {
        auto l = std::make_unique<Phyxel::UI::UILabel>();
        l->id = "obj_" + std::to_string(i);
        l->text = "[ ] Speak with Reeve Aldric about the missing children";   // wraps to 3 lines at 324 px inner width
        l->size = {320.0f, 28.0f};
        panel->addChild(std::move(l));
    }
    panel->applyAutoSize(&font, th);
    const float line = font.lineHeight(2.0f);
    // 4 labels x 3 lines + spacing + padding: the panel is as tall as its text, not 220
    const float expected = th.padding * 2.0f + 4.0f * 3.0f * line + 3.0f * th.itemSpacing;
    EXPECT_NEAR(panel->size.y, expected, 0.5f);
    EXPECT_FALSE(panel->scrollable);
    // with a cap the panel stops there and turns scrollable instead of clipping
    panel->maxSize = {340.0f, 200.0f};
    panel->applyAutoSize(&font, th);
    EXPECT_FLOAT_EQ(panel->size.y, 200.0f);
    EXPECT_TRUE(panel->scrollable);
}

TEST(UILayoutTest, LintReportsCutOffChildrenAndOverlappingPanels) {
    Phyxel::UI::BitmapFont font;
    const auto th = testTheme();
    // the shipped Objectives panel as it was: fixed 340x220 with more text than fits
    auto objectives = std::make_unique<Phyxel::UI::UIPanel>();
    objectives->id = "hud_objectives"; objectives->size = {340.0f, 220.0f};
    objectives->anchor = Phyxel::UI::Anchor::TopLeft; objectives->offset = {16.0f, 16.0f};
    for (int i = 0; i < 4; ++i) {
        auto l = std::make_unique<Phyxel::UI::UILabel>();
        l->id = "obj_" + std::to_string(i); l->text = "[ ] Speak with Reeve Aldric about the missing children"; l->size = {320.0f, 28.0f};
        objectives->addChild(std::move(l));
    }
    // a second panel placed right on top of it
    auto banner = std::make_unique<Phyxel::UI::UIPanel>();
    banner->id = "hud_combat_banner"; banner->size = {380.0f, 48.0f};
    banner->anchor = Phyxel::UI::Anchor::TopLeft; banner->offset = {200.0f, 30.0f};
    const auto defects = Phyxel::UI::lintLayout({objectives.get(), banner.get()}, {1280.0f, 720.0f}, &font, th);
    int overflow = 0, overlap = 0;
    for (const auto& d : defects) { if (d.kind == "child_overflow") ++overflow; if (d.kind == "panel_overlap") ++overlap; }
    EXPECT_GE(overflow, 1) << "the 4th objective is cut off by the 220 px panel";
    EXPECT_EQ(overlap, 1) << "the banner sits on the objectives panel";
    // auto-sizing the objectives panel removes the cut-off (and the lint knows it)
    objectives->autoHeight = true;
    const auto after = Phyxel::UI::lintLayout({objectives.get()}, {1280.0f, 720.0f}, &font, th);
    for (const auto& d : after) EXPECT_NE(d.kind, "child_overflow") << d.message;
}


TEST(UILayoutTest, AFullscreenOverlayIsNotAnOverlapPartner) {
    Phyxel::UI::BitmapFont font;
    const auto th = testTheme();
    auto overlay = std::make_unique<Phyxel::UI::UIPanel>();
    overlay->id = "screen:fade"; overlay->size = {1280.0f, 720.0f}; overlay->anchor = Phyxel::UI::Anchor::TopLeft;
    auto health = std::make_unique<Phyxel::UI::UIPanel>();
    health->id = "hud_health"; health->size = {340.0f, 60.0f}; health->anchor = Phyxel::UI::Anchor::BottomLeft; health->offset = {24.0f, -24.0f};
    const auto defects = Phyxel::UI::lintLayout({overlay.get(), health.get()}, {1280.0f, 720.0f}, &font, th);
    for (const auto& d : defects) EXPECT_NE(d.kind, "panel_overlap") << d.message;
    // ...but two ordinary panels still are
    auto bar = std::make_unique<Phyxel::UI::UIPanel>();
    bar->id = "hud_hotbar"; bar->size = {624.0f, 88.0f}; bar->anchor = Phyxel::UI::Anchor::BottomCenter; bar->offset = {0.0f, -12.0f};
    const auto d2 = Phyxel::UI::lintLayout({health.get(), bar.get()}, {1280.0f, 720.0f}, &font, th);
    int overlap = 0;
    for (const auto& d : d2) if (d.kind == "panel_overlap") ++overlap;
    EXPECT_EQ(overlap, 1);
}


// ============================================================================
// ACTION BAR (CombatUiBg3 increment 4): a repeater of buttons built from JSON takes its
// labels, enabled/armed state and its CLICK from the bound records - the shell never
// touches widgets, it publishes rows and one action handler.
// ============================================================================
#include "ui/HudDataContext.h"
TEST(UIActionBarTest, RepeaterButtonsCarryTheirRowsActions) {
    const auto rep = Phyxel::UI::MenuDefinition::buildWidget(nlohmann::json::parse(R"({
        "type":"repeater","id":"action_list","bind":"actionbar","horizontal":true,"size":[900,44],
        "item":{"type":"button","id":"slot","bind":"item.label","actionBind":"actionbar.use","size":[150,44]}})"));
    ASSERT_NE(rep, nullptr);
    Phyxel::UI::HudDataContext ctx;
    ctx.setList("actionbar", [] {
        std::vector<Phyxel::UI::HudRecord> rows;
        Phyxel::UI::HudRecord a; a.texts["label"] = "Attack"; a.texts["action"] = "attack"; a.floats["enabled"] = 1.0f; rows.push_back(a);
        Phyxel::UI::HudRecord b; b.texts["label"] = "Fire Bolt"; b.texts["action"] = "spell:fire_bolt"; b.floats["enabled"] = 0.0f; b.floats["armed"] = 1.0f; rows.push_back(b);
        return rows;
    });
    std::vector<std::string> fired;
    ctx.setAction("actionbar.use", [&](const Phyxel::UI::HudRecord& r) {
        fired.push_back(r.texts.count("action") ? r.texts.at("action") : std::string("<no action key>"));
    });
    Phyxel::UI::applyHudBindings(rep.get(), ctx);
    auto* r = static_cast<Phyxel::UI::UIRepeater*>(rep.get());
    ASSERT_EQ(r->generated.size(), 2u);
    auto* b0 = dynamic_cast<Phyxel::UI::UIButton*>(r->generated[0].get());
    auto* b1 = dynamic_cast<Phyxel::UI::UIButton*>(r->generated[1].get());
    ASSERT_NE(b0, nullptr); ASSERT_NE(b1, nullptr);
    EXPECT_EQ(b0->text, "Attack");  EXPECT_TRUE(b0->enabled);  EXPECT_FLOAT_EQ(b0->customBg.a, 0.0f);
    EXPECT_EQ(b1->text, "Fire Bolt"); EXPECT_FALSE(b1->enabled); EXPECT_GT(b1->customBg.a, 0.5f) << "armed rows glow";
    ASSERT_TRUE(b0->onClick); b0->onClick();
    ASSERT_TRUE(b1->onClick); b1->onClick();
    ASSERT_EQ(fired.size(), 2u);
    EXPECT_EQ(fired[0], "attack");
    EXPECT_EQ(fired[1], "spell:fire_bolt");
}

// G-106: a click on a repeater's generated button must reach that button (BG3 action
// bar). RED before: UIRepeater had no handleClick override, so UIPanel::handleClick
// stopped at the repeater and the shipped bar swallowed every click (probe L4 2026-09-15:
// consumed=true, no "Action bar" log line).
TEST(UIActionBarTest, ClicksReachTheRepeatersGeneratedButtons) {
    const auto rep = Phyxel::UI::MenuDefinition::buildWidget(nlohmann::json::parse(R"({
        "type":"repeater","id":"action_list","bind":"actionbar","horizontal":true,"itemSpacing":6,"size":[900,44],
        "item":{"type":"button","id":"slot","bind":"item.label","actionBind":"actionbar.use","size":[150,44]}})"));
    ASSERT_NE(rep, nullptr);
    Phyxel::UI::HudDataContext ctx;
    ctx.setList("actionbar", [] {
        std::vector<Phyxel::UI::HudRecord> rows;
        Phyxel::UI::HudRecord a; a.texts["label"] = "Attack"; a.texts["action"] = "attack"; a.floats["enabled"] = 1.0f; rows.push_back(a);
        Phyxel::UI::HudRecord b; b.texts["label"] = "End Turn"; b.texts["action"] = "end_turn"; b.floats["enabled"] = 1.0f; rows.push_back(b);
        return rows;
    });
    std::vector<std::string> fired;
    ctx.setAction("actionbar.use", [&](const Phyxel::UI::HudRecord& r) { fired.push_back(r.texts.at("action")); });
    Phyxel::UI::applyHudBindings(rep.get(), ctx);
    Phyxel::UI::UITheme theme;
    // repeater drawn at (100, 600): button 0 spans x 100..250, button 1 spans 256..406
    EXPECT_TRUE(rep->handleClick({120.0f, 620.0f}, {100.0f, 600.0f}, theme));
    EXPECT_TRUE(rep->handleClick({300.0f, 620.0f}, {100.0f, 600.0f}, theme));
    EXPECT_FALSE(rep->handleClick({253.0f, 620.0f}, {100.0f, 600.0f}, theme)) << "the gap between buttons is not a button";
    EXPECT_FALSE(rep->handleClick({120.0f, 700.0f}, {100.0f, 600.0f}, theme)) << "below the bar";
    ASSERT_EQ(fired.size(), 2u);
    EXPECT_EQ(fired[0], "attack");
    EXPECT_EQ(fired[1], "end_turn");
    // hover mirrors the same layout
    rep->handleHover({300.0f, 620.0f}, {100.0f, 600.0f}, theme);
    auto* r = static_cast<Phyxel::UI::UIRepeater*>(rep.get());
    EXPECT_FALSE(static_cast<Phyxel::UI::UIButton*>(r->generated[0].get())->hovered);
    EXPECT_TRUE(static_cast<Phyxel::UI::UIButton*>(r->generated[1].get())->hovered);
}

// G-108: the health panel sat behind the dialogue panel (probe L4 2026-09-15: lint
// "'hud_health' overlaps 'hud_dialogue' by 274x90 px"). Authoring needs "hide WHILE a
// dialogue is up": visibleWhen accepts a leading '!' (fail-closed like the plain form -
// a negated key with no provider still hides). RED before: "!dialogue.active" resolved
// to no provider -> hidden always.
TEST(UILayoutTest, VisibleWhenAcceptsANegatedKey) {
    auto panel = Phyxel::UI::MenuDefinition::buildWidget(nlohmann::json::parse(R"({
        "type":"panel","id":"hp","size":[100,40],"visibleWhen":"!dialogue.active","children":[]})"));
    ASSERT_NE(panel, nullptr);
    Phyxel::UI::HudDataContext ctx;
    float active = 0.0f;
    ctx.setFloat("dialogue.active", [&] { return active; });
    Phyxel::UI::applyHudBindings(panel.get(), ctx);
    EXPECT_TRUE(panel->visible) << "no dialogue -> shown";
    active = 1.0f;
    Phyxel::UI::applyHudBindings(panel.get(), ctx);
    EXPECT_FALSE(panel->visible) << "dialogue up -> hidden";
    Phyxel::UI::HudDataContext none;
    Phyxel::UI::applyHudBindings(panel.get(), none);
    EXPECT_FALSE(panel->visible) << "no provider stays fail-closed even when negated";
}

// G-117 (manual review 2026-09-16: "compass or mini map - directions to go east mean
// nothing"). The compass strip maps bearings around the camera heading onto the strip:
// straight ahead sits at the centre, +-span/2 at the edges, anything behind is hidden;
// wrap-around is handled (heading 350, bearing 10 -> +20 deg, not -340). A JSON
// "compass" widget binds its heading float and its POI list.
TEST(UICompassTest, BearingsMapOntoTheStripAroundTheHeading) {
    using C = Phyxel::UI::UICompass;
    EXPECT_FLOAT_EQ(C::wrapDeg(190.0f), -170.0f);
    EXPECT_FLOAT_EQ(C::wrapDeg(-190.0f), 170.0f);
    ASSERT_TRUE(C::offsetFor(0.0f, 0.0f, 180.0f, 400.0f).has_value());
    EXPECT_FLOAT_EQ(*C::offsetFor(0.0f, 0.0f, 180.0f, 400.0f), 0.0f) << "dead ahead = centre";
    EXPECT_FLOAT_EQ(*C::offsetFor(90.0f, 0.0f, 180.0f, 400.0f), 200.0f) << "east on a north heading = right edge";
    EXPECT_FLOAT_EQ(*C::offsetFor(45.0f, 0.0f, 180.0f, 400.0f), 100.0f);
    EXPECT_FLOAT_EQ(*C::offsetFor(10.0f, 350.0f, 180.0f, 400.0f), 200.0f / 90.0f * 20.0f) << "wraps across north";
    EXPECT_FALSE(C::offsetFor(180.0f, 0.0f, 180.0f, 400.0f).has_value()) << "behind: hidden";
    EXPECT_FALSE(C::offsetFor(120.0f, 0.0f, 180.0f, 400.0f).has_value());

    auto w = Phyxel::UI::MenuDefinition::buildWidget(nlohmann::json::parse(R"({
        "type":"compass","id":"c","bind":"compass.heading","poiBind":"compass.poi","span":180,"size":[460,30]})"));
    ASSERT_NE(w, nullptr);
    auto* c = dynamic_cast<C*>(w.get());
    ASSERT_NE(c, nullptr);
    Phyxel::UI::HudDataContext ctx;
    ctx.setFloat("compass.heading", [] { return 90.0f; });
    ctx.setList("compass.poi", [] {
        std::vector<Phyxel::UI::HudRecord> rows;
        Phyxel::UI::HudRecord r; r.texts["label"] = "Hollin Farm"; r.floats["bearing"] = 100.0f; rows.push_back(r);
        return rows;
    });
    Phyxel::UI::applyHudBindings(w.get(), ctx);
    EXPECT_FLOAT_EQ(c->heading, 90.0f);
    ASSERT_EQ(c->pois.size(), 1u);
    EXPECT_EQ(c->pois[0].label, "Hollin Farm");
    EXPECT_FLOAT_EQ(c->pois[0].bearing, 100.0f);
}

// G-132 (manual review 2026-09-16: "window resize doesn't reposition UI", "loading screen
// not resized"): the HUD keeps its logical canvas and is placed into the window scaled to
// fit and centred; input arrives in window pixels and maps back onto the canvas.
TEST(UIPlacementTest, TheLogicalCanvasIsLetterboxedIntoTheWindow) {
    using U = Phyxel::UI::UISystem;
    auto p = U::placementFor(1280, 720, 1280, 720);
    EXPECT_FLOAT_EQ(p.scale, 1.0f); EXPECT_FLOAT_EQ(p.x, 0.0f); EXPECT_FLOAT_EQ(p.y, 0.0f);
    p = U::placementFor(1920, 1080, 1280, 720);
    EXPECT_FLOAT_EQ(p.scale, 1.5f) << "same aspect: scales up, no bars";
    EXPECT_FLOAT_EQ(p.x, 0.0f); EXPECT_FLOAT_EQ(p.y, 0.0f);
    p = U::placementFor(2560, 1080, 1280, 720);
    EXPECT_FLOAT_EQ(p.scale, 1.5f) << "ultrawide: height-limited";
    EXPECT_FLOAT_EQ(p.x, (2560.0f - 1280.0f * 1.5f) * 0.5f) << "centred with side bars";
    EXPECT_FLOAT_EQ(p.y, 0.0f);
    p = U::placementFor(1280, 1024, 1280, 720);
    EXPECT_FLOAT_EQ(p.scale, 1.0f) << "taller window: width-limited";
    EXPECT_FLOAT_EQ(p.y, (1024.0f - 720.0f) * 0.5f) << "letterbox bars top and bottom";
    p = U::placementFor(0, 0, 1280, 720);
    EXPECT_FLOAT_EQ(p.scale, 1.0f) << "a minimised window does not divide by zero";
}

// ============================================================================
// ACTION BAR ICONS + TOOLTIPS (Ravenmere G-148). User feedback 2026-09-21:
// "player action bar should show icons not long strings. each spell needs an icon...
// mouse over of a spell or action should then result in text information".
// The two halves are one feature: an icon-only bar you cannot hover is LESS readable
// than the sentences it replaced, so both are tested together here.
// ============================================================================

TEST(UIActionBarTest, RowsCarryTheirOwnIconAndTooltip) {
    const auto rep = Phyxel::UI::MenuDefinition::buildWidget(nlohmann::json::parse(R"({
        "type":"repeater","id":"action_list","bind":"actionbar","horizontal":true,"size":[900,52],
        "item":{"type":"button","id":"slot","bind":"item.label","iconBind":"item.icon",
                "tooltipBind":"item.tooltip","actionBind":"actionbar.use","size":[52,52]}})"));
    ASSERT_NE(rep, nullptr);
    Phyxel::UI::HudDataContext ctx;
    ctx.setList("actionbar", [] {
        std::vector<Phyxel::UI::HudRecord> rows;
        Phyxel::UI::HudRecord a;
        a.texts["label"] = "Attack";
        a.texts["icon"] = "resources/ui/icons/spells/attack.png";
        a.texts["tooltip"] = "Attack\nWeapon attack against the selected target.";
        a.floats["enabled"] = 1.0f;
        rows.push_back(a);
        Phyxel::UI::HudRecord b;
        b.texts["label"] = "Fire Bolt";
        b.texts["icon"] = "resources/ui/icons/spells/fire_bolt.png";
        b.texts["tooltip"] = "Fire Bolt\nCantrip - Evocation\n1d10 Fire";
        b.floats["enabled"] = 0.0f;
        rows.push_back(b);
        return rows;
    });
    Phyxel::UI::applyHudBindings(rep.get(), ctx);
    auto* r = static_cast<Phyxel::UI::UIRepeater*>(rep.get());
    ASSERT_EQ(r->generated.size(), 2u);
    auto* b0 = dynamic_cast<Phyxel::UI::UIButton*>(r->generated[0].get());
    auto* b1 = dynamic_cast<Phyxel::UI::UIButton*>(r->generated[1].get());
    ASSERT_NE(b0, nullptr);
    ASSERT_NE(b1, nullptr);
    EXPECT_EQ(b0->iconPath, "resources/ui/icons/spells/attack.png");
    EXPECT_EQ(b1->iconPath, "resources/ui/icons/spells/fire_bolt.png");
    EXPECT_EQ(b0->tooltip, "Attack\nWeapon attack against the selected target.");
    EXPECT_EQ(b1->tooltip, "Fire Bolt\nCantrip - Evocation\n1d10 Fire");
    // The label is still bound - it is what a missing PNG falls back to.
    EXPECT_EQ(b1->text, "Fire Bolt");
}

TEST(UIActionBarTest, HoverNamesTheRowUnderThePointerIncludingDisabledOnes) {
    const auto rep = Phyxel::UI::MenuDefinition::buildWidget(nlohmann::json::parse(R"({
        "type":"repeater","id":"action_list","bind":"actionbar","horizontal":true,"itemSpacing":6,
        "size":[900,52],
        "item":{"type":"button","id":"slot","bind":"item.label","iconBind":"item.icon",
                "tooltipBind":"item.tooltip","actionBind":"actionbar.use","size":[52,52]}})"));
    ASSERT_NE(rep, nullptr);
    Phyxel::UI::HudDataContext ctx;
    ctx.setList("actionbar", [] {
        std::vector<Phyxel::UI::HudRecord> rows;
        Phyxel::UI::HudRecord a;
        a.texts["label"] = "Attack"; a.texts["tooltip"] = "Attack"; a.floats["enabled"] = 1.0f;
        rows.push_back(a);
        Phyxel::UI::HudRecord b;   // depleted: greyed out, and the ONLY way to learn why
        b.texts["label"] = "Fireball"; b.texts["tooltip"] = "Fireball\nNo spell slots left";
        b.floats["enabled"] = 0.0f;
        rows.push_back(b);
        return rows;
    });
    Phyxel::UI::applyHudBindings(rep.get(), ctx);
    Phyxel::UI::UITheme theme;
    // drawn at (100, 600): row 0 spans x 100..152, row 1 spans 158..210
    rep->handleHover({120.0f, 620.0f}, {100.0f, 600.0f}, theme);
    EXPECT_EQ(Phyxel::UI::hoveredTooltip(rep.get()), "Attack");
    rep->handleHover({180.0f, 620.0f}, {100.0f, 600.0f}, theme);
    EXPECT_EQ(Phyxel::UI::hoveredTooltip(rep.get()), "Fireball\nNo spell slots left")
        << "a greyed row is exactly the one you hover to find out why it is greyed";
    rep->handleHover({155.0f, 620.0f}, {100.0f, 600.0f}, theme);
    EXPECT_EQ(Phyxel::UI::hoveredTooltip(rep.get()), "") << "the gap between rows is not a row";
    rep->handleHover({120.0f, 700.0f}, {100.0f, 600.0f}, theme);
    EXPECT_EQ(Phyxel::UI::hoveredTooltip(rep.get()), "") << "below the bar";
}

TEST(UIActionBarTest, AnIconRowStillFiresItsAction) {
    const auto rep = Phyxel::UI::MenuDefinition::buildWidget(nlohmann::json::parse(R"({
        "type":"repeater","id":"action_list","bind":"actionbar","horizontal":true,"itemSpacing":6,
        "size":[900,52],
        "item":{"type":"button","id":"slot","bind":"item.label","iconBind":"item.icon",
                "tooltipBind":"item.tooltip","actionBind":"actionbar.use","size":[52,52]}})"));
    Phyxel::UI::HudDataContext ctx;
    ctx.setList("actionbar", [] {
        std::vector<Phyxel::UI::HudRecord> rows;
        Phyxel::UI::HudRecord a;
        a.texts["label"] = "Attack"; a.texts["action"] = "attack";
        a.texts["icon"] = "resources/ui/icons/spells/attack.png";
        a.texts["tooltip"] = "Attack"; a.floats["enabled"] = 1.0f;
        rows.push_back(a);
        return rows;
    });
    std::vector<std::string> fired;
    ctx.setAction("actionbar.use", [&](const Phyxel::UI::HudRecord& r) { fired.push_back(r.texts.at("action")); });
    Phyxel::UI::applyHudBindings(rep.get(), ctx);
    Phyxel::UI::UITheme theme;
    EXPECT_TRUE(rep->handleClick({120.0f, 620.0f}, {100.0f, 600.0f}, theme));
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0], "attack");
}

TEST(UIActionBarTest, ADeeperTooltipWinsOverTheOneBehindIt) {
    auto panel = Phyxel::UI::MenuDefinition::buildWidget(nlohmann::json::parse(R"({
        "type":"panel","id":"bar","size":[200,60],"freeLayout":true,"tooltip":"the action bar",
        "children":[{"type":"button","id":"slot","size":[52,52],"position":[10,4],
                     "tooltip":"Fire Bolt"}]})"));
    ASSERT_NE(panel, nullptr);
    Phyxel::UI::UITheme theme;
    panel->handleHover({130.0f, 610.0f}, {100.0f, 600.0f}, theme);   // over the row
    EXPECT_EQ(Phyxel::UI::hoveredTooltip(panel.get()), "Fire Bolt");
    panel->handleHover({190.0f, 650.0f}, {100.0f, 600.0f}, theme);   // bar, but no row
    EXPECT_EQ(Phyxel::UI::hoveredTooltip(panel.get()), "the action bar");
}

// The icon set is GENERATED from resources/spells/*.json (tools/gen_spell_icons.py).
// Adding a spell without re-running it would ship a slot with no picture, which no test
// of the widget layer can catch - this is the guard that the art tracks the data.
#include "core/SpellDefinition.h"
#include <filesystem>
TEST(UIActionBarTest, EverySpellTheEngineLoadsHasAnIcon) {
    auto& reg = Phyxel::Core::SpellRegistry::instance();
    reg.loadFromDirectory("resources/spells");
    ASSERT_GT(reg.count(), 0u) << "run from the repo root; resources/spells must be readable";
    std::vector<std::string> missing;
    for (const auto* sp : reg.getAllSpells()) {
        if (!std::filesystem::exists("resources/ui/icons/spells/" + sp->id + ".png"))
            missing.push_back(sp->id);
    }
    for (const char* extra : {"attack", "end_turn"}) {
        if (!std::filesystem::exists(std::string("resources/ui/icons/spells/") + extra + ".png"))
            missing.push_back(extra);
    }
    EXPECT_TRUE(missing.empty())
        << missing.size() << " action-bar rows have no icon (first: " << missing.front()
        << ") - run: python tools/gen_spell_icons.py";
}
