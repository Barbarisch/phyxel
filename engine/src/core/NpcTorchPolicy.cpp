#include "core/NpcTorchPolicy.h"

#include <cmath>

namespace Phyxel {
namespace Core {

bool NpcTorchPolicy::parse(const std::string& t, TorchPolicy& out) {
    if (t == "night")   { out = TorchPolicy::Night;   return true; }
    if (t == "dark")    { out = TorchPolicy::Dark;    return true; }
    if (t == "always")  { out = TorchPolicy::Always;  return true; }
    if (t == "never")   { out = TorchPolicy::Never;   return true; }
    if (t == "default") { out = TorchPolicy::Default; return true; }
    return false;
}

const char* NpcTorchPolicy::name(TorchPolicy p) {
    switch (p) {
        case TorchPolicy::Never:  return "never";
        case TorchPolicy::Night:  return "night";
        case TorchPolicy::Dark:   return "dark";
        case TorchPolicy::Always: return "always";
        default:                  return "default";
    }
}

TorchPolicy NpcTorchPolicy::resolve(TorchPolicy own, TorchPolicy gameDefault, bool hostile, bool monster) {
    if (own != TorchPolicy::Default) return own;
    if (hostile || monster) return TorchPolicy::Never;
    return gameDefault == TorchPolicy::Default ? TorchPolicy::Night : gameDefault;
}

bool NpcTorchPolicy::wantsTorch(TorchPolicy p, bool night, bool indoors) {
    switch (p) {
        case TorchPolicy::Always: return true;
        case TorchPolicy::Night:  return night && !indoors;
        case TorchPolicy::Dark:   return night || indoors;
        default:                  return false;   // Never; Default is resolved before this
    }
}

bool NpcTorchPolicy::updateNight(bool wasNight, float elev) {
    if (wasNight) return !(elev > kNightOffDeg);
    return elev < kNightOnDeg;
}

bool NpcTorchPolicy::updateIndoors(NpcTorchState& s, bool raw, float dt) {
    if (!s.known) {   // first sample (or first after a pause in scanning): no latency
        s.known = true;
        s.indoors = raw;
        s.pending = 0.0f;
        return s.indoors;
    }
    if (raw == s.indoors) { s.pending = 0.0f; return s.indoors; }
    s.pending += dt;
    if (s.pending >= (raw ? kIndoorOnSec : kIndoorOffSec) - 1e-4f) {
        s.indoors = raw;
        s.pending = 0.0f;
    }
    return s.indoors;
}

NpcTorchPolicy::Shelter NpcTorchPolicy::scanShelter(const MicroSampler& solid, const glm::vec3& feet) {
    Shelter out;
    if (!solid) return out;
    auto micro = [](float w) { return static_cast<int>(std::floor(w * 9.0f)); };
    const int fx = micro(feet.x), fz = micro(feet.z);

    // Roof: five columns - the NPC's own and four 1/3-cube offsets - so a gap between roof
    // boards directly overhead does not read as open sky.
    const int y0 = micro(feet.y + kRoofFromFeet);
    const int y1 = micro(feet.y + kRoofFromFeet + kRoofReach);
    static const int kOff[5][2] = {{0, 0}, {3, 0}, {-3, 0}, {0, 3}, {0, -3}};
    for (const auto& o : kOff) {
        for (int y = y0; y <= y1 && !out.roof; ++y)
            if (solid(glm::ivec3(fx + o[0], y, fz + o[1]))) out.roof = true;
        if (out.roof) break;
    }
    if (!out.roof) return out;   // no roof: walls cannot make it indoors, skip the rays

    // Walls: four horizontal rays at chest height.
    const int wy = micro(feet.y + kWallHeight);
    const int reach = static_cast<int>(kWallReach * 9.0f);
    static const int kDir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& d : kDir) {
        for (int i = 1; i <= reach; ++i) {
            if (solid(glm::ivec3(fx + d[0] * i, wy, fz + d[1] * i))) { ++out.walls; break; }
        }
    }
    return out;
}

} // namespace Core
} // namespace Phyxel
