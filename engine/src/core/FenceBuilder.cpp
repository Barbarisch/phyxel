#include "core/FenceBuilder.h"

namespace Phyxel {
namespace Core {

FenceType fenceTypeFromString(const std::string& s) {
    if (s == "privacy") return FenceType::Privacy;
    if (s == "post_rail" || s == "postrail" || s == "post-rail") return FenceType::PostRail;
    return FenceType::Picket;
}
std::string fenceTypeToString(FenceType t) {
    switch (t) { case FenceType::Privacy: return "privacy"; case FenceType::PostRail: return "post_rail";
                 default: return "picket"; }
}
std::string fenceArchetype(FenceType t) {
    switch (t) { case FenceType::Privacy: return "fence_privacy"; case FenceType::PostRail: return "fence_post_rail";
                 default: return "fence_picket"; }
}

FenceProfile planFenceProfile(int runLenMicro, int heightMicro, int postSpacingMicro, int rails,
                              FenceType type, int thickMicro) {
    FenceProfile p;
    if (runLenMicro < 1 || heightMicro < 1 || thickMicro < 1) return p;
    if (postSpacingMicro < 1) postSpacingMicro = runLenMicro;     // degenerate -> just end posts
    p.heightMicro = heightMicro;
    p.thickMicro = thickMicro;

    auto add = [&](int u, int y) { for (int w = 0; w < thickMicro; ++w) p.cells.push_back({u, y, w}); };
    auto column = [&](int u) { for (int y = 0; y < heightMicro; ++y) add(u, y); };  // a full-height post/slat

    // POSTS: full-height columns at the grounded spacing, plus an end post at the run's end.
    for (int u = 0; u < runLenMicro; u += postSpacingMicro) column(u);
    column(runLenMicro - 1);

    // RAILS: `rails` horizontal lines spanning the whole run (what an open fence's slats hang on).
    for (int r = 1; r <= rails; ++r) {
        const int ry = heightMicro * r / (rails + 1);
        for (int u = 0; u < runLenMicro; ++u) add(u, ry);
    }

    // INFILL by type: privacy = solid close boards; picket = spaced vertical slats (gaps); post-rail =
    // nothing (posts + rails only -> open).
    if (type == FenceType::Privacy) {
        for (int u = 0; u < runLenMicro; ++u) column(u);
    } else if (type == FenceType::Picket) {
        for (int u = 0; u < runLenMicro; u += 2) column(u);       // slat / gap / slat ...
    }

    p.ok = true;
    return p;
}

}  // namespace Core
}  // namespace Phyxel
