#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/CoarseWorldModel.h"
#include "core/FlowField.h"
#include "core/WaterBodyIndex.h"
#include "core/Spline.h"
#include "core/WaterOccupancy.h"
#include "core/WorldConstants.h"
#include "core/WorldForgeParams.h"

namespace Phyxel {

class Chunk;
struct WorldRecipe;
class HydrologyMap;
class FlowField;
struct MapCoarseData;
class WorldForgePlan;

/**
 * @brief World generation interface for procedural world creation
 * 
 * Provides various world generation algorithms that can be used to create
 * interesting terrain and structures instead of random cubes.
 */
class WorldGenerator {
public:
    // Generation function type: takes chunk coordinate and returns true if a cube should exist at local position
    using GenerationFunction = std::function<bool(const glm::ivec3& chunkCoord, const glm::ivec3& localPos)>;
    
    // Predefined generation types
    enum class GenerationType {
        Random,         // Random cubes (current default)
        Perlin,         // Perlin noise terrain
        Flat,           // Flat world
        Mountains,      // Mountain terrain
        Caves,          // Cave systems
        City,           // Urban structures
        Custom          // Custom function provided
    };
    
    // Constructor
    explicit WorldGenerator(GenerationType type = GenerationType::Random, uint32_t seed = 0);
    
    // Generate chunk content
    void generateChunk(Chunk& chunk, const glm::ivec3& chunkCoord);
    
    // Set custom generation function
    void setCustomGenerator(GenerationFunction func) { customGenerator = func; generationType = GenerationType::Custom; }
    
    // Change generation type
    void setGenerationType(GenerationType type) { generationType = type; clearColumnCache(); }
    
    // Terrain parameters
    struct TerrainParams {
        float heightScale = 16.0f;      // Maximum terrain height
        float frequency = 0.05f;        // Noise frequency (higher = more detail)
        int octaves = 4;                // Noise octaves (more = more detail layers)
        float persistence = 0.5f;       // How much each octave contributes
        float lacunarity = 2.0f;        // Frequency multiplier per octave
        float caveThreshold = 0.3f;     // Cave generation threshold
        float stoneLevel = 8.0f;        // Below this level, generate stone instead of grass
        float climateFrequency = 0.002f; // Biome size: lower = bigger biomes (~1/cf wavelength). 0.002 ~= 500-unit climate cells, biomes several chunks across.
        // Sea level this world generates against: the hydrology bake's Priority-Flood outlet AND
        // the seabed/altitude/flora material gates. Sourced from game.json `water.seaLevel` via the
        // recipe (applyRecipe rebakes); default keeps legacy behavior. Deliberately NOT consumed by
        // the continental height spline or Flat's surface Y — a water setting must never move
        // terrain that existing worlds/cameras were authored against (WaterLab sets 54).
        float seaLevelY = Core::kSeaLevelY;
    };
    
    TerrainParams& getTerrainParams() { return terrainParams; }

    // How far solid terrain extends below the surface. Default: unbounded (solid stone
    // forever, as you dig down). Set hasBedrock + bedrockY to add an indestructible floor.
    struct DepthProfile {
        bool hasBedrock = false;
        int  bedrockY = -2048;
    };
    DepthProfile& getDepthProfile() { return depthProfile; }

    // One vegetation band: a weighted template/type pool placed at its own spacing/density by an
    // independent local-maxima pass. Biomes stack these (a dense understory + sparse giants) —
    // see Biome::extraFloraLayers. Mirrors the legacy flat flora fields (which are "layer 0").
    struct FloraLayer {
        float density = 0.0f;
        int spacing = 6;
        std::string mode = "pool";       // "pool" | "procedural"
        float fullness = 0.85f;
        std::vector<std::pair<std::string, int>> items;  // (template/type, weight)
    };

