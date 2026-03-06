#pragma once

#include <glm/glm.hpp>
#include <functional>

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
    
private:
    GenerationType generationType;
    uint32_t seed;
    TerrainParams terrainParams;
    GenerationFunction customGenerator;
    
    // Generation implementations
    bool generateRandom(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generatePerlin(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generateFlat(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generateMountains(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generateCaves(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    bool generateCity(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    
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
