#include "core/MonsterVisualRegistry.h"

#include <fstream>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

namespace {
constexpr const char* kBindingsPath = "resources/monsters/visuals/bindings.json";
}

MonsterVisualRegistry& MonsterVisualRegistry::instance() {
    static MonsterVisualRegistry s;
    return s;
}

int MonsterVisualRegistry::ensureLoaded() {
    if (m_loaded) return static_cast<int>(m_visuals.size());
    m_loaded = true;

    std::ifstream in(kBindingsPath);
    if (!in.is_open()) {
        LOG_WARN("MonsterVisual", "no bindings file at {}", kBindingsPath);
        return 0;
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        LOG_ERROR("MonsterVisual", "failed to parse {}: {}", kBindingsPath, e.what());
        return 0;
    }

    for (auto it = j.begin(); it != j.end(); ++it) {
        const nlohmann::json& v = it.value();
        if (!v.is_object()) continue;
        MonsterVisual mv;
        mv.monsterId = it.key();
        mv.animFile  = v.value("animFile", std::string{});
        if (mv.animFile.empty()) {
            LOG_WARN("MonsterVisual", "binding '{}' has no animFile — skipped", it.key());
            continue;
        }
        if (v.contains("animationMapping") && v["animationMapping"].is_object()) {
            for (auto am = v["animationMapping"].begin();
                 am != v["animationMapping"].end(); ++am)
                mv.animationMapping[am.key()] = am.value().get<std::string>();
        }
        if (v.contains("appearance")) mv.appearance = v["appearance"];
        mv.faction = v.value("faction", std::string{"monsters"});
        m_visuals[mv.monsterId] = std::move(mv);
    }
    LOG_INFO("MonsterVisual", "loaded {} monster visual bindings", m_visuals.size());
    return static_cast<int>(m_visuals.size());
}

const MonsterVisual* MonsterVisualRegistry::get(const std::string& monsterId) const {
    auto it = m_visuals.find(monsterId);
    return it != m_visuals.end() ? &it->second : nullptr;
}

}  // namespace Core
}  // namespace Phyxel
