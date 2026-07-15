#include "core/ChopManager.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

ChopManager::ChopResult ChopManager::addChop(const glm::ivec3& base,
                                             const std::string& material,
                                             int trunkHeight, float chopPower,
                                             float hardness) {
    ChopResult out;
    out.base = base;

    const Key key = keyOf(base);
    auto it = m_trees.find(key);
    if (it == m_trees.end()) {
        // First contact fixes this tree's hardness/height/material.
        TreeState st;
        st.hardness    = std::max(1.0f, hardness);
        st.trunkHeight = trunkHeight;
        st.material    = material;
        it = m_trees.emplace(key, st).first;
    }
    TreeState& st = it->second;

    if (st.felled) {
        // Already down — no re-fire, no further accumulation.
        out.progress = 1.0f;
        out.alreadyFelled = true;
        return out;
    }

    st.accumulated += std::max(0.0f, chopPower);
    out.progress = std::min(1.0f, st.accumulated / st.hardness);

    if (st.accumulated >= st.hardness) {
        st.felled = true;
        out.felled = true;
        out.progress = 1.0f;
        if (m_onFelled) {
            TreeFellEvent ev;
            ev.base        = base;
            ev.material    = st.material;
            ev.trunkHeight = st.trunkHeight;
            ev.totalChop   = st.accumulated;
            m_onFelled(ev);
        }
    }
    return out;
}

float ChopManager::progressAt(const glm::ivec3& base) const {
    auto it = m_trees.find(keyOf(base));
    if (it == m_trees.end()) return 0.0f;
    if (it->second.felled) return 1.0f;
    return std::min(1.0f, it->second.accumulated / it->second.hardness);
}

bool ChopManager::isFelled(const glm::ivec3& base) const {
    auto it = m_trees.find(keyOf(base));
    return it != m_trees.end() && it->second.felled;
}

void ChopManager::forget(const glm::ivec3& base) {
    m_trees.erase(keyOf(base));
}

void ChopManager::clear() {
    m_trees.clear();
}

} // namespace Core
} // namespace Phyxel