    // A biome's material rules, selected by climate (temperature + moisture). Data-driven
    // from resources/biomes.json; a built-in default set is always present as a fallback.
    struct Biome {
        std::string name = "Plains";
        std::string surfaceMaterial = "Grass";     // top voxel of the column
        std::string subsurfaceMaterial = "Dirt";   // few voxels below the surface
        std::string deepMaterial = "Stone";        // deep underground
        float tempMin = 0.0f, tempMax = 1.0f;      // climate cell (its CENTRE is the Voronoi site)
        float moistMin = 0.0f, moistMax = 1.0f;
        // Optional third selection axis: continentalness (0 = ocean-adjacent, 1 = deep interior).
        // Defaults to the full range so a biome that doesn't care about it (most land biomes) is
        // selected purely on temp+moisture — its centre 0.5 is shared, so the term cancels in the
        // argmax/blend and adds no bias. Narrow it to gate a biome by ocean-distance/elevation
        // (the hook P2's ocean/coast biomes will use). (docs/TerrainGenerationV2.md §P1)
        float contMin = 0.0f, contMax = 1.0f;
        float heightScale = 1.0f;                  // multiplies terrain height variation
        float heightOffset = 0.0f;                 // added to the surface height (world units)
        // Optional surface scatter: some columns get surfaceAlt instead of surfaceMaterial,
        // in patches, so a floor reads as mixed (e.g. a forest floor of dirt + grass).
        std::string surfaceAlt = "";               // material for the scattered patches
        float surfaceAltChance = 0.0f;             // 0..1 fraction of columns that use surfaceAlt
        // Flora decoration: vegetation templates (gen_tree.py .voxel output) scattered on the
        // surface by the decoration pass. floraDensity = probability per candidate grid site;
        // flora = weighted (template name, weight) pool selected per placement.
        float floraDensity = 0.0f;
        int floraSpacing = 6;            // min world-column distance between this biome's plants
        std::string floraMode = "pool";  // "pool" = stamp templates; "procedural" = generate fresh
        float floraFullness = 0.85f;     // canopy density for procedural generation
        // In "pool" mode the flora pair is (template name, weight); in "procedural" mode the
        // first is a tree TYPE (oak/birch/bush/spruce/acacia/dead). These flat fields ARE flora
        // "layer 0" — the biome's primary vegetation band.
        std::vector<std::pair<std::string, int>> flora;
        // Additional flora bands with their OWN spacing/density, each placed by an independent
        // local-maxima pass. Lets one biome carry e.g. sparse giants (spacing 24-32) over a dense
        // understory (spacing 4-6) — the enchanted-forest requirement. Empty = single-layer (legacy).
        std::vector<FloraLayer> extraFloraLayers;
        // Fauna: wandering creatures (animal .anim rigs) that the runtime FaunaSpawner scatters
        // as roaming NPCs — much sparser than flora (spacing measured in tens of columns).
        // faunaDensity = probability per candidate site; fauna = weighted (animFile, weight) pool.
        float faunaDensity = 0.0f;
        int faunaSpacing = 48;            // min world-column distance between herd anchors
        std::vector<std::pair<std::string, int>> fauna;
    };

    // Per-column terrain sample, computed once per (x,z) by the column-first pipeline.
    // Biomes hang off the climate fields here.
    struct ColumnSample {
        int   surfaceY = 16;          // world Y of the top solid voxel
        float temperature = 0.5f;     // [0,1]
        float moisture    = 0.5f;     // [0,1]
        float continentalness = 0.5f; // [0,1] large-scale land elevation
        int   biomeIndex  = 0;        // dominant biome (index into m_biomes)
        std::string surfaceMat = "Grass"; // resolved surface material (biome surface or scatter)
        int   riverOrder  = 0;        // Strahler order of the river carved here (0 = no channel);
                                      // >0 marks a carved riverbed (for the flora gate + water runtime)
        bool  creekBed    = false;    // order 1-2 inner channel band (depth >= the runtime pin
                                      // threshold): the surface voxel is emitted as a 2/3 subcube
                                      // shelf so the creek rests in a 1/3-voxel recess
                                      // (water-as-terrain-stage P2)
        int   roadClass   = 0;        // WorldForge road here (1 track/2 road/3 highway; 0 = none) —
                                      // the riverOrder pattern: pure function of world position via
                                      // the baked plan; drives surface material + the flora gate
        float roadDist    = 0.0f;     // distance to the road centreline (valid when roadClass > 0)
    };

    // Load biome definitions from JSON (resources/biomes.json). Returns false (and keeps
    // the built-in defaults) if the file is missing or invalid.
    bool loadBiomes(const std::string& path);
    const std::vector<Biome>& getBiomes() const { return m_biomes; }
    uint32_t getSeed() const { return seed; }

