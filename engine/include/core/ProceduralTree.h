#pragma once

#include "core/VoxelTemplate.h"
#include <string>
#include <cstdint>

namespace Phyxel {

/// Runtime branch-driven tree generator — a C++ port of tools/gen_tree.py (same algorithm, so
/// authored pool trees and procedural runtime trees share a look). Produces a VoxelTemplate
/// (cubes/subcubes/microcubes) deterministically from (type, height, fullness, seed). Geometry
/// is computed in SUB space (1 unit = 1/3 cube) and compressed on emit. Floaters are pruned so
/// nothing detaches. See docs/WorldRecipeAndFlora.md and project_biome_flora.
class ProceduralTree {
public:
    /// type: oak | autumn | birch | bush | spruce | acacia | palm | dead (unknown -> oak).
    /// log/leaf are material names (so biomes can theme the same shape). leaf "" = no foliage.
    static VoxelTemplate generate(const std::string& type, int height, float fullness,
                                  uint32_t seed, const std::string& log = "Log",
                                  const std::string& leaf = "Leaf");
};

} // namespace Phyxel
