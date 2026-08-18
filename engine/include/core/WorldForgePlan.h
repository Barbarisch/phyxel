#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "core/FlowField.h"
#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"
#include "core/WorldForgeParams.h"

namespace Phyxel {

// ── WorldForge: the world-scale planning layer (docs/WorldForge.md) ───────────────────────────
//
// Sits between the hydrology bake and per-column generation: a PURE, deterministic plan of
// settlement SITES (scored on relief / water proximity / biome hostility / spacing) and the
// ROAD GRAPH connecting them (slope-averse A* on the hydrology cell grid, MST + detour-relaxed
// loops, smoothed centerlines). Baked once in WorldGenerator::rebuildCoarseModel next to the
// hydrology backings; immutable + shared_ptr-held, so the streaming workers' generator copies
// share it safely (the m_coarse/m_hydro contract).
//
// Consumers:
//  • sampleColumn reads roadAt() to stamp road surface material + gate flora (the riverOrder
//    pattern) — roads are a pure function of world position, seam-free by construction, and
//    exist in never-visited terrain (WorldRenderV2 P-DERIVED).
//  • the worldforge_build orchestration job realizes sites via SettlementBuildService.
//
// Forge contract (docs/ForgePattern.md): grounded params (tier presets/materials from
// resources/settlement_program.json; REASONED score terms logged in docs/WorldForge.md),
// byte-identical determinism (WorldForgePlanTest.PlanDeterminism), honest degradation (an
// unviable region yields fewer sites, surfaced — never an invented flat spot).

/// One planned settlement site.
struct WorldForgeSite {
    int id = 0;
    glm::ivec2 pos{0, 0};       // world-column centre of the settlement footprint
    std::string tier;           // "hamlet" | "village" | "town" (settlement_program.json tiers)
    int width = 0, depth = 0;   // footprint (cubes), from the tier preset
    uint32_t seed = 0;          // derived: hash(worldSeed, pos) — the canonical settlement seed
    std::string surfaceMat;     // resolved surface material at the centre (hostility echo)
    float surfaceY = 0.0f;      // planning-height estimate (coarse+relief; realization re-grounds)
    struct Score {
        float relief = 0.0f;    // flatness of the footprint window
        float water = 0.0f;     // proximity to a river/lake (bell: near, not in)
        float biome = 0.0f;     // surface-material hostility weight
        float total = 0.0f;
    } score;
};

/// One planned inter-settlement road.
struct WorldForgeRoad {
    int a = 0, b = 0;           // site ids
    int cls = 1;                // 1 track / 2 road / 3 highway (endpoint tier min)
    std::vector<glm::vec2> centerline;   // world XZ, smoothed, ~16 u point spacing
    struct Crossing {
        glm::vec2 pos{0.0f};
        int riverOrder = 0;     // order >= 3 channel crossed here -> realized as a bridge span
    };
    std::vector<Crossing> crossings;
};

/// One bridge deck spanning an order>=3 channel (placer #44, V2 of the road field). The
/// endpoints sit on the BANKS (found by marching the road centerline off the carve-accurate
/// channel), the deck is flat at the higher bank's surface, and generation emits it per
/// column — the only thing generateChunk ever places above the surface.
struct WorldForgeBridgeSpan {
    glm::vec2 a{0.0f}, b{0.0f};   // deck centerline endpoints (world XZ, on the road)
    float deckY = 0.0f;           // flat deck height (world Y of the deck cube layer)
    int cls = 1;                  // road class (width follows roadHalfWidth)
    int crossingOrder = 0;        // Strahler order of the channel spanned
};

class WorldForgePlan {
public:
    /// Full rendered surface height (coarse base + Layer-1 relief) — the SAME function the
    /// hydrology bake floods. Must be pure (no generator instance captured beyond immutable
    /// shared state).
    using HeightFn = std::function<float(float worldX, float worldZ)>;
    /// Resolved surface material for a world column (biome + physical overrides), used for the
    /// biome-hostility score term. Only invoked DURING bake — never stored in the plan.
    using SurfaceMatFn = std::function<std::string(int worldX, int worldZ)>;
    /// REAL column surface (sampleColumn's surfaceY — biome blend + carve included): bridge
    /// deck endpoints must meet the ground the generator actually emits, not the coarse height.
    using SurfaceYFn = std::function<int(int worldX, int worldZ)>;
    /// Carve-accurate channel query (WorldGenerator::channelHitAt — MEANDER-WARPED, unlike raw
    /// FlowField::channelAt): bridge spans must clear the channel as it is actually carved.
    using ChannelFn = std::function<FlowField::ChannelHit(float worldX, float worldZ)>;

