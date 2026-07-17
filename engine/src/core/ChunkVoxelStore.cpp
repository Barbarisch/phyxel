#include "core/ChunkVoxelStore.h"

namespace Phyxel {

namespace {
const std::string kNoMaterial;   // returned for air; stable reference
}

void ChunkVoxelStore::clear() {
    m_palette.clear();
    m_lookup.clear();
    m_idx.assign(kVoxels, kEmpty);
    m_state.assign(kVoxels, 0);
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

void ChunkVoxelStore::set(size_t idx, const std::string& material, bool visible) {
    if (idx >= kVoxels) return;
    m_idx[idx] = intern(material);
    m_state[idx] = visible ? kVisible : 0;
}

void ChunkVoxelStore::erase(size_t idx) {
    if (idx >= kVoxels) return;
    m_idx[idx] = kEmpty;
    m_state[idx] = 0;
}

const std::string& ChunkVoxelStore::material(size_t idx) const {
    if (idx >= kVoxels) return kNoMaterial;
    const uint16_t id = m_idx[idx];
    if (id == kEmpty || id >= m_palette.size()) return kNoMaterial;
    return m_palette[id];
}

size_t ChunkVoxelStore::solidCount() const {
    size_t n = 0;
    for (uint16_t v : m_idx) if (v != kEmpty) ++n;
    return n;
}

size_t ChunkVoxelStore::approxBytes() const {
    size_t bytes = m_idx.capacity() * sizeof(uint16_t) + m_state.capacity() * sizeof(uint8_t);
    for (const auto& s : m_palette) bytes += s.capacity() + sizeof(std::string);
    return bytes;
}

} // namespace Phyxel
