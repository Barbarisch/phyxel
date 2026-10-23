#pragma once
// FarTreeResidencyMask.h -- which chunk COLUMNS near the camera visibly render near-field
// geometry, as a bit mask small enough to ride in push constants (G-178, 2026-09-23).
//
// The far-tree card and LOD-mesh tiers must never dissolve where the real tree does not exist
// yet (streaming is async: a gap where neither tier draws), and must never stay solid where it
// does (a pale impostor on top of the real oak). That "residency hold" used to be decided per
// TILE from four quadrant probes, and a tile that straddled the load radius held EVERY card in
// it solid - including cards 30 u from the camera over fully resident forest (the discs in
// Ravenmere's rv_horizon_l4_after.png). The hold is now per CARD: the vertex shader looks the
// card's own chunk column up in this mask. Bit set = some chunk in that column has uploaded
// faces (numInstances > 0) - the same test FarTerrainManager's chunk-coverage function uses to
// hide far tiles, so both tiers agree on what the near field visibly owns.
//
// Grid: kDim x kDim columns centred on the camera's column. 22 columns = +-11 chunks = +-352 u,
// which covers the whole near fade band (load 256 u -> +90 u) with margin; beyond the grid the
// distance fade alone keeps a card solid. 484 bits -> 16 words = 64 B of the 128 B push budget.
// Pure: built from a list of rendered columns, so it is unit-tested without a device.

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace Phyxel::Graphics {

struct FarTreeResidencyMask {
    static constexpr int kDim   = 22;
    static constexpr int kWords = 16;   // 484 bits round up to 512
    static_assert(kDim * kDim <= kWords * 32, "mask words must cover the grid");

    glm::ivec2 originChunk{0};              ///< chunk column of bit 0 (camera column - kDim/2)
    std::array<uint32_t, kWords> bits{};    ///< row-major: bit = (cz - origin.y) * kDim + (cx - origin.x)
    bool enabled = false;

    static glm::ivec2 originFor(const glm::ivec2& cameraChunk) {
        return cameraChunk - glm::ivec2(kDim / 2);
    }
    /// Bit index for a chunk column, or -1 when the column lies outside the grid.
    static int bitIndex(const glm::ivec2& column, const glm::ivec2& origin) {
        const glm::ivec2 c = column - origin;
        if (c.x < 0 || c.y < 0 || c.x >= kDim || c.y >= kDim) return -1;
        return c.y * kDim + c.x;
    }
    bool inGrid(int cx, int cz) const { return bitIndex({cx, cz}, originChunk) >= 0; }
    bool resident(int cx, int cz) const {
        const int b = bitIndex({cx, cz}, originChunk);
        return b >= 0 && ((bits[b >> 5] >> (b & 31)) & 1u) != 0u;
    }
    void set(int cx, int cz) {
        const int b = bitIndex({cx, cz}, originChunk);
        if (b >= 0) bits[b >> 5] |= (1u << (b & 31));
    }
    int residentCount() const {
        int n = 0;
        for (int b = 0; b < kDim * kDim; ++b) n += (bits[b >> 5] >> (b & 31)) & 1u;
        return n;
    }

    /// Build from the columns that visibly render (a chunk with uploaded faces somewhere in the
    /// column). Columns outside the grid are ignored. Order-independent: a set of bits.
    static FarTreeResidencyMask fromColumns(const glm::ivec2& cameraChunk,
                                            const std::vector<glm::ivec2>& renderedColumns) {
        FarTreeResidencyMask m;
        m.originChunk = originFor(cameraChunk);
        m.enabled = true;
        for (const auto& c : renderedColumns) m.set(c.x, c.y);
        return m;
    }
};

}  // namespace Phyxel::Graphics
