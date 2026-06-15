#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

namespace Phyxel {

class Chunk;
struct WorldRecipe;

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
    void setGenerationType(GenerationType type) { generationType = type; }
    
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
    };
    
    TerrainParams& getTerrainParams() { return terrainParams; }

    // How far solid terrain extends below the surface. Default: unbounded (solid stone
    // forever, as you dig down). Set hasBedrock + bedrockY to add an indestructible floor.
    struct DepthProfile {
        bool hasBedrock = false;
        int  bedrockY = -2048;
    };
    DepthProfile& getDepthProfile() { return depthProfile; }

    // A biome's material rules, selected by climate (temperature + moisture). Data-driven
    // from resources/biomes.json; a built-in default set is always present as a fallback.
    struct Biome {
        std::string name = "Plains";
        std::string surfaceMaterial = "Grass";     // top voxel of the column
        std::string subsurfaceMaterial = "Dirt";   // few voxels below the surface
        std::string deepMaterial = "Stone";        // deep underground
        float tempMin = 0.0f, tempMax = 1.0f;      // climate cell (its CENTRE is the Voronoi site)
        float moistMin = 0.0f, moistMax = 1.0f;
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
        int floraSpacing = 6;   // min world-column distance between this biome's plants
        std::vector<std::pair<std::string, int>> flora;
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
    };

    // Load biome definitions from JSON (resources/biomes.json). Returns false (and keeps
    // the built-in defaults) if the file is missing or invalid.
    bool loadBiomes(const std::string& path);
    const std::vector<Biome>& getBiomes() const { return m_biomes; }

    // Per-world recipe (docs/WorldRecipeAndFlora.md): snapshot the current generation tuning
    // into a recipe, or apply a stored one. applyRecipe overrides climateFrequency + per-biome
    // extremeness (heightScale) + flora (density/spacing/items) by biome name; biome category
    // fields (materials, climate ranges) stay from biomes.json.
    WorldRecipe makeRecipe() const;
    void applyRecipe(const WorldRecipe& recipe);

    // Public surface/climate query: surface height + dominant biome for a world column.
    // Pure function of world (x,z) + seed/params, so it's seam-free and reusable by the
    // flora decoration pass (which lives outside WorldGenerator).
    ColumnSample sampleSurface(int worldX, int worldZ) { return sampleColumn(worldX, worldZ); }

    // A planned piece of flora: which template to stamp, the surface column it belongs to,
    // and the surface Y its trunk base sits on. The caller (which owns ObjectTemplateManager)
    // centers the template footprint on the column and stamps it. Kept template-agnostic so
    // WorldGenerator stays decoupled from the template/stamping subsystem.
    struct FloraPlacement {
        std::string templateName;
        int worldX = 0;
        int surfaceY = 16;
        int worldZ = 0;
    };

    // Deterministically scatter biome-appropriate flora across a world-column rectangle
    // [colMinX,colMaxX] x [colMinZ,colMaxZ] (inclusive, world coords). `edgeInset` columns are
    // skipped at the rectangle border so footprints don't spill past a fixed region's edge.
    std::vector<FloraPlacement> planFlora(int colMinX, int colMinZ, int colMaxX, int colMaxZ,
                                          int edgeInset = 8);

private:
    GenerationType generationType;
    uint32_t seed;
    TerrainParams terrainParams;
    DepthProfile depthProfile;
    std::vector<Biome> m_biomes;
    GenerationFunction customGenerator;

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
    float surfaceVariationFor(int worldX, int worldZ);   // terrain bumpiness around base level (per type)
    void initDefaultBiomes();
    std::string materialForColumn(int worldY, const ColumnSample& col) const;

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
