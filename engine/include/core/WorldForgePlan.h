#pragma once

#include <climits>
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
    int a = 0, b = 0;           // site ids; -1 = a box-edge exit (M4a: the corridor continues outside)
    int cls = 1;                // 1 track / 2 road / 3 highway (endpoint tier min)
    std::vector<glm::vec2> centerline;   // world XZ, smoothed, ~16 u point spacing
    struct Crossing {
        glm::vec2 pos{0.0f};
        int riverOrder = 0;     // Strahler order of the channel crossed here
        bool carved = false;    // M2b-2: a CARVED river crossing -> realized as a bridge span
        /// M4c (D31/D32): what the crossing IS on a corridor-driven road — a ford (a creek,
        /// depth < fordMaxDepthU), a bridge within the grounded span, or refused (the road was
        /// re-routed, or the demand refused, with the reason). Legacy = the version-1 path.
        enum class Kind : uint8_t { Legacy = 0, Ford, Bridge, Refused };
        Kind kind = Kind::Legacy;
        float widthU = 0.0f;      // the channel's full width at the crossing (the M2b-2 law)
        float depthU = 0.0f;      // the channel's centreline depth
        float bankStepU = 0.0f;   // |bank A - bank B| surface step (bridges)
        float spanU = 0.0f;       // deck length (bridges)
    };
    static const char* crossingKindName(Crossing::Kind k) {
        switch (k) { case Crossing::Kind::Ford: return "ford"; case Crossing::Kind::Bridge: return "bridge";
                     case Crossing::Kind::Refused: return "refused"; default: return "legacy"; }
    }
    std::vector<Crossing> crossings;
    /// Slope-limited road surface Y per centerline vertex (road GRADING): the cut lower
    /// envelope of the real terrain (ascent <= kMaxRoadGrade in both walk directions)
    /// raised toward bridge-deck pins so approaches climb to meet the deck. sampleColumn
    /// pulls corridor surfaceY to the interpolated profile — cut AND fill both fall out
    /// of moving surfaceY. Empty = ungraded (legacy drape).
    std::vector<float> gradeProfile;
    /// M4b: the REAL emitted surface at each centreline vertex before any grading (the height the
    /// profile started from) - the earthwork at a vertex is gradeProfile - naturalY.
    std::vector<float> naturalY;
    /// M4b-3: switchback stretches inserted on this road (a pitched stretch replaced by legs
    /// across the slope at the class cap, folded inside the corridor band with hairpins).
    int switchbacks = 0;
    float switchbackLengthU = 0.0f;   // the legs' total length
    /// M4b-4: the earthwork census along the road (per vertex: cut = naturalY - gradeProfile):
    /// the length walled (cut > the bank limit), rock-faced (cut > bank limit + wallTierU) and
    /// the deepest cut; reported in the plan JSON on corridor-driven plans.
    /// walled: the centreline cut exceeds the bank limit (a wall tier stands at the edge);
    /// rock: walled AND the face top stands above the wall (the rock face is emitted);
    /// section: the face (centreline or far natural) exceeds the bank limit (an earthwork
    /// section exists, with or without a wall - a steep hillside beside a shallow cut).
    /// deepestCutU is the deepest FACE on the final profile (the D33 bound applies to it).
    float walledLengthU = 0.0f, rockLengthU = 0.0f, sectionLengthU = 0.0f, deepestCutU = 0.0f;
    glm::vec2 deepestCutAt{0.0f};
    /// M4b-4: the natural surface kEarthworkReachU past each vertex on the LEFT (x) and RIGHT (y)
    /// of the road (the vertex normal (-dir.y, dir.x) is left) - the bank above a walled cut rises
    /// at repose from the rock top to THIS height, so it meets the ground exactly at the reach
    /// whether the ground rises or falls away (a bank anchored to each column's own height fell
    /// at 1.8 per u on ground falling away and never met a rising slope - measured 2026-10-28).
    std::vector<glm::vec2> naturalSideY;
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
    /// M4c: the pier rule of a corridor-driven span — a pier every pierSpacingU once the deck is
    /// longer than singleSpanU (0 = the version-1 rule: stations every kPierSpacing, none under 2x).
    float pierSpacingU = 0.0f;
    float singleSpanU = 0.0f;
    int road = -1;                // M4c: the road this span belongs to (its pin cones); not serialized
};

