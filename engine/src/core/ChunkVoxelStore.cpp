#include "core/ChunkVoxelStore.h"

namespace Phyxel {

namespace {
const std::string kNoMaterial;   // returned for air; stable reference
}

void ChunkVoxelStore::clear() {
    m_palette.clear();
    m_lookup.clear();
    // Release, don't just clear: uniform stores must not hold the 96 KB dense arrays.
    std::vector<uint16_t>().swap(m_idx);
    std::vector<uint8_t>().swap(m_state);
    m_solidCount = 0;
    m_uniform = true;
    m_uniformIdx = kEmpty;
    m_uniformState = 0;
}

uint16_t ChunkVoxelStore::intern(const std::string& material) {
    auto it = m_lookup.find(material);
    if (it != m_lookup.end()) return it->second;
    // kEmpty is the sentinel, so the palette can hold at most kEmpty distinct materials. A chunk
    // realistically carries a handful; if this ever saturates, reuse the last slot rather than
    // aliasing air.
    if (m_palette.size() >= kEmpty) return static_cast<uint16_t>(m_palette.size() - 1);
    m_palette.push_back(material);
    const uint16_t id = static_cast<uint16_t>(m_palette.size() - 1);
    m_lookup.emplace(material, id);
    return id;
}

// The "split": materialize the dense arrays from the uniform value. Called exactly once, by the
// first write that doesn't conform to the uniform state; from then on the store is dense.
void ChunkVoxelStore::split() {
    if (!m_uniform) return;
    m_idx.assign(kVoxels, m_uniformIdx);
    m_state.assign(kVoxels, m_uniformState);
    m_uniform = false;
}

void ChunkVoxelStore::fillUniform(const std::string& material, bool visible) {
    m_palette.clear();
    m_lookup.clear();
    std::vector<uint16_t>().swap(m_idx);
    std::vector<uint8_t>().swap(m_state);
    m_uniform = true;
    m_uniformIdx = intern(material);
    m_uniformState = visible ? kVisible : 0;
    m_solidCount = kVoxels;
}

bool ChunkVoxelStore::isUniform() const { return m_uniform; }

const std::string& ChunkVoxelStore::uniformMaterial() const {
    if (!m_uniform || m_uniformIdx == kEmpty || m_uniformIdx >= m_palette.size())
        return kNoMaterial;
    return m_palette[m_uniformIdx];
}

void ChunkVoxelStore::set(size_t idx, const std::string& material, bool visible) {
    if (idx >= kVoxels) return;
    const uint16_t id = intern(material);
    const uint8_t state = visible ? kVisible : 0;
    if (m_uniform) {
        if (id == m_uniformIdx && state == m_uniformState) return;   // conforming — stay uniform
        split();
    }
    if (m_idx[idx] == kEmpty) ++m_solidCount;
    m_idx[idx] = id;
    m_state[idx] = state;
}

void ChunkVoxelStore::setVisible(size_t idx, bool visible) {
    if (idx >= kVoxels) return;
    if (m_uniform) {
        if (m_uniformIdx == kEmpty) return;                          // air: nothing to hide
        const bool cur = (m_uniformState & kVisible) != 0;
        if (cur == visible) return;                                  // conforming
        split();
    }
    if (m_idx[idx] == kEmpty) return;
    if (visible) m_state[idx] |= kVisible;
    else m_state[idx] &= static_cast<uint8_t>(~kVisible);
}

void ChunkVoxelStore::erase(size_t idx) {
    if (idx >= kVoxels) return;
    if (m_uniform) {
        if (m_uniformIdx == kEmpty) return;                          // already air everywhere
        split();
    }
    if (m_idx[idx] != kEmpty) --m_solidCount;
    m_idx[idx] = kEmpty;
    m_state[idx] = 0;
}

const std::string& ChunkVoxelStore::material(size_t idx) const {
    if (idx >= kVoxels) return kNoMaterial;
    const uint16_t id = m_uniform ? m_uniformIdx : m_idx[idx];
    if (id == kEmpty || id >= m_palette.size()) return kNoMaterial;
    return m_palette[id];
}

size_t ChunkVoxelStore::approxBytes() const {
    size_t bytes = m_idx.capacity() * sizeof(uint16_t) + m_state.capacity() * sizeof(uint8_t);
    for (const auto& s : m_palette) bytes += s.capacity() + sizeof(std::string);
    return bytes;
}

} // namespace Phyxel
