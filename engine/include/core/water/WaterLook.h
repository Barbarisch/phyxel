#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// WaterCore Phase G3 (docs/WaterCore.md 18.7): the per-body LOOK profile - the knobs a game sets to
// make one body of water look different from another. Every knob is optional: unset means "today's
// derived value" (Pope & Fry clear water, W2's depth-proxy turbidity, W3's wind roughness), so a
// world that never sets a look renders exactly as before.
namespace Phyxel::Core::Water {

struct WaterLook {
    float     clarity = -1.0f;            ///< m: Secchi depth (how far down a white disc stays visible). < 0 = unset (derived: ~11 m)
    glm::vec3 tint{-1.0f};                ///< linear RGB, the colour deep water glows with, in the units of today's (0.04, 0.18, 0.24). x < 0 = unset
    float     turbidity = -1.0f;          ///< 0..1 murkiness. < 0 = unset (derived)
    float     roughness = -1.0f;          ///< 0..2 ripple strength. < 0 = unset (derived)

    bool hasClarity() const { return clarity >= 0.0f; }
    bool hasTint() const { return tint.x >= 0.0f; }
    bool hasTurbidity() const { return turbidity >= 0.0f; }
    bool hasRoughness() const { return roughness >= 0.0f; }
    bool any() const { return hasClarity() || hasTint() || hasTurbidity() || hasRoughness(); }
    bool operator==(const WaterLook& o) const { return clarity == o.clarity && tint == o.tint && turbidity == o.turbidity && roughness == o.roughness; }
};

/// The ranges a look may take. Clamped values are reported (one note per clamp) so the route can
/// echo what it changed instead of silently storing something else.
///  clarity 0.1..100 m (below 0.1 m the fog is opaque at one voxel; above 100 m no natural water)
///  tint each channel 0..1; turbidity 0..1; roughness 0..2 (2 = twice the shipped ripple slope)
WaterLook clampLook(const WaterLook& in, std::vector<std::string>* notes = nullptr);

/// The shader-ready form: look0 = (tint.rgb or -1, clarity or 0), look1 = (turbidity or -1,
/// roughness or -1, 0, 0). The neutral (all unset) value is look0 = (-1, -1, -1, 0), look1 = (-1, -1, 0, 0).
struct WaterLookPacked {
    glm::vec4 look0{-1.0f, -1.0f, -1.0f, 0.0f};
    glm::vec4 look1{-1.0f, -1.0f, 0.0f, 0.0f};
};
WaterLookPacked packLook(const WaterLook& l);

/// JSON object of the SET knobs only ({} when nothing is set); and back. `fromJson` ignores unknown
/// keys and treats null as unset.
std::string lookToJson(const WaterLook& l);
WaterLook lookFromJson(const std::string& text);

}  // namespace Phyxel::Core::Water