/// M4c: a demand the box plan could not realize — no crossable crossing on any route (the reason
/// names the last refused crossing) or no dry route at all. Reported in the plan, never silent.
struct WorldForgeRefusedDemand {
    int a = -1, b = -1, cls = 1;
    glm::vec2 pos{0.0f};
    std::string reason;
    // M4c audit: the numbers the refusal was judged on (−1e30 where not reached), so a test can
    // measure the terrain at the refusal independently of the reason string.
    float bankLowU = -1e30f, bankHighU = -1e30f, rimU = -1e30f, deckU = -1e30f;
    float spanAU = -1e30f, spanBU = -1e30f;   // the banks' distances from pos along -tangent / +tangent
    glm::vec2 tangent{0.0f};   // the deck's direction at the crossing (the bank search line)
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
    /// M2c: the water level of a column (the generator's rim-corrected level); null = the raw bake.
    using LevelFn = std::function<float(float worldX, float worldZ)>;
    /// M4a: a routing-cost multiplier for a world position (1 = no change; the generator returns
    /// the reuse fraction inside the continent corridor band so the box roads follow the corridors).
    using CorridorFn = std::function<float(float worldX, float worldZ)>;
    /// M4a (D25): a demand edge the box plan routes INSTEAD of the V1 all-pairs + MST network.
    /// An endpoint is a pinned site (index into `sitePins` order) or, at -1, a world point —
    /// the spot where a continent corridor crosses the hydrology box's edge, so the fine road
    /// meets the macro-fidelity stamp outside (D28). cls = the corridor's class (1 track, 2 road,
    /// 3 highway). With a demand list, no road exists that the corridor graph did not ask for.
    struct DemandEdge {
        int a = -1, b = -1;
        glm::vec2 pa{0.0f}, pb{0.0f};
        int cls = 1;
    };

    /// Bake the plan. `params` is clamped internally (the plan echoes the clamped copy).
    /// Deterministic: same (params, worldSeed, terrain/hydrology inputs) → byte-identical
    /// toJson(). Not memoized process-wide (unlike the hydrology bake): the bake is cheap
    /// (~tens of ms) and its identity depends on biome tuning that the hydro key can't see —
    /// a wrong shared cache is a worse failure than a duplicate bake.
    static std::shared_ptr<const WorldForgePlan> bake(
        const WorldForgeParams& params, uint32_t worldSeed, const HeightFn& heightAt,
        const HydrologyMap& hydro, const FlowField& flow, const WaterBodyIndex& bodies,
        const SurfaceMatFn& surfaceMatAt, const SurfaceYFn& surfaceYAt,
        const ChannelFn& channelAt, const LevelFn& waterLevelAt = nullptr,
        const CorridorFn& corridorMulAt = nullptr, const std::vector<DemandEdge>* demand = nullptr);

    const WorldForgeParams& params() const { return m_params; }   // clamped echo
    const std::vector<WorldForgeSite>& sites() const { return m_sites; }
    const std::vector<WorldForgeRoad>& roads() const { return m_roads; }
    const std::vector<WorldForgeBridgeSpan>& bridges() const { return m_bridges; }
    const std::vector<WorldForgeRefusedDemand>& refusedDemands() const { return m_refused; }   // M4c

