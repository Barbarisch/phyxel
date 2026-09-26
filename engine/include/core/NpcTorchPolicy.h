#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// NpcTorchPolicy - who carries a lit torch, and when (Ravenmere T-73, 2026-09-26).
//
// Nights are dark now (a real sun arc + a faint moon), so townsfolk light a torch when
// they are out in the dark. Per NPC, game.json "torch":
//   "night"  - outdoors at night (the default for non-hostile NPCs)
//   "dark"   - outdoors at night AND in any enclosed place, day or night (miners, cellar keepers)
//   "always" / "never"
// A game-wide default comes from game.json "npcDefaults": {"torch": ...}. Hostile NPCs and
// monsters default to "never" (an ambusher does not announce itself with a flame).
//
// Every input is WORLD STATE, never camera state (the chunks-must-not-be-visible rule):
//   night   = the SUN'S ELEVATION (the same arc the sky draws and the shadows use), with
//             hysteresis: on below -2 deg, off above +2 deg. Not DayNightCycle::isNight's
//             fixed 18:00-06:00, which no longer matches the sky since the sun got a latitude.
//   indoors = a scan of static voxel occupancy around the NPC (scanShelter): a roof over the
//             head AND walls on at least three of four sides. NOT the renderer's
//             light-occupancy sky visibility, which is centred on the CAMERA and reads "open
//             sky" outside it - the same NPC would hold a torch near the player and not across
//             town. Unloaded chunks read as open.
// ============================================================================
enum class TorchPolicy : uint8_t { Default, Never, Night, Dark, Always };

/// Per-NPC torch state: its authored policy plus the indoor latch (see updateIndoors).
struct NpcTorchState {
    TorchPolicy policy = TorchPolicy::Default;
    bool  indoors = false;     ///< latched shelter state
    bool  known = false;       ///< the latch holds a real sample (false = take the next one as-is)
    float pending = 0.0f;      ///< seconds the raw sample has disagreed with the latch
    float scanTimer = 0.0f;    ///< seconds until the next shelter scan
    bool  lit = false;         ///< the POLICY lit the torch it carries (it only puts out its own)
};

class NpcTorchPolicy {
public:
    static constexpr const char* kTorchItemId = "torch";

    // Hysteresis. Night: a 4-degree band around the horizon, so the flame does not blink at
    // dusk while the clock is paused near the threshold. Indoors: ON after 0.5 s under cover
    // (a townsperson who steps inside puts the torch out almost at once), OFF only after 2 s in
    // the open (walking past a doorway, under a lintel, through a gap in a roof, does not
    // flicker it). Scans run at 4 Hz per NPC - the latch times are multiples of that.
    static constexpr float kNightOnDeg   = -2.0f;
    static constexpr float kNightOffDeg  =  2.0f;
    static constexpr float kIndoorOnSec  =  0.5f;
    static constexpr float kIndoorOffSec =  2.0f;
    static constexpr float kScanInterval =  0.25f;

    // Shelter scan extents, in cubes (1 cube ~ 1 m; a character is ~1.8 cubes tall).
    static constexpr float kRoofFromFeet  = 2.0f;    ///< the roof scan starts above the head
    static constexpr float kRoofReach     = 12.0f;   ///< a ceiling up to 14 cubes above the feet counts
    static constexpr float kWallHeight    = 1.6f;    ///< chest height: above tables and bar counters
    static constexpr float kWallReach     = 12.0f;   ///< half the width of a large hall
    static constexpr int   kWallsIndoors  = 3;       ///< of 4 - a room with an open door is still a room

    /// "night" / "dark" / "always" / "never" / "default" -> policy. False = unknown text.
    static bool parse(const std::string& text, TorchPolicy& out);
    static const char* name(TorchPolicy p);

    /// The policy that applies: an NPC's own if set; otherwise "never" for hostile NPCs and
    /// monsters, else the game default (itself "night" when unset).
    static TorchPolicy resolve(TorchPolicy own, TorchPolicy gameDefault, bool hostile, bool monster);

    /// Does a (resolved) policy want the torch lit?
    static bool wantsTorch(TorchPolicy resolved, bool night, bool indoors);

    /// The night latch: sun elevation in degrees, previous state -> new state.
    static bool updateNight(bool wasNight, float sunElevationDeg);

    /// Indoor latch: feed a raw sample every scan; returns the latched state.
    static bool updateIndoors(NpcTorchState& s, bool rawIndoors, float secondsSinceLastSample);

    struct Shelter {
        bool roof = false;
        int  walls = 0;   ///< 0..4 horizontal directions blocked within kWallReach
        bool indoors() const { return roof && walls >= kWallsIndoors; }
    };
    /// Scan static occupancy around an NPC whose FEET are at `feet` (world units). The sampler
    /// answers "is this world MICRO cell (cube*9 + 0..8) solid?" - ChunkManager::occupiedMicro
    /// in the engine. Resolution-complete: a 1-micro plank roof or a subcube wall counts.
    using MicroSampler = std::function<bool(const glm::ivec3&)>;
    static Shelter scanShelter(const MicroSampler& occupiedMicro, const glm::vec3& feet);
};

} // namespace Core
} // namespace Phyxel
