#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <optional>
#include <vector>
#include <utility>

namespace Phyxel {
namespace UI {

/// One row of list data for a Repeater. Fields are looked up by an item's
/// "item.<field>" bind / visibleWhen keys (floats for progressbars/visibility,
/// texts for labels; a float falls back to text).
struct HudRecord {
    std::unordered_map<std::string, float>       floats;
    std::unordered_map<std::string, std::string> texts;
};

/**
 * @brief Typed data-binding registry for HUD widgets.
 *
 * Hosts (editor Application, standalone GameShell) register named providers that
 * read live game state; widgets reference a provider via their `bind` key. Each
 * frame, before render, applyBindings() walks the widget tree and pulls the
 * current provider values into the widgets. Widgets do NOT store gameplay state
 * — the provider remains the single source of truth (a HUD widget merely mirrors
 * the latest value for drawing).
 *
 * v1 providers (see docs/HudSystem.md §6): player.health, ... — extend by
 * registering more providers. This generalizes GameMenuRenderer's `{{token}}`
 * resolver into typed (float / text) bindings usable by bars and labels.
 */
class HudDataContext {
public:
    using FloatProvider = std::function<float()>;
    using TextProvider  = std::function<std::string()>;
    using ListProvider  = std::function<std::vector<HudRecord>()>;

    void setFloat(const std::string& key, FloatProvider p) { floatProviders_[key] = std::move(p); }
    void setText (const std::string& key, TextProvider  p) { textProviders_[key]  = std::move(p); }
    void setList (const std::string& key, ListProvider  p) { listProviders_[key]  = std::move(p); }

    void clear() { floatProviders_.clear(); textProviders_.clear(); listProviders_.clear(); }

    std::optional<float> resolveFloat(const std::string& key) const {
        auto it = floatProviders_.find(key);
        if (it == floatProviders_.end() || !it->second) return std::nullopt;
        return it->second();
    }
    std::optional<std::string> resolveText(const std::string& key) const {
        auto it = textProviders_.find(key);
        if (it == textProviders_.end() || !it->second) return std::nullopt;
        return it->second();
    }
    std::optional<std::vector<HudRecord>> resolveList(const std::string& key) const {
        auto it = listProviders_.find(key);
        if (it == listProviders_.end() || !it->second) return std::nullopt;
        return it->second();
    }

    // The widget-tree application (scalars, visibleWhen, repeaters) lives in
    // MenuDefinition (UI::applyHudBindings) where JSON item-template building is
    // available — keeps this header light (it's pulled in widely via RenderCoordinator).

private:
    std::unordered_map<std::string, FloatProvider> floatProviders_;
    std::unordered_map<std::string, TextProvider>  textProviders_;
    std::unordered_map<std::string, ListProvider>  listProviders_;
};

} // namespace UI
} // namespace Phyxel