    /// Bridge query for one world column (sampleColumn, gated on a roadAt hit — decks lie
    /// inside the road corridor). Linear over the few spans; hit() false = no deck here.
    struct BridgeHit {
        float deckY = -1e30f;
        int cls = 0;
        bool rail = false;   // deck-EDGE band (lateral dist in (halfWidth-1, halfWidth]):
                             // generation raises a sub-voxel parapet here, never on the walkway
        bool pier = false;   // within a pier footprint — stations every kPierSpacing along the
                             // span, derived per query so baked plan data (and planHash) are
                             // unchanged by piers
        bool hit() const { return cls > 0; }
    };
    static constexpr float kPierSpacing = 12.0f;   // interior pier stations along a span
    static constexpr float kMaxRoadGrade = 0.5f;   // grade profile: max |dY| per u of arclength
    /// M4b: the deepest cut a road of this class takes before the grade may exceed its cap (a
    /// "short pitch"): the lateral room of the corridor — half-width + the 3 u grading shoulder
    /// (WorldGenerator kGradeBlend) — times the angle of repose of loose scree (kRockSlope 0.70,
    /// TerrainGenerationV2 P0). DERIVED, not typed: track 3.15 u, road 3.85 u, highway 4.2 u.
    static float cutLimit(int cls);
    static constexpr float kShoulderU = 3.0f;    // the grading shoulder (= WorldGenerator kGradeBlend)
    static constexpr float kReposeSlope = 0.70f; // loose scree (= WorldGenerator kRockSlope, P0)
    /// The widest shoulder the repose bank may take (DESIGN: 4x the grading shoulder, 12 u = an
    /// 8.4 u cut at repose; deeper cuts stand steeper and the bank test reports them - M4b-3's
    /// retaining walls own those). Bounds the raster's per-cell road index reach.
    static constexpr float kShoulderMaxU = 12.0f;
    /// M4c: the deepest cut or fill a bank at repose can carry inside the widest shoulder,
    /// kShoulderMaxU x kReposeSlope = 8.4 u (DERIVED from the two constants above; gradeBlendAt
    /// widens the shoulder by |profile - natural| / repose for a fill exactly as for a cut, so an
    /// embankment approach stands at repose like a cutting). A bridge abutment may cut the higher
    /// bank or fill the lower one this much; cutLimit (3.15-4.2 u) stays the PITCH trigger.
    static float bankLimit() { return kShoulderMaxU * kReposeSlope; }
    /// M4b-4: the lateral room an earthwork section needs beyond the plain shoulder - the wall's
    /// thickness (ceil(0.67 x a tier <= 3 u) <= 2 u) plus a column of slack (DERIVED).
    static constexpr float kEarthworkExtraU = 3.0f;
    /// M4b-4: where the far natural is sampled - the widest shoulder past the wall band
    static float earthworkReachU(int cls) { return roadHalfWidth(cls) + kEarthworkExtraU + kShoulderMaxU; }
    /// M4c: a centreline as an arc - cumulative length per vertex, a point's arc (its projection
    /// onto the NEAREST segment; legs of a hairpin closer than twice the point's offset would
    /// misproject - the bank offset is 3 u, DESIGN) and the point at an arc (clamped to the ends).
    /// Public statics so the bridge approach check is unit-tested (BridgeSpanTest).
    static std::vector<float> cumulativeArc(const std::vector<glm::vec2>& cl);
    static float arcOfPoint(const std::vector<glm::vec2>& cl, const std::vector<float>& cum, const glm::vec2& q);
    static glm::vec2 pointAtArc(const std::vector<glm::vec2>& cl, const std::vector<float>& cum, float arc);
    BridgeHit bridgeAt(float worldX, float worldZ) const;

