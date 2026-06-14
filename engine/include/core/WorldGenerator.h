#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

namespace Phyxel {

class Chunk;

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
    };

    // Per-column terrain sample, computed once per (x,z) by the column-first pipeline.
    // Biomes hang off the climate fields here.
    struct ColumnSample {
        int   surfaceY = 16;          // world Y of the top solid voxel
        float temperature = 0.5f;     // [0,1]
        float moisture    = 0.5f;     // [0,1]
        float continentalness = 0.5f; // [0,1] large-scale land elevation
        int   biomeIndex  = 0;        // dominant biome (index into m_biomes)
    };

    // Load biome definitions from JSON (resources/biomes.json). Returns false (and keeps
    // the built-in defaults) if the file is missing or invalid.
    bool loadBiomes(const std::string& path);
    const std::vector<Biome>& getBiomes() const { return m_biomes; }

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
