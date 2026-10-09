#pragma once

#include "core/water/WaterSpanWriteBack.h"

#include <vector>

// WaterCore Phase D slice D2 (docs/WaterCore.md 16.3): edits never create water (rule 3). The
// PURE column rules; the application decides ownership (an awake volume owns its columns and
// writes the truth back itself) and residency (a run clipped into a non-resident vertical chunk is
// HELD: left as it is, counted, resolved by the next wake).
namespace Phyxel::Core::Water {

struct SpanEditResult {
    bool   changed = false;
    double displaced = 0.0;   ///< m^3 removed by a placed solid (debited to the body, never re-minted)
};

/// Voxel (x, y, z) became SOLID: every run of column (x, z) is split around [y, y+1); the overlap
/// is displaced. Parts thinner than a millimetre are dropped (counted in `displaced`).
SpanEditResult spanSolidPlaced(std::vector<WorldSpan>& spans, int x, int y, int z);

/// Voxel (x, y, z) became AIR: a run resting exactly on it (bottom == y + 1) falls one voxel
/// (bottom - 1, top - 1: mass exact). Any other dig - a pit, a side wall, a rim - changes nothing:
/// sideways flow needs the motion layer, and a pit gets NO water (rule 3).
SpanEditResult spanSolidRemoved(std::vector<WorldSpan>& spans, int x, int y, int z);

/// True when any run of column (x, z) reaches into a vertical chunk that `resident(cy)` says is
/// absent: the edit must be held (the caller counts it).
template <typename ResidentFn>
bool spanEditHeld(const std::vector<WorldSpan>& spans, int x, int z, ResidentFn resident) {
    for (const auto& s : spans) {
        if (s.x != x || s.z != z) continue;
        const int cyA = static_cast<int>(std::floor(s.bottomY / 32.0f));
        const int cyB = static_cast<int>(std::floor((s.topY - 1e-4f) / 32.0f));
        for (int cy = cyA; cy <= cyB; ++cy) if (!resident(cy)) return true;
    }
    return false;
}

}  // namespace Phyxel::Core::Water
