#pragma once

#include "core/VfxDirector.h"
#include <string>

namespace Phyxel {

// ============================================================================
// Layer 3 — gameplay → VFX mapping.
//
// Maps a spell id to a fully-resolved VfxComposition, scaled by gameplay
// modifiers. The VFX layer stays dumb: this is where damage/mastery/crit/range
// turn into concrete particle counts, sizes, radii, colors, and distances.
// The per-spell base composition (which archetypes a spell uses) lives here too
// (Layer 2 content); gameplay (e.g. SpellResolver's CastResult) supplies the
// modifiers. See [[project-spell-vfx]].
// ============================================================================

struct VfxSpellModifiers {
    float power      = 1.0f;   // magnitude (~ damage / reference): scales count/size/radius/intensity
    int   tier       = 0;      // upcast slots above base / mastery: palette shift + extra particles
    bool  crit       = false;  // critical hit: bigger + brighter
    float rangeUnits = 0.0f;   // optional projectile travel-distance override (world units; 0 = preset)
};

// Resolve a spell id + modifiers into a ready-to-cast composition.
// Unknown ids fall back to a generic sphere burst. The returned composition's
// params are already scaled — hand it straight to VfxDirector::cast().
VfxComposition resolveSpellVfx(const std::string& spellId, const VfxSpellModifiers& mods);

} // namespace Phyxel