    // Per-world recipe (docs/WorldModel.md): snapshot the current generation tuning
    // into a recipe, or apply a stored one. applyRecipe overrides climateFrequency + per-biome
    // extremeness (heightScale) + flora (density/spacing/items) by biome name; biome category
    // fields (materials, climate ranges) stay from biomes.json.
    WorldRecipe makeRecipe() const;
    void applyRecipe(const WorldRecipe& recipe);

    // Public surface/climate query: surface height + dominant biome for a world column.
    // Pure function of world (x,z) + seed/params, so it's seam-free and reusable by the
    // flora decoration pass (which lives outside WorldGenerator).
    ColumnSample sampleSurface(int worldX, int worldZ) { return sampleColumn(worldX, worldZ); }

    // ── THE AUTHORITATIVE WATER QUERY (docs/Water.md §5.2) ─────────────────────────
    // What water does the TERRAIN hold at this column? Returns false when the column is dry.
    //
    // This is the answer the whole water system should be built on, and it is cheap: the generator
    // already knows the column's REAL surface height, and the bake supplies a candidate level. The
    // span is then built by Phyxel::buildOpenWaterSpan, which derives its bottom from that real
    // surface — so a column whose ground stands above the level reports DRY, whatever the coarse
    // bake claims.
    //
    // ⚑CONTRAST WITH WHAT SHIPS TODAY: the renderer draws a camera-following sheet at the bake's
    // level with no per-column terrain check, which is why a lake sheet lay over a grass hillside
    // and why the engine's own validator measured 606 of 606 rim columns leaking. Both consult the
    // same bake; only this one consults the ground.
    //
    // ⚑Open-sky water only, inheriting buildOpenWaterSpan's scope: a cave lake beneath a surface
    // lake needs the column's full solidity profile and is a later increment.
    bool waterSpanAt(int worldX, int worldZ, WaterSpan& out);

    // The same answer for a whole rectangular block of columns, in ONE pass — this is the form
    // generation actually calls.
    //
    // ⚑WHY A BATCH FORM EXISTS AT ALL, MEASURED: `waterSpanAt` costs **3.4 ms per column**, because
    // each call re-floods the same neighbourhood and re-samples the same terrain. A 32x32 chunk is
    // 1,024 columns, so per-column resolution would cost ~3.5 s of flood PER CHUNK — generation
    // would stall for minutes. The cost is structural, so the fix is structural: sample each
    // column's terrain once, then run a single multi-source flood over the whole block.
    //
    // `spans` and `hasSpan` are sized w*d, row-major (index = z*w + x), relative to (minX, minZ).
    // `hasSpan[i] == 0` means that column holds no water.
    //
    // ⚑THE MARGIN IS LOAD-BEARING, NOT A TUNING KNOB. The flood can only reach bodies whose baked
    // seeds lie inside the sampled area, so columns near its border under-resolve. The block is
    // therefore sampled with `kWaterBlockMargin` columns of padding on every side and the padding's
    // answers are discarded. That padding is ALSO what keeps the result seam-free: two chunks that
    // overlap in the padding region see the same terrain and the same seeds, so they agree on their
    // shared shoreline. Shrink the margin and shorelines tear at chunk borders.
    void waterSpansForBlock(int minX, int minZ, int w, int d,
                            std::vector<WaterSpan>& spans, std::vector<uint8_t>& hasSpan);

    // Layer-0 from an imported drawn map (docs/TerrainGenerationV2.md P4): drive base
    // elevation from a heightmap instead of noise. Rebuilds the coarse model to sample the
    // map (and drops the procedural hydrology bake — the map's rivers are baked into the
    // height). The shared immutable data is copy-safe for the streaming worker's generator
    // copy. Pass nullptr to revert to the noise source. See core/MapCoarseSource.h.
    void setHeightmapSource(std::shared_ptr<const MapCoarseData> src);
    bool hasHeightmapSource() const { return static_cast<bool>(m_mapSource); }

    // Baked hydrology backings (docs/TerrainGenerationV2.md §P2), for the water runtime + tests.
    // Null for Flat / non-height-based types (nothing baked). Owned by the generator; the pointers
    // are valid until the next rebuild (ctor / applyRecipe).
    const FlowField*      riverNetwork() const { return m_flow.get(); }
    const HydrologyMap*   hydrology()    const { return m_hydro.get(); }
    // Water BODY identity over the bake (tangible-water Phase A): which body a wet column belongs
    // to and what kind (OCEAN/LAKE infinite; POND finite/scoopable). Null when nothing is baked.
    const WaterBodyIndex* waterBodies()  const { return m_waterBodies.get(); }

