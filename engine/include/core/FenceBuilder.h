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
// Post column positions (micro offsets along a run) — EVENLY distributed so an interior post never
// lands adjacent to an end/corner post (the doubled, messy corner). endPosts=false omits the two end
// columns so the perpendicular run's post is the single shared corner. Pure + deterministic: this is
// the ground-truth the corner-cleanliness unit test asserts on (a picket fence's voxel density can't
// distinguish a post from a slat, so density-based world detection is unsound — test the plan instead).
std::vector<int> fencePostPositions(int runLenMicro, int postSpacingMicro, bool endPosts = true);

// endPosts: place a post at BOTH run ends (a run that owns its corners). Pass false for the runs whose
// corners are owned by the perpendicular run, so adjacent edges meet at ONE shared corner post instead
// of doubling it. Posts are EVENLY distributed (never colliding with the end posts).
FenceProfile planFenceProfile(int runLenMicro, int heightMicro, int postSpacingMicro, int rails,
                              FenceType type, int thickMicro = 1, bool endPosts = true);

}  // namespace Core
}  // namespace Phyxel
