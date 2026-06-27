#pragma once

// ============================================================================
// FenceBuilder — turn a fence RUN into a THIN, GROUNDED, TYPED fence profile (place_fence #22).
//
// A fence is NOT a 1 m cube wall. It is a thin line of posts + rails (+ pickets / close boards) at a
// grounded height. This builds the per-run micro-voxel profile from the dimension canon
// (resources/object_dimensions.json: fence_picket/privacy/post_rail — height, post_spacing, rails,
// all cited), so a picket comes out ~0.9 m tall, ~0.1 m thick, posts every ~1.8 m — measured against
// the canon by FenceProfileTest (a 1 m-thick or 1.6 m-tall "fence" FAILS).
//
// Units: micro = 1/9 m (1 cube = 9 micro). Local frame: u = ALONG the run, y = UP from the ground,
// w = ACROSS (thickness). The caller orients (u->edge axis, w->perpendicular) and seats y on terrain.
// ============================================================================

#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

enum class FenceType { Picket, Privacy, PostRail };

FenceType   fenceTypeFromString(const std::string& s);   ///< unknown -> Picket
std::string fenceTypeToString(FenceType t);
std::string fenceArchetype(FenceType t);                 ///< canon id: "fence_picket" | "fence_privacy" | "fence_post_rail"

struct FenceMicro { int u = 0, y = 0, w = 0; };          ///< one micro-voxel in the run's local frame

struct FenceProfile {
    std::vector<FenceMicro> cells;   ///< the thin fence geometry along the run
    int heightMicro = 0;             ///< realized top (micro) — should match the canon height
    int thickMicro = 0;              ///< realized thickness (micro) across the run — THIN (a few micro), not 9
    bool ok = false;
};

/// Build a fence profile for a run of `runLenMicro` (along u), to `heightMicro` tall, with posts every
/// `postSpacingMicro`, `rails` horizontal rails, and a type-specific infill: Picket = spaced vertical
/// slats (gaps), Privacy = solid close boards, PostRail = posts + rails only (open). The fence is
/// `thickMicro` (a few micro) THICK, never a full cube. Deterministic. Caller supplies the canon-derived
/// dims (height/post_spacing/rails) so this stays pure + unit-testable against object_dimensions.json.
FenceProfile planFenceProfile(int runLenMicro, int heightMicro, int postSpacingMicro, int rails,
                              FenceType type, int thickMicro = 1);

}  // namespace Core
}  // namespace Phyxel