    /// Bake the plan. `params` is clamped internally (the plan echoes the clamped copy).
    /// Deterministic: same (params, worldSeed, terrain/hydrology inputs) → byte-identical
    /// toJson(). Not memoized process-wide (unlike the hydrology bake): the bake is cheap
    /// (~tens of ms) and its identity depends on biome tuning that the hydro key can't see —
    /// a wrong shared cache is a worse failure than a duplicate bake.
    static std::shared_ptr<const WorldForgePlan> bake(
        const WorldForgeParams& params, uint32_t worldSeed, const HeightFn& heightAt,
        const HydrologyMap& hydro, const FlowField& flow, const WaterBodyIndex& bodies,
        const SurfaceMatFn& surfaceMatAt, const SurfaceYFn& surfaceYAt,
        const ChannelFn& channelAt);

    const WorldForgeParams& params() const { return m_params; }   // clamped echo
    const std::vector<WorldForgeSite>& sites() const { return m_sites; }
    const std::vector<WorldForgeRoad>& roads() const { return m_roads; }
    const std::vector<WorldForgeBridgeSpan>& bridges() const { return m_bridges; }

    /// Bridge query for one world column (sampleColumn, gated on a roadAt hit — decks lie
    /// inside the road corridor). Linear over the few spans; hit() false = no deck here.
    struct BridgeHit {
        float deckY = -1e30f;
        int cls = 0;
        bool hit() const { return cls > 0; }
    };
    BridgeHit bridgeAt(float worldX, float worldZ) const;

    /// Road query for one world column — the per-column generation hook (sampleColumn).
    /// O(1): raster cell lookup + exact distance against <= 3 candidate segments.
    struct RoadHit {
        int cls = 0;            // 0 = no road here
        float dist = 1e9f;      // distance to the road centerline (world units)
        int roadIdx = -1;       // index into roads()
    };
    RoadHit roadAt(float worldX, float worldZ) const;

    /// Paved half-width / surface material per road class. GROUNDED in
    /// resources/settlement_program.json street specs: track = hamlet Dirt lane, road =
    /// village Gravel main (width 5), highway = town Cobblestone main (width 6).
    static float roadHalfWidth(int cls);
    static const char* roadMaterial(int cls);

    /// Settlement footprint preset per tier (cubes). GROUNDED in the L4-tested settlement
    /// sizes (settlement morphology v2: village ~80x48, town 140x60; hamlet REASONED smaller).
    struct TierPreset {
        const char* name;
        int width, depth;
    };
    static TierPreset tierPreset(const std::string& tier);

    /// The canonical settlement seed for a site position (closes the "settlement seed is
    /// caller-supplied" gap): pure hash of (worldSeed, world position), the same derivation
    /// idiom structure floorplans use.
    static uint32_t siteSeed(uint32_t worldSeed, const glm::ivec2& pos);

    /// Full plan as JSON: sites + scores, road polylines (decimated), crossings, clamped
    /// params. The determinism contract is defined over this string.
    nlohmann::json toJson() const;
    /// FNV-1a over toJson().dump() — the realization ledger stores this to detect plan drift.
    uint64_t planHash() const;

private:
    WorldForgePlan() = default;

    WorldForgeParams m_params;
    std::vector<WorldForgeSite> m_sites;
    std::vector<WorldForgeRoad> m_roads;
    std::vector<WorldForgeBridgeSpan> m_bridges;

    // Road raster: 8 u cells over the road-network bbox. Per cell the nearest global segment
    // index (0xFFFF = none within reach) — roadAt refines with exact segment distances.
    struct Raster {
        float originX = 0.0f, originZ = 0.0f;
        float cellSize = 8.0f;
        int cellsX = 0, cellsZ = 0;
        std::vector<uint16_t> nearestSeg;
    } m_raster;
    // Global segment table (all roads concatenated; raster values index this).
    struct Segment {
        glm::vec2 a{0.0f}, b{0.0f};
        uint16_t roadIdx = 0;
        uint8_t cls = 1;
    };
    std::vector<Segment> m_segments;
};

}  // namespace Phyxel