    // ── WorldForge plan (docs/WorldForge.md) ────────────────────────────────────────────────
    // The world-scale settlement + road plan, baked in rebuildCoarseModel right after the
    // hydrology bake (it consumes hydrology). Null when worldforge is disabled (every legacy
    // world) or nothing is baked. Immutable + shared_ptr → worker-copy-safe like m_hydro.
    const WorldForgePlan* worldForge() const { return m_worldForge.get(); }
    const WorldForgeParams& worldForgeParams() const { return m_worldForgeParams; }
    void setWorldForgeParams(const WorldForgeParams& p) { m_worldForgeParams = p; rebuildCoarseModel(); }
    // Bake a plan for arbitrary params WITHOUT storing it (the worldforge_plan preview
    // command). Requires a hydrology bake (returns null otherwise).
    std::shared_ptr<const WorldForgePlan> previewWorldForge(const WorldForgeParams& params);

    // ── Fine-scale ponds (tangible-water Phase B) ────────────────────────────────────────────
    // TRUE small ponds — sub-bake-cell depressions the 128 u/cell hydrology can never see —
    // discovered per 32×32-column analysis cell over a ±16 margin window (chunk-column cache
    // reuse; border-touching basins discarded = seam-free by construction; see FinePonds.h).
    // These are the FINITE bodies: contained (level = spill − freeboard, cannot leak),
    // genuinely flat, ≤ 200 columns — the shape the per-body delta store requires. Bake-cell
    // "ponds" stay infinite: their coarse levels fragment against fine terrain (measured).
    struct FinePondHit {
        int64_t id = -1;
        float   level = 0.0f;
        int     areaColumns = 0;
    };
    // The fine pond owning this world column, or id −1. Deterministic and memoized; safe on the
    // generation worker's copy (same argument as the column cache).
    FinePondHit finePondAt(int worldX, int worldZ);
    // Full pond records for one analysis cell (world-space; tests + tooling).
    struct StoredFinePond {
        int64_t id;
        float   level;
        glm::ivec2 bboxMinW, bboxMaxW;         // inclusive world columns
        std::vector<uint64_t> columns;         // packed world columns, sorted (membership test)
    };
    std::shared_ptr<const std::vector<StoredFinePond>> finePondsForCell(int cellX, int cellZ);

    // THE channel line, as the terrain actually carves it (water-as-terrain-stage P2): channelAt
    // through the SAME meander warp sampleColumn uses for the carve, valley, swale, and bed shelf.
    // The water runtime MUST bind these (not FlowField::channelAt on raw coordinates): the warp
    // displaces the channel by up to ~kMeanderAmp, so a raw-coordinate ribbon lands beside the
    // carved bed — measured live as creek pins with no shelf under them (floor 0.0, CreekLab
    // 2026-07-31). Empty hit / zero vector when no network is baked.
    FlowField::ChannelHit channelHitAt(float worldX, float worldZ) const;
    glm::vec2 channelFlowDirAt(float worldX, float worldZ) const;

    // Testing/tooling hook: clear the process-wide hydrology-bake memoization cache so the NEXT
    // generator construction re-bakes independently instead of sharing a cached backing. Used to keep
    // bake determinism genuinely under test (otherwise two same-seed generators share one cached
    // object and a determinism comparison becomes a tautology).
    static void clearHydroBakeCache();

    // A planned piece of flora: which template to stamp, the surface column it belongs to,
    // and the surface Y its trunk base sits on. The caller (which owns ObjectTemplateManager)
    // centers the template footprint on the column and stamps it. Kept template-agnostic so
    // WorldGenerator stays decoupled from the template/stamping subsystem.
    struct FloraPlacement {
        std::string templateName;   // pool: template name; procedural: tree type
        int worldX = 0;
        int surfaceY = 16;
        int worldZ = 0;
        bool procedural = false;    // generate fresh (vs stamp a pooled template)
        float fullness = 0.85f;     // canopy density for procedural generation
    };

    // Deterministically scatter biome-appropriate flora across a world-column rectangle
    // [colMinX,colMaxX] x [colMinZ,colMaxZ] (inclusive, world coords). `edgeInset` columns are
    // skipped at the rectangle border so footprints don't spill past a fixed region's edge.
    std::vector<FloraPlacement> planFlora(int colMinX, int colMinZ, int colMaxX, int colMaxZ,
                                          int edgeInset = 8);

