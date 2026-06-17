#pragma once

#include "ui/UIWidget.h"

#include <string>
#include <unordered_map>
#include <functional>
#include <optional>
#include <cstdio>
#include <utility>

namespace Phyxel {
namespace UI {

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

    void setFloat(const std::string& key, FloatProvider p) { floatProviders_[key] = std::move(p); }
    void setText (const std::string& key, TextProvider  p) { textProviders_[key]  = std::move(p); }

    void clear() { floatProviders_.clear(); textProviders_.clear(); }

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

    /// Walk the widget tree (depth-first) and push bound values into widgets.
    void applyBindings(UIWidget* root) const {
        if (!root) return;
        applyToWidget(root);
        if (root->type() == WidgetType::Panel) {
            for (auto& child : static_cast<UIPanel*>(root)->children) {
                applyBindings(child.get());
            }
        }
    }

private:
    void applyToWidget(UIWidget* w) const {
        if (!w || w->bind.empty()) return;
        switch (w->type()) {
            case WidgetType::ProgressBar: {
                if (auto v = resolveFloat(w->bind)) {
                    static_cast<UIProgressBar*>(w)->value = *v;
                }
                break;
            }
            case WidgetType::Label: {
                if (auto s = resolveText(w->bind)) {
                    static_cast<UILabel*>(w)->text = *s;
                } else if (auto v = resolveFloat(w->bind)) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%g", *v);
                    static_cast<UILabel*>(w)->text = buf;
                }
                break;
            }
            case WidgetType::Button: {
                if (auto s = resolveText(w->bind)) {
                    static_cast<UIButton*>(w)->text = *s;
                }
                break;
            }
            default:
                break;
        }
    }

    std::unordered_map<std::string, FloatProvider> floatProviders_;
    std::unordered_map<std::string, TextProvider>  textProviders_;
};

} // namespace UI
} // namespace Phyxel