    /// Road query for one world column — the per-column generation hook (sampleColumn).
    /// O(1): raster cell lookup + exact distance against <= 3 candidate segments.
    struct RoadHit {
        int cls = 0;            // 0 = no road here
        float dist = 1e9f;      // distance to the road centerline (world units)
        int roadIdx = -1;       // index into roads()
        float gradeY = -1e30f;  // graded road surface Y at the query's centerline projection
                                // (interpolated from the road's gradeProfile; -1e30 = none)
    };
    RoadHit roadAt(float worldX, float worldZ) const;
    /// M4b-2: the CONTINUOUS grade field — every road whose corridor + 3 u shoulder reaches the
    /// point contributes its graded Y with a weight that falls from 1 inside the corridor to 0
    /// at the shoulder's edge; gradeY = the weighted mean, weight = the largest weight (how much
    /// of the natural surface survives). Continuous by construction where corridors overlap,
    /// which the nearest-road pick was not (the junction seam, section 10). Corridor-driven
    /// (demand) plans only — blendsGrades() — the version-1 path keeps roadAt + reconciliation.
    struct GradeBlend {
        float gradeY = -1e30f;
        float weight = 0.0f;
        int coreRoad = -1;   // the road whose profile the surface follows here (inside its corridor), -1 = a shoulder blend
        // M4b-4: where the core road's cut exceeds the bank limit the column is part of an
        // EARTHWORK section - the surface is given outright (surfaceY), with a wall tier
        // (wallBaseY, wallTopY], a rock face (rockBaseY, rockTopY] and the side the road lies on
        // (towardRoad, a unit vector); surfaceY = -1e30 means the plain blend applies.
        float surfaceY = -1e30f;
        float wallBaseY = -1e30f, wallTopY = -1e30f;
        float rockBaseY = -1e30f, rockTopY = -1e30f;
        glm::vec2 towardRoad{0.0f};
    };
    /// naturalY = the column's pre-grading surface (the bank below a cut runs from the road edge
    /// down to it at the repose slope, so a deep cut widens its own shoulder); roadsOut (tests)
    /// receives every road index that contributed.
    GradeBlend gradeBlendAt(float worldX, float worldZ, float naturalY, std::vector<int>* roadsOut = nullptr) const;
    bool blendsGrades() const { return m_blendGrades; }

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
    /// M0: road segments the 8 u raster could NOT index (uint16 segment table). Roads past the
    /// cap exist in the plan but `roadAt` cannot see them - a loud WARN at bake and this count
    /// (also in toJson as "raster_truncated_segments") replace the silent `break` the code had.
    size_t rasterTruncatedSegments() const { return m_rasterTruncatedSegments; }
    /// TEST-ONLY: lower the road-raster segment cap so a small fixture can exercise the
    /// truncation path (the real cap is 0xFFFE). 0 resets to the real cap.
    static void setRasterSegmentCapForTesting(size_t cap);
    /// FNV-1a over toJson().dump() — the realization ledger stores this to detect plan drift.
    uint64_t planHash() const;

private:
    WorldForgePlan() = default;

    WorldForgeParams m_params;
    std::vector<WorldForgeSite> m_sites;
    std::vector<WorldForgeRoad> m_roads;
    std::vector<WorldForgeBridgeSpan> m_bridges;
    std::vector<WorldForgeRefusedDemand> m_refused;   // M4c

    // Road raster: 8 u cells over the road-network bbox. Per cell the nearest global segment
    // index (0xFFFF = none within reach) — roadAt refines with exact segment distances.
    struct Raster {
        float originX = 0.0f, originZ = 0.0f;
        float cellSize = 8.0f;
        int cellsX = 0, cellsZ = 0;
        std::vector<uint16_t> nearestSeg;
        // M4b-2 audit: EVERY road within reach of a cell, with that road's nearest segment to the
        // cell centre (compressed rows: cellRoadsStart[cell]..cellRoadsStart[cell+1] index
        // cellRoadSeg) - two slots could not hold the 3-6 roads that bundle on a shared trunk.
        std::vector<uint32_t> cellRoadsStart;
        std::vector<uint16_t> cellRoadSeg;
    } m_raster;
    // Global segment table (all roads concatenated; raster values index this).
    struct Segment {
        glm::vec2 a{0.0f}, b{0.0f};
        uint16_t roadIdx = 0;
        uint8_t cls = 1;
        uint16_t vert = 0;   // index of `a` in its road's centerline (grade interpolation)
    };
    std::vector<Segment> m_segments;
    bool m_blendGrades = false;   // M4b-2: set for corridor-driven plans
    size_t m_rasterTruncatedSegments = 0;   ///< M0: segments dropped at the raster's uint16 cap (0 = none)
};

}  // namespace Phyxel