    // A planned herd anchor: which animal rig, and the surface column it stands on. The
    // runtime FaunaSpawner turns these into wandering NPCs. Kept NPC-subsystem-agnostic so
    // WorldGenerator stays decoupled (same split as FloraPlacement/ObjectTemplateManager).
    struct FaunaPlacement {
        std::string animFile;       // animal rig to spawn (resources/animated_characters/*.anim)
        int worldX = 0;
        int surfaceY = 16;
        int worldZ = 0;
    };

    // Deterministically scatter biome-appropriate fauna anchors across a world-column rectangle
    // (same local-maximum Poisson planner as planFlora, but sparser and single-layer). Pure
    // function of (cell, seed) so a streamed chunk and a whole-region pass agree — the runtime
    // spawner can re-plan per chunk with no double-spawns.
    std::vector<FaunaPlacement> planFauna(int colMinX, int colMinZ, int colMaxX, int colMaxZ,
                                          int edgeInset = 0);

private:
    // The single meander displacement every channel-line consumer shares (see channelHitAt).
    glm::vec2 meanderedChannelPos(float wx, float wz) const;

    GenerationType generationType;
    uint32_t seed;
    TerrainParams terrainParams;
    DepthProfile depthProfile;
    std::vector<Biome> m_biomes;
    GenerationFunction customGenerator;

    // Layer 0 (docs/TerrainGenerationV2.md): low-frequency continental base + climate,
    // sampled+interpolated from a coarse grid. sampleColumn (Layer 1) reads the base
    // elevation from here and adds high-frequency ridged mountain detail on top. Held by
    // shared_ptr with a PURE source (captures seed/params by value) so the worker's
    // generator copy shares the same immutable, thread-safe model. Rebuilt whenever seed
    // or generation params change (ctor + applyRecipe).
    std::shared_ptr<CoarseWorldModel> m_coarse;
    void rebuildCoarseModel();

    // Optional Layer-0 override: an imported heightmap (P4). When set, rebuildCoarseModel
    // sources baseHeight/continentalness from it instead of noise, and skips the hydrology
    // bake. Immutable + shared_ptr → copy-safe for the streaming worker. See setHeightmapSource.
    std::shared_ptr<const MapCoarseData> m_mapSource;

    // Layer-0 hydrology baked over a BOUNDED region (docs/TerrainGenerationV2.md §P2): lake/sea
    // surface levels (m_hydro) and the river drainage network + Strahler-ordered channel geometry
    // (m_flow), computed ONCE per world build in rebuildCoarseModel over the FULL surface height
    // (coarse base + Layer-1 relief) so rivers sit in the rendered valleys. sampleColumn reads
    // m_flow->channelAt to carve riverbeds. Both are immutable plain-grid backings held by
    // shared_ptr with a PURE source, so the streaming worker's generator copy shares them safely
    // (like m_coarse). Columns outside the baked region get no water/rivers (infinite-world
    // partitioning is P5). Null for Flat / non-height-based types.
    std::shared_ptr<HydrologyMap>   m_hydro;
    std::shared_ptr<FlowField>      m_flow;
    std::shared_ptr<WaterBodyIndex> m_waterBodies;  // body identity riding the bake (Phase A)

    // WorldForge (docs/WorldForge.md): params come from the world recipe; the plan is baked
    // at the end of rebuildCoarseModel (after hydrology, which it consumes). NOT memoized
    // process-wide — the bake is cheap and its identity depends on biome tuning the hydro
    // key can't see (see WorldForgePlan::bake).
    WorldForgeParams m_worldForgeParams;
    std::shared_ptr<const WorldForgePlan> m_worldForge;
    void bakeWorldForgePlan();

    // Fine-pond registry cache (Phase B), FIFO-capped like the column cache.
    std::unordered_map<uint64_t, std::shared_ptr<const std::vector<StoredFinePond>>> m_finePondCache;
    std::vector<uint64_t> m_finePondCacheOrder;
    static constexpr size_t kFinePondCacheMax = 256;

