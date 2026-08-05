#pragma once

#include <cstdint>
#include <string>

namespace Phyxel {
namespace Graphics {

/// World Rendering v2, M2 — the shared far-tree species table.
///
/// One row per tree FAMILY. The far-terrain worker classifies flora-plan template names into
/// species ids (stable, deterministic), and the render side maps the same ids to (a) a
/// REPRESENTATIVE template whose TemplateLodChain meshes are instanced in the mid band, and
/// (b) the card archetype used in the far tail. Card params live here too so the two tiers
/// can never drift apart on species identity.
struct TreeSpecies {
    const char* prefix;        ///< template-name prefix (longest-prefix wins, table order)
    const char* meshTemplate;  ///< representative template for the instanced LOD chain
    uint32_t    shapeClass;    ///< card silhouette: 0 broadleaf, 1 conifer, 2 palm, 3 dead
    float       height;        ///< card height (world units)
    float       canopyR;       ///< card canopy half-width
    uint8_t     r, g, b;       ///< card tint
};

/// Species ids are INDICES into this table — persisted into FarTreeInstance::packed bits 2-7,
/// so rows must only be APPENDED, never reordered.
inline const TreeSpecies* treeSpeciesTable(size_t& count) {
    static const TreeSpecies kRows[] = {
        {"forge_oak_hero",  "forge_oak_hero", 0, 15.0f, 5.5f,  92, 142,  58},
        {"forge_oak_l",     "forge_oak_l",    0, 12.0f, 4.5f,  88, 138,  56},
        {"forge_oak",       "forge_oak_m",    0,  9.0f, 3.6f,  84, 134,  54},
        {"forge_birch",     "forge_birch_m",  0, 10.0f, 3.2f, 140, 165,  82},
        {"forge_spruce_l",  "forge_spruce_l", 1, 14.0f, 3.2f,  42,  92,  56},
        {"forge_spruce",    "forge_spruce_m", 1, 11.0f, 2.8f,  46,  96,  58},
        {"forge_pine_l",    "forge_pine_l",   1, 13.0f, 3.0f,  56, 102,  60},
        {"forge_pine",      "forge_pine_m",   1, 10.0f, 2.6f,  58, 104,  62},
        {"forge_fir_l",     "forge_fir_l",    1, 12.0f, 2.8f,  44,  94,  57},
        {"forge_fir",       "forge_fir_m",    1, 10.0f, 2.6f,  46,  96,  58},
        {"forge_acacia",    "forge_acacia_m", 0,  7.5f, 4.6f, 112, 132,  62},
        {"forge_jungle_l",  "forge_jungle_l", 0, 13.0f, 4.6f,  58, 128,  60},
        {"forge_jungle",    "forge_jungle_m", 0, 10.0f, 4.0f,  62, 132,  62},
        {"forge_palm",      "forge_palm_m",   2,  8.0f, 3.0f,  82, 142,  70},
        {"forge_redwood",   "forge_redwood_xxl", 1, 22.0f, 5.5f, 60, 100, 60},
        {"forge_elder_oak", "forge_elder_oak_xxl", 0, 22.0f, 7.0f, 82, 130, 60},
        {"forge_enchanted", "forge_enchanted_oak", 0, 12.0f, 4.5f, 92, 160, 118},
        {"forge_dead",      "forge_dead_s",   3,  7.0f, 1.0f,  92,  76,  56},
        {"tree_apple",      "tree_apple",     0,  8.0f, 3.2f,  98, 148,  62},
        {"tree_",           "tree_apple",     0,  8.5f, 3.4f,  88, 138,  58},
    };
    count = sizeof(kRows) / sizeof(kRows[0]);
    return kRows;
}

/// Template name -> species id, or -1 for non-tree flora (bush/fern/shrub/flower).
/// Longest-prefix semantics via table order (specific rows precede their generic stem).
inline int treeSpeciesFor(const std::string& name) {
    if (name.find("bush") != std::string::npos || name.find("fern") != std::string::npos ||
        name.find("shrub") != std::string::npos || name.find("flower") != std::string::npos)
        return -1;
    size_t n = 0;
    const TreeSpecies* rows = treeSpeciesTable(n);
    for (size_t i = 0; i < n; ++i)
        if (name.rfind(rows[i].prefix, 0) == 0) return int(i);
    return -1;
}

} // namespace Graphics
} // namespace Phyxel