    // The continentalness → base-elevation shaping spline (docs/TerrainGenerationV2.md §2a): the
    // "how tall" art-direction curve, decoupled from the "how mountainous" noise. Default is a
    // smoothstep ramp from the ocean/shelf floor to the high-interior plateau (behavior-identical to
    // the old hardcoded continentalBase); a world recipe may override the control points to reshape
    // coastlines/plateaus without recompiling. Captured BY VALUE into the coarse-model source, so it
    // is safe under the streaming worker's generator copy.
    Spline m_continentalHeightSpline;

    // Generation implementations
    bool generateRandom(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generatePerlin(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generateFlat(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generateMountains(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generateCaves(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generateCity(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);

    // Column-first pipeline (height-based types: Perlin/Flat/Mountains/Caves)
    bool isHeightBased() const;
    ColumnSample sampleColumn(int worldX, int worldZ);   // surface height + climate + blended biome

    // Per-column-chunk memo of the 32x32 sampleColumn results. Every vertical band of a
    // column stack needs the SAME 1024 samples — without the memo, a 5-band stack paid
    // the (dominant) column-sampling cost 5x per (x,z). Per-instance (each streaming
    // worker owns a private generator copy), so no locking. Invalidated at the coarse
    // rebuild points (applyRecipe / setGenerationType / setHeightmapSource / loadBiomes);
    // direct getTerrainParams() mutation is NOT tracked — recipe flows go through
    // applyRecipe, which clears it.
    std::shared_ptr<const std::vector<ColumnSample>> columnsForChunk(const glm::ivec2& colChunk);

    // Per-chunk-column water spans (docs/Water.md §2 layer 1), memoized like columnsForChunk and
    // for the same reason: every vertical chunk of a column stack clips the SAME 32x32 span set,
    // so the flood + margin sampling runs once per (x,z) chunk column, not once per chunk.
    // 1024 dense entries, index = x*32 + z (matches ColumnSample order); has[i] == 0 -> dry.
    struct ChunkColumnSpans {
        std::vector<WaterSpan> spans;   // world-space Y
        std::vector<uint8_t>   has;
    };
    std::shared_ptr<const ChunkColumnSpans> waterSpansForChunkColumn(const glm::ivec2& colChunk);

    void clearColumnCache() {
        m_columnCache.clear(); m_columnCacheOrder.clear();
        // Spans derive from the same terrain + bake the column samples do — one lifetime.
        m_waterSpanCache.clear(); m_waterSpanCacheOrder.clear();
    }
    static constexpr size_t kColumnCacheMax = 128;   // ~1024 samples/entry; FIFO eviction
    std::unordered_map<uint64_t, std::shared_ptr<const std::vector<ColumnSample>>> m_columnCache;
    std::vector<uint64_t> m_columnCacheOrder;
    std::unordered_map<uint64_t, std::shared_ptr<const ChunkColumnSpans>> m_waterSpanCache;
    std::vector<uint64_t> m_waterSpanCacheOrder;
    // Layer-1 ridged mountain relief at a column. `continentalness` is passed in (the caller already
    // sampled the coarse model) so we don't re-sample it here — the slope pass calls this per-neighbor.
    float surfaceVariationFor(int worldX, int worldZ, float continentalness);
    void initDefaultBiomes();
    std::string materialForColumn(int worldY, const ColumnSample& col) const;

    // Flora placement candidate grid (world columns per cell). Plants are decided per-cell by
    // an order-independent local-maxima test so a single chunk and a whole region agree.
    static constexpr int kFloraGrid = 3;
    // Plan one plant for a candidate cell in a given flora layer (0 = the biome's flat flora
    // fields; 1+ index into Biome::extraFloraLayers). Each layer places independently.
    bool floraCellLayer(int cellX, int cellZ, int layerIdx, FloraPlacement& out);

    // Plan one fauna herd anchor for a candidate cell (fauna-salted local-maxima test on the
    // shared kFloraGrid, using the biome's faunaSpacing/faunaDensity/fauna pool).
    bool faunaCell(int cellX, int cellZ, FaunaPlacement& out);

    // Material selection based on world position and terrain context (City/Random fallback)
    std::string getMaterialForPosition(const glm::ivec3& worldPos, float surfaceHeight) const;
    
    // Noise functions
    float perlinNoise3D(float x, float y, float z, int octaves, float persistence, float lacunarity);
    float noise3D(float x, float y, float z);
    float fade(float t);
    float lerp(float a, float b, float t);
    float grad(int hash, float x, float y, float z);
    
    // Utility functions
    int hash(int x, int y, int z);
};

} // namespace Phyxel
