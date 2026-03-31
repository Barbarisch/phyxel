#include "core/StructureGenerator.h"
#include "core/ChunkManager.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

// ============================================================================
// MaterialPalette
// ============================================================================

MaterialPalette MaterialPalette::fromJson(const nlohmann::json& j) {
    MaterialPalette p;
    if (j.is_object()) {
        p.wall      = j.value("wall", "Stone");
        p.floor     = j.value("floor", "Wood");
        p.roof      = j.value("roof", "Wood");
        p.door      = j.value("door", "");
        p.window    = j.value("window", "");
        p.stairs    = j.value("stairs", "Stone");
        p.furniture = j.value("furniture", "Wood");
    }
    return p;
}

// ============================================================================
// Facing helpers
// ============================================================================

Facing StructureGenerator::facingFromString(const std::string& s) {
    if (s == "north" || s == "North") return Facing::North;
    if (s == "east"  || s == "East")  return Facing::East;
    if (s == "south" || s == "South") return Facing::South;
    if (s == "west"  || s == "West")  return Facing::West;
    return Facing::South; // default: door faces south
}

std::string StructureGenerator::facingToString(Facing f) {
    switch (f) {
        case Facing::North: return "north";
        case Facing::East:  return "east";
        case Facing::South: return "south";
        case Facing::West:  return "west";
    }
    return "south";
}

DetailLevel StructureGenerator::detailLevelFromString(const std::string& s) {
    if (s == "rough"  || s == "Rough")  return DetailLevel::Rough;
    if (s == "fine"   || s == "Fine")   return DetailLevel::Fine;
    return DetailLevel::Detailed; // default
}

std::string StructureGenerator::detailLevelToString(DetailLevel d) {
    switch (d) {
        case DetailLevel::Rough:    return "rough";
        case DetailLevel::Detailed: return "detailed";
        case DetailLevel::Fine:     return "fine";
    }
    return "detailed";
}

glm::ivec3 StructureGenerator::rotateLocal(const glm::ivec3& local, Facing facing, int width, int depth) {
    // Rotate around the center of the bounding box in XZ plane.
    // North = identity (door on -Z side).
    // East  = 90° CW:  (x,y,z) -> (depth-1-z, y, x)
    // South = 180°:     (x,y,z) -> (width-1-x, y, depth-1-z)
    // West  = 270° CW:  (x,y,z) -> (z, y, width-1-x)
    switch (facing) {
        case Facing::North: return local;
        case Facing::East:  return {depth - 1 - local.z, local.y, local.x};
        case Facing::South: return {width - 1 - local.x, local.y, depth - 1 - local.z};
        case Facing::West:  return {local.z, local.y, width - 1 - local.x};
    }
    return local;
}

void StructureGenerator::transformResult(StructureResult& result, const glm::ivec3& worldOrigin,
                                          Facing facing, int width, int depth) {
    for (auto& v : result.voxels) {
        v.position = worldOrigin + rotateLocal(v.position, facing, width, depth);
    }
    // Rotate location markers too
    for (auto& loc : result.locations) {
        glm::ivec3 rotated = rotateLocal(glm::ivec3(loc.position), facing, width, depth);
        loc.position = glm::vec3(worldOrigin) + glm::vec3(rotated);
    }
}

// ============================================================================
// Primitives
// ============================================================================

StructureResult StructureGenerator::generateBox(const glm::ivec3& pos, int width, int height, int depth,
                                                 const std::string& material, bool hollow) {
    StructureResult result;
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            for (int z = 0; z < depth; ++z) {
                if (hollow && x > 0 && x < width - 1 &&
                    y > 0 && y < height - 1 && z > 0 && z < depth - 1) {
                    continue;
                }
                result.voxels.push_back({pos + glm::ivec3(x, y, z), material});
            }
        }
    }
    return result;
}

StructureResult StructureGenerator::generateWalls(const glm::ivec3& pos, int width, int height, int depth,
                                                   const std::string& material, int thickness) {
    StructureResult result;
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            for (int z = 0; z < depth; ++z) {
                bool isWall = (x < thickness || x >= width - thickness ||
                               z < thickness || z >= depth - thickness);
                if (isWall) {
                    result.voxels.push_back({pos + glm::ivec3(x, y, z), material});
                }
            }
        }
    }
    return result;
}

StructureResult StructureGenerator::generateFloor(const glm::ivec3& pos, int width, int depth,
                                                   const std::string& material) {
    StructureResult result;
    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < depth; ++z) {
            result.voxels.push_back({pos + glm::ivec3(x, 0, z), material});
        }
    }
    return result;
}

StructureResult StructureGenerator::generateRoom(const glm::ivec3& pos, int width, int height, int depth,
                                                  const MaterialPalette& mat) {
    StructureResult result;

    // Floor
    auto floor = generateFloor(pos, width, depth, mat.floor);
    result.voxels.insert(result.voxels.end(), floor.voxels.begin(), floor.voxels.end());

    // Walls (above the floor)
    auto walls = generateWalls(pos + glm::ivec3(0, 1, 0), width, height - 1, depth, mat.wall);
    result.voxels.insert(result.voxels.end(), walls.voxels.begin(), walls.voxels.end());

    // Ceiling
    auto ceiling = generateFloor(pos + glm::ivec3(0, height, 0), width, depth, mat.roof);
    result.voxels.insert(result.voxels.end(), ceiling.voxels.begin(), ceiling.voxels.end());

    return result;
}

std::vector<glm::ivec3> StructureGenerator::generateDoorOpening(const glm::ivec3& wallBase,
                                                                  int width, int height) {
    std::vector<glm::ivec3> positions;
    for (int dx = 0; dx < width; ++dx) {
        for (int dy = 0; dy < height; ++dy) {
            positions.push_back(wallBase + glm::ivec3(dx, dy, 0));
        }
    }
    return positions;
}

std::vector<glm::ivec3> StructureGenerator::generateWindowOpening(const glm::ivec3& wallBase,
                                                                    int width, int height) {
    std::vector<glm::ivec3> positions;
    for (int dx = 0; dx < width; ++dx) {
        for (int dy = 0; dy < height; ++dy) {
            positions.push_back(wallBase + glm::ivec3(dx, dy, 0));
        }
    }
    return positions;
}

StructureResult StructureGenerator::generateStaircase(const glm::ivec3& pos, Facing facing,
                                                       int height, int width,
                                                       const std::string& material) {
    // Build stairs in local coords ascending along +Z, then rotate.
    StructureResult result;
    for (int step = 0; step < height; ++step) {
        for (int w = 0; w < width; ++w) {
            // Each step: one block at local (w, step, step)
            glm::ivec3 local(w, step, step);
            result.voxels.push_back({local, material});
        }
    }
    transformResult(result, pos, facing, width, height);
    return result;
}

StructureResult StructureGenerator::generateSubcubeStaircase(const glm::ivec3& pos, Facing facing,
                                                              int height, int width,
                                                              const std::string& material) {
    // 3 subcube steps per block height. Each step rises 1 subcube (1/3 block) and
    // extends 1 subcube (1/3 block) deep. Total depth in blocks = height (same as cube stairs).
    // Within each block height, 3 steps at subcube positions Y=0,1,2 and Z=0,1,2.
    StructureResult result;

    for (int blockY = 0; blockY < height; ++blockY) {
        for (int step = 0; step < 3; ++step) {
            // step 0: subcubeY=0, subcubeZ=0 in block (w, blockY, blockY)
            // step 1: subcubeY=1, subcubeZ=1
            // step 2: subcubeY=2, subcubeZ=2
            int cubeZ = blockY;  // Each block height advances one block in Z

            for (int w = 0; w < width; ++w) {
                VoxelPlacement vp;
                vp.position = glm::ivec3(w, blockY, cubeZ);
                vp.material = material;
                vp.level = VoxelLevel::Subcube;
                vp.subcubePos = glm::ivec3(1, step, step);  // X=1 (center), Y/Z ascending

                result.voxels.push_back(vp);

                // Fill the step surface (all X subcubes for the tread)
                if (step > 0) {
                    // Add left/right subcubes for the tread surface
                    vp.subcubePos = glm::ivec3(0, step, step);
                    result.voxels.push_back(vp);
                    vp.subcubePos = glm::ivec3(2, step, step);
                    result.voxels.push_back(vp);
                } else {
                    vp.subcubePos = glm::ivec3(0, step, step);
                    result.voxels.push_back(vp);
                    vp.subcubePos = glm::ivec3(2, step, step);
                    result.voxels.push_back(vp);
                }

                // Fill solid subcubes below this step (riser)
                for (int fillY = 0; fillY < step; ++fillY) {
                    for (int sx = 0; sx < 3; ++sx) {
                        VoxelPlacement fill;
                        fill.position = glm::ivec3(w, blockY, cubeZ);
                        fill.material = material;
                        fill.level = VoxelLevel::Subcube;
                        fill.subcubePos = glm::ivec3(sx, fillY, step);
                        result.voxels.push_back(fill);
                    }
                }
            }
        }
    }

    transformResult(result, pos, facing, width, height);
    return result;
}

// ============================================================================
// Subcube detail primitives
// ============================================================================

StructureResult StructureGenerator::generateWindowFrame(const glm::ivec3& pos, Facing facing,
                                                         int width, int height,
                                                         const std::string& material) {
    // Generate a frame of subcubes around a window opening.
    // The frame sits on the wall surface (z=0 in local), bordering the opening.
    // Subcubes line the inner edge of each cube surrounding the opening.
    StructureResult result;
    int w = width, d = 1;

    // Bottom sill: subcubes at Y=-1 cube, subcubeY=2 (top subcube row of cube below opening)
    for (int x = 0; x < width; ++x) {
        VoxelPlacement vp;
        vp.position = glm::ivec3(x, -1, 0);
        vp.material = material;
        vp.level = VoxelLevel::Subcube;
        vp.subcubePos = glm::ivec3(1, 2, 1);
        result.voxels.push_back(vp);
    }

    // Top lintel: subcubes at Y=height cube, subcubeY=0 (bottom subcube row of cube above opening)
    for (int x = 0; x < width; ++x) {
        VoxelPlacement vp;
        vp.position = glm::ivec3(x, height, 0);
        vp.material = material;
        vp.level = VoxelLevel::Subcube;
        vp.subcubePos = glm::ivec3(1, 0, 1);
        result.voxels.push_back(vp);
    }

    // Left jamb: subcubes at X=-1 cube, subcubeX=2 (rightmost subcube column)
    for (int y = 0; y < height; ++y) {
        VoxelPlacement vp;
        vp.position = glm::ivec3(-1, y, 0);
        vp.material = material;
        vp.level = VoxelLevel::Subcube;
        vp.subcubePos = glm::ivec3(2, 1, 1);
        result.voxels.push_back(vp);
    }

    // Right jamb: subcubes at X=width cube, subcubeX=0 (leftmost subcube column)
    for (int y = 0; y < height; ++y) {
        VoxelPlacement vp;
        vp.position = glm::ivec3(width, y, 0);
        vp.material = material;
        vp.level = VoxelLevel::Subcube;
        vp.subcubePos = glm::ivec3(0, 1, 1);
        result.voxels.push_back(vp);
    }

    transformResult(result, pos, facing, width + 1, d);
    return result;
}

StructureResult StructureGenerator::generateDoorFrame(const glm::ivec3& pos, Facing facing,
                                                       int width, int height,
                                                       const std::string& material) {
    // Door frame: top lintel + left/right jambs (no bottom sill).
    StructureResult result;
    int w = width, d = 1;

    // Top lintel
    for (int x = 0; x < width; ++x) {
        VoxelPlacement vp;
        vp.position = glm::ivec3(x, height, 0);
        vp.material = material;
        vp.level = VoxelLevel::Subcube;
        vp.subcubePos = glm::ivec3(1, 0, 1);
        result.voxels.push_back(vp);
    }

    // Left jamb
    for (int y = 0; y < height; ++y) {
        VoxelPlacement vp;
        vp.position = glm::ivec3(-1, y, 0);
        vp.material = material;
        vp.level = VoxelLevel::Subcube;
        vp.subcubePos = glm::ivec3(2, 1, 1);
        result.voxels.push_back(vp);
    }

    // Right jamb
    for (int y = 0; y < height; ++y) {
        VoxelPlacement vp;
        vp.position = glm::ivec3(width, y, 0);
        vp.material = material;
        vp.level = VoxelLevel::Subcube;
        vp.subcubePos = glm::ivec3(0, 1, 1);
        result.voxels.push_back(vp);
    }

    transformResult(result, pos, facing, width + 1, d);
    return result;
}

StructureResult StructureGenerator::generateRailing(const glm::ivec3& pos, Facing facing,
                                                     int length, const std::string& material) {
    // Railing: posts at each end (full subcube columns) + top rail (subcube bar).
    // 1 cube tall, runs along local X.
    StructureResult result;
    int w = length, d = 1;

    // Top rail: subcubes at Y=0, subcubeY=2 (top third of the cube) across the length
    for (int x = 0; x < length; ++x) {
        for (int sz = 0; sz < 3; ++sz) {
            VoxelPlacement vp;
            vp.position = glm::ivec3(x, 0, 0);
            vp.material = material;
            vp.level = VoxelLevel::Subcube;
            vp.subcubePos = glm::ivec3(1, 2, sz);
            result.voxels.push_back(vp);
        }
    }

    // End posts: full subcube columns at X=0 and X=length-1
    for (int endX : {0, length - 1}) {
        for (int sy = 0; sy < 3; ++sy) {
            VoxelPlacement vp;
            vp.position = glm::ivec3(endX, 0, 0);
            vp.material = material;
            vp.level = VoxelLevel::Subcube;
            vp.subcubePos = glm::ivec3(1, sy, 1);
            result.voxels.push_back(vp);
        }
    }

    transformResult(result, pos, facing, w, d);
    return result;
}

StructureResult StructureGenerator::generateHalfWall(const glm::ivec3& pos, Facing facing,
                                                      int length, const std::string& material) {
    // Bottom half of a wall using subcubes (subcubeY = 0 and 1 filled, 2 empty).
    StructureResult result;
    int w = length, d = 1;

    for (int x = 0; x < length; ++x) {
        for (int sy = 0; sy < 2; ++sy) {
            for (int sz = 0; sz < 3; ++sz) {
                for (int sx = 0; sx < 3; ++sx) {
                    VoxelPlacement vp;
                    vp.position = glm::ivec3(x, 0, 0);
                    vp.material = material;
                    vp.level = VoxelLevel::Subcube;
                    vp.subcubePos = glm::ivec3(sx, sy, sz);
                    result.voxels.push_back(vp);
                }
            }
        }
    }

    transformResult(result, pos, facing, w, d);
    return result;
}

StructureResult StructureGenerator::generatePitchedRoof(const glm::ivec3& pos, Facing facing,
                                                         int width, int depth,
                                                         const std::string& material) {
    // Gable roof along local X. Ridge at center Z, slopes down to edges.
    // Uses subcubes for smooth stepping: 3 subcube steps per cube height.
    StructureResult result;

    int halfDepth = depth / 2;
    // Each row moving from the edge toward the center rises 1 subcube
    // So total rise = halfDepth * 1 subcube = halfDepth/3 cubes worth of height

    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < depth; ++z) {
            // Distance from nearest edge
            int distFromEdge = std::min(z, depth - 1 - z);
            // subcube rise = distFromEdge steps
            int cubeY = distFromEdge / 3;
            int subcubeY = distFromEdge % 3;

            // Place a subcube slab (all 3 X subcubes, single Z row)
            for (int sx = 0; sx < 3; ++sx) {
                VoxelPlacement vp;
                vp.position = glm::ivec3(x, cubeY, z);
                vp.material = material;
                vp.level = VoxelLevel::Subcube;
                vp.subcubePos = glm::ivec3(sx, subcubeY, 1);
                result.voxels.push_back(vp);
            }
        }
    }

    transformResult(result, pos, facing, width, depth);
    return result;
}

// ============================================================================
// Furniture
// ============================================================================

StructureResult StructureGenerator::generateTable(const glm::ivec3& pos, Facing facing,
                                                   const std::string& material) {
    // 3x1x2 table top at Y=1, with 4 corner legs at Y=0
    // Local coords: X=[0..2], Z=[0..1]
    StructureResult result;
    int w = 3, d = 2;

    // Legs (4 corners at Y=0)
    result.voxels.push_back({{0, 0, 0}, material});
    result.voxels.push_back({{2, 0, 0}, material});
    result.voxels.push_back({{0, 0, 1}, material});
    result.voxels.push_back({{2, 0, 1}, material});

    // Top (Y=1)
    for (int x = 0; x < 3; ++x) {
        for (int z = 0; z < 2; ++z) {
            result.voxels.push_back({{x, 1, z}, material});
        }
    }

    transformResult(result, pos, facing, w, d);
    return result;
}

StructureResult StructureGenerator::generateChair(const glm::ivec3& pos, Facing facing,
                                                   const std::string& material) {
    // 1x1 seat at Y=1 + back at Y=2 on -Z side
    // Local: seat at (0,1,0), back at (0,2,0), legs at corners
    StructureResult result;
    int w = 1, d = 1;

    result.voxels.push_back({{0, 0, 0}, material}); // leg
    result.voxels.push_back({{0, 1, 0}, material}); // seat
    result.voxels.push_back({{0, 2, 0}, material}); // back

    transformResult(result, pos, facing, w, d);
    return result;
}

StructureResult StructureGenerator::generateCounter(const glm::ivec3& pos, Facing facing,
                                                     int length, const std::string& material) {
    // Counter: length blocks along local X, 2 high (base + top), 1 deep
    StructureResult result;
    int w = length, d = 1;

    for (int x = 0; x < length; ++x) {
        result.voxels.push_back({{x, 0, 0}, material}); // base
        result.voxels.push_back({{x, 1, 0}, material}); // top
    }

    transformResult(result, pos, facing, w, d);
    return result;
}

StructureResult StructureGenerator::generateBed(const glm::ivec3& pos, Facing facing,
                                                 const std::string& material) {
    // 2 wide, 3 long, 1 high (Y=0 base)
    StructureResult result;
    int w = 2, d = 3;

    for (int x = 0; x < 2; ++x) {
        for (int z = 0; z < 3; ++z) {
            result.voxels.push_back({{x, 0, z}, material});
        }
    }
    // Headboard at Z=0, Y=1
    result.voxels.push_back({{0, 1, 0}, material});
    result.voxels.push_back({{1, 1, 0}, material});

    transformResult(result, pos, facing, w, d);
    return result;
}

// ============================================================================
// Composites
// ============================================================================

StructureResult StructureGenerator::generateHouse(const glm::ivec3& pos, int width, int depth, int height,
                                                   const MaterialPalette& materials, Facing facing,
                                                   int windows, bool furnished,
                                                   DetailLevel detail) {
    // Build in local coords (door on -Z side = south when facing South)
    StructureResult result;

    // Floor
    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < depth; ++z) {
            result.voxels.push_back({{x, 0, z}, materials.floor});
        }
    }

    // Walls (Y=1 to Y=height-1, leaving interior hollow)
    for (int y = 1; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int z = 0; z < depth; ++z) {
                bool isWall = (x == 0 || x == width - 1 || z == 0 || z == depth - 1);
                if (isWall) {
                    result.voxels.push_back({{x, y, z}, materials.wall});
                }
            }
        }
    }

    // Roof (flat)
    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < depth; ++z) {
            result.voxels.push_back({{x, height, z}, materials.roof});
        }
    }

    // Collect wall voxels to remove for door and windows (before rotation)
    std::vector<glm::ivec3> removals;

    // Door: centered on -Z wall (z=0), Y=1..3
    {
        int doorX = width / 2;
        int doorW = 2;
        int doorH = 3;
        int startX = doorX - doorW / 2;
        for (int dx = 0; dx < doorW && startX + dx < width - 1; ++dx) {
            for (int dy = 1; dy <= doorH && dy < height; ++dy) {
                // Only remove actual wall voxels (z=0 face)
                if (startX + dx > 0) {
                    removals.push_back({startX + dx, dy, 0});
                }
            }
        }
    }

    // Windows: evenly spaced on side walls (+X and -X)
    if (windows > 0 && depth >= 4 && height >= 4) {
        int winY = 2; // window sill height
        int winW = 2, winH = 2;
        int spacing = std::max(1, (depth - 2) / (windows + 1));

        for (int i = 0; i < windows; ++i) {
            int winZ = spacing * (i + 1);
            if (winZ + winW > depth - 1) break;

            // +X wall (x = width-1)
            for (int dz = 0; dz < winW; ++dz) {
                for (int dy = 0; dy < winH; ++dy) {
                    removals.push_back({width - 1, winY + dy, winZ + dz});
                }
            }
            // -X wall (x = 0)
            for (int dz = 0; dz < winW; ++dz) {
                for (int dy = 0; dy < winH; ++dy) {
                    removals.push_back({0, winY + dy, winZ + dz});
                }
            }
        }
    }

    // Remove door/window voxels from the result
    for (const auto& rem : removals) {
        result.voxels.erase(
            std::remove_if(result.voxels.begin(), result.voxels.end(),
                           [&rem](const VoxelPlacement& v) { return v.position == rem; }),
            result.voxels.end());
    }

    // Furniture
    if (furnished && width >= 6 && depth >= 6) {
        // Table + chairs in the center area
        auto table = generateTable({width / 2 - 1, 1, depth / 2}, Facing::North, materials.furniture);
        result.voxels.insert(result.voxels.end(), table.voxels.begin(), table.voxels.end());

        // Chair on one side
        if (width >= 7) {
            auto chair = generateChair({width / 2 - 2, 1, depth / 2}, Facing::East, materials.furniture);
            result.voxels.insert(result.voxels.end(), chair.voxels.begin(), chair.voxels.end());
        }

        // Bed in back corner
        auto bed = generateBed({1, 1, depth - 4}, Facing::North, materials.furniture);
        result.voxels.insert(result.voxels.end(), bed.voxels.begin(), bed.voxels.end());
    }

    // -- Detail pass (subcube trim) --
    if (detail >= DetailLevel::Detailed) {
        std::string trimMat = materials.furniture; // Use furniture material for trim

        // Door frame on -Z wall
        {
            int doorX = width / 2;
            int doorW = 2;
            int startX = doorX - doorW / 2;
            if (startX > 0) {
                auto frame = generateDoorFrame({startX, 1, 0}, Facing::North, doorW, 3, trimMat);
                result.voxels.insert(result.voxels.end(), frame.voxels.begin(), frame.voxels.end());
            }
        }

        // Window frames on side walls
        if (windows > 0 && depth >= 4 && height >= 4) {
            int winY = 2;
            int winW = 2, winH = 2;
            int spacing = std::max(1, (depth - 2) / (windows + 1));

            for (int i = 0; i < windows; ++i) {
                int winZ = spacing * (i + 1);
                if (winZ + winW > depth - 1) break;

                // +X wall window frame
                auto frameR = generateWindowFrame({0, winY, winZ}, Facing::East, winW, winH, trimMat);
                result.voxels.insert(result.voxels.end(), frameR.voxels.begin(), frameR.voxels.end());

                // -X wall window frame
                auto frameL = generateWindowFrame({0, winY, winZ}, Facing::West, winW, winH, trimMat);
                result.voxels.insert(result.voxels.end(), frameL.voxels.begin(), frameL.voxels.end());
            }
        }

        // Pitched roof over flat roof
        if (width >= 4 && depth >= 4) {
            auto roof = generatePitchedRoof({0, height + 1, 0}, Facing::North, width, depth, materials.roof);
            result.voxels.insert(result.voxels.end(), roof.voxels.begin(), roof.voxels.end());
        }
    }

    // Location marker: center of the house interior
    result.locations.push_back({
        "", "House",
        glm::vec3(width / 2.0f, 1.0f, depth / 2.0f),
        std::max(width, depth) / 2.0f,
        LocationType::Home
    });

    // Apply rotation and world offset
    transformResult(result, pos, facing, width, depth);
    return result;
}

StructureResult StructureGenerator::generateTavern(const glm::ivec3& pos, int width, int depth, int stories,
                                                    const MaterialPalette& materials, Facing facing,
                                                    bool furnished,
                                                    DetailLevel detail) {
    StructureResult result;
    int storyHeight = 5; // 4 interior + 1 ceiling/floor
    int totalHeight = stories * storyHeight + 1; // +1 for final roof

    // -- Ground floor shell --
    // Floor
    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < depth; ++z) {
            result.voxels.push_back({{x, 0, z}, materials.floor});
        }
    }

    // All story walls + intermediate floors
    for (int story = 0; story < stories; ++story) {
        int baseY = story * storyHeight;

        // Walls for this story (Y = baseY+1 to baseY+storyHeight-1)
        for (int y = baseY + 1; y < baseY + storyHeight; ++y) {
            for (int x = 0; x < width; ++x) {
                for (int z = 0; z < depth; ++z) {
                    bool isWall = (x == 0 || x == width - 1 || z == 0 || z == depth - 1);
                    if (isWall) {
                        result.voxels.push_back({{x, y, z}, materials.wall});
                    }
                }
            }
        }

        // Intermediate floor (if not ground floor)
        if (story > 0) {
            for (int x = 0; x < width; ++x) {
                for (int z = 0; z < depth; ++z) {
                    result.voxels.push_back({{x, baseY, z}, materials.floor});
                }
            }
            // Leave a stairwell opening (3x3 at back-right corner)
            // Remove floor voxels at the stairwell
            int swX = width - 4, swZ = depth - 4;
            result.voxels.erase(
                std::remove_if(result.voxels.begin(), result.voxels.end(),
                    [baseY, swX, swZ](const VoxelPlacement& v) {
                        return v.position.y == baseY &&
                               v.position.x >= swX && v.position.x < swX + 3 &&
                               v.position.z >= swZ && v.position.z < swZ + 3;
                    }),
                result.voxels.end());
        }
    }

    // Final roof
    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < depth; ++z) {
            result.voxels.push_back({{x, totalHeight - 1, z}, materials.roof});
        }
    }

    // Collect removals for door and windows
    std::vector<glm::ivec3> removals;

    // Front door: centered on -Z wall (z=0)
    {
        int doorX = width / 2;
        int doorW = 2, doorH = 3;
        int startX = doorX - doorW / 2;
        for (int dx = 0; dx < doorW; ++dx) {
            for (int dy = 1; dy <= doorH; ++dy) {
                if (startX + dx > 0 && startX + dx < width - 1) {
                    removals.push_back({startX + dx, dy, 0});
                }
            }
        }
    }

    // Windows on ground floor: side walls
    if (depth >= 6) {
        int winY = 2, winW = 2, winH = 2;
        // Two windows on each side wall
        int positions[] = {depth / 4, 3 * depth / 4 - 1};
        for (int winZ : positions) {
            if (winZ < 1 || winZ + winW > depth - 1) continue;
            // +X wall
            for (int dz = 0; dz < winW; ++dz) {
                for (int dy = 0; dy < winH; ++dy) {
                    removals.push_back({width - 1, winY + dy, winZ + dz});
                }
            }
            // -X wall
            for (int dz = 0; dz < winW; ++dz) {
                for (int dy = 0; dy < winH; ++dy) {
                    removals.push_back({0, winY + dy, winZ + dz});
                }
            }
        }
    }

    // Remove door/window voxels
    for (const auto& rem : removals) {
        result.voxels.erase(
            std::remove_if(result.voxels.begin(), result.voxels.end(),
                           [&rem](const VoxelPlacement& v) { return v.position == rem; }),
            result.voxels.end());
    }

    // -- Furniture (ground floor) --
    if (furnished && width >= 8 && depth >= 8) {
        // Bar counter along back wall (+Z side)
        int counterLen = std::min(width - 4, 8);
        auto counter = generateCounter({2, 1, depth - 3}, Facing::North, counterLen, materials.furniture);
        result.voxels.insert(result.voxels.end(), counter.voxels.begin(), counter.voxels.end());

        // Tables in the main hall (2-3 tables depending on size)
        int numTables = std::min(3, (width - 4) / 4);
        for (int t = 0; t < numTables; ++t) {
            int tx = 2 + t * 4;
            auto table = generateTable({tx, 1, depth / 3}, Facing::North, materials.furniture);
            result.voxels.insert(result.voxels.end(), table.voxels.begin(), table.voxels.end());

            // Chair on each side
            auto chair1 = generateChair({tx, 1, depth / 3 - 1}, Facing::South, materials.furniture);
            result.voxels.insert(result.voxels.end(), chair1.voxels.begin(), chair1.voxels.end());
            auto chair2 = generateChair({tx + 2, 1, depth / 3 + 2}, Facing::North, materials.furniture);
            result.voxels.insert(result.voxels.end(), chair2.voxels.begin(), chair2.voxels.end());
        }
    }

    // -- Stairs between floors (subcube resolution for smooth stepping) --
    if (stories > 1) {
        for (int story = 0; story < stories - 1; ++story) {
            int baseY = story * storyHeight + 1;
            int swX = width - 4, swZ = depth - 4;
            auto stairs = generateSubcubeStaircase({swX, baseY, swZ}, Facing::North,
                                                    storyHeight - 1, 2, materials.stairs);
            result.voxels.insert(result.voxels.end(), stairs.voxels.begin(), stairs.voxels.end());
        }

        // Upper floor furniture: beds in rooms
        if (furnished && width >= 8 && depth >= 8) {
            for (int story = 1; story < stories; ++story) {
                int baseY = story * storyHeight + 1;
                // Bed in left corner
                auto bed1 = generateBed({1, baseY, 1}, Facing::South, materials.furniture);
                result.voxels.insert(result.voxels.end(), bed1.voxels.begin(), bed1.voxels.end());
                // Bed in right area (away from stairwell)
                if (width >= 10) {
                    auto bed2 = generateBed({1, baseY, depth / 2}, Facing::South, materials.furniture);
                    result.voxels.insert(result.voxels.end(), bed2.voxels.begin(), bed2.voxels.end());
                }
            }
        }
    }

    // -- Detail pass (subcube trim) --
    if (detail >= DetailLevel::Detailed) {
        std::string trimMat = materials.furniture;

        // Door frame on front entrance
        {
            int doorX = width / 2;
            int doorW = 2;
            int startX = doorX - doorW / 2;
            if (startX > 0 && startX < width - 1) {
                auto frame = generateDoorFrame({startX, 1, 0}, Facing::North, doorW, 3, trimMat);
                result.voxels.insert(result.voxels.end(), frame.voxels.begin(), frame.voxels.end());
            }
        }

        // Window frames on ground-floor side walls
        if (depth >= 6) {
            int winY = 2, winW = 2, winH = 2;
            int positions[] = {depth / 4, 3 * depth / 4 - 1};
            for (int winZ : positions) {
                if (winZ < 1 || winZ + winW > depth - 1) continue;
                auto frameR = generateWindowFrame({0, winY, winZ}, Facing::East, winW, winH, trimMat);
                result.voxels.insert(result.voxels.end(), frameR.voxels.begin(), frameR.voxels.end());
                auto frameL = generateWindowFrame({0, winY, winZ}, Facing::West, winW, winH, trimMat);
                result.voxels.insert(result.voxels.end(), frameL.voxels.begin(), frameL.voxels.end());
            }
        }

        // Railing on upper-floor stairwell opening
        if (stories > 1 && width >= 8) {
            for (int story = 1; story < stories; ++story) {
                int baseY = story * storyHeight;
                int swX = width - 4;
                int swZ = depth - 4;
                // Railing along the open edge of the stairwell (facing into room)
                auto rail = generateRailing({swX, baseY + 1, swZ - 1}, Facing::North, 3, trimMat);
                result.voxels.insert(result.voxels.end(), rail.voxels.begin(), rail.voxels.end());
            }
        }

        // Pitched roof
        if (width >= 4 && depth >= 4) {
            auto roof = generatePitchedRoof({0, totalHeight, 0}, Facing::North, width, depth, materials.roof);
            result.voxels.insert(result.voxels.end(), roof.voxels.begin(), roof.voxels.end());
        }
    }

    // Location markers
    result.locations.push_back({
        "", "Tavern",
        glm::vec3(width / 2.0f, 1.0f, depth / 2.0f),
        std::max(width, depth) / 2.0f,
        LocationType::Tavern
    });

    // Apply rotation and world offset
    transformResult(result, pos, facing, width, depth);
    return result;
}

StructureResult StructureGenerator::generateWallSegment(const glm::ivec3& start, const glm::ivec3& end,
                                                         int height, const std::string& material,
                                                         int thickness) {
    StructureResult result;

    // Bresenham-style line in XZ plane, extruded by height and thickness
    int dx = end.x - start.x;
    int dz = end.z - start.z;
    int steps = std::max(std::abs(dx), std::abs(dz));
    if (steps == 0) steps = 1;

    for (int i = 0; i <= steps; ++i) {
        int x = start.x + dx * i / steps;
        int z = start.z + dz * i / steps;

        for (int y = start.y; y < start.y + height; ++y) {
            for (int t = 0; t < thickness; ++t) {
                // Thicken perpendicular to the wall direction
                if (std::abs(dx) >= std::abs(dz)) {
                    result.voxels.push_back({{x, y, z + t}, material});
                } else {
                    result.voxels.push_back({{x + t, y, z}, material});
                }
            }
        }
    }
    return result;
}

StructureResult StructureGenerator::generateTower(const glm::ivec3& pos, int radius, int height,
                                                   const std::string& material, Facing facing,
                                                   DetailLevel detail) {
    StructureResult result;
    int diameter = radius * 2 + 1;

    // Cylindrical shell
    for (int y = 0; y < height; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                float dist = std::sqrt(static_cast<float>(x * x + z * z));
                bool isShell = (dist <= radius && dist > radius - 1.5f);
                bool isFloor = (y == 0 && dist <= radius);
                bool isRoof  = (y == height - 1 && dist <= radius);

                if (isShell || isFloor || isRoof) {
                    result.voxels.push_back({pos + glm::ivec3(x + radius, y, z + radius), material});
                }
            }
        }
    }

    // Door opening (on the facing side)
    std::vector<glm::ivec3> doorRemovals;
    int doorH = 3;
    glm::ivec3 doorBase;

    switch (facing) {
        case Facing::South: doorBase = pos + glm::ivec3(radius, 1, 0); break;
        case Facing::North: doorBase = pos + glm::ivec3(radius, 1, diameter - 1); break;
        case Facing::East:  doorBase = pos + glm::ivec3(diameter - 1, 1, radius); break;
        case Facing::West:  doorBase = pos + glm::ivec3(0, 1, radius); break;
    }
    for (int dy = 0; dy < doorH && dy + 1 < height - 1; ++dy) {
        doorRemovals.push_back(doorBase + glm::ivec3(0, dy, 0));
    }

    // Remove door voxels
    for (const auto& rem : doorRemovals) {
        result.voxels.erase(
            std::remove_if(result.voxels.begin(), result.voxels.end(),
                           [&rem](const VoxelPlacement& v) { return v.position == rem; }),
            result.voxels.end());
    }

    // Internal staircase (spiral approximation)
    int stairY = 1;
    float angle = 0.0f;
    float stairRadius = radius - 2.0f;
    if (stairRadius < 1.0f) stairRadius = 1.0f;

    while (stairY < height - 1) {
        int sx = static_cast<int>(std::round(stairRadius * std::cos(angle))) + radius;
        int sz = static_cast<int>(std::round(stairRadius * std::sin(angle))) + radius;
        result.voxels.push_back({pos + glm::ivec3(sx, stairY, sz), material});
        // Add neighbor for wider step
        int sx2 = static_cast<int>(std::round((stairRadius - 1.0f) * std::cos(angle))) + radius;
        int sz2 = static_cast<int>(std::round((stairRadius - 1.0f) * std::sin(angle))) + radius;
        if (sx2 != sx || sz2 != sz) {
            result.voxels.push_back({pos + glm::ivec3(sx2, stairY, sz2), material});
        }

        stairY++;
        angle += 0.5f; // ~30 degrees per step
    }

    // -- Detail pass --
    if (detail >= DetailLevel::Detailed) {
        // Door frame
        auto frame = generateDoorFrame(doorBase, facing, 1, doorH, "Wood");
        result.voxels.insert(result.voxels.end(), frame.voxels.begin(), frame.voxels.end());

        // Parapet / half-wall at the top of the tower
        if (radius >= 2) {
            for (int x = -radius; x <= radius; ++x) {
                for (int z = -radius; z <= radius; ++z) {
                    float dist = std::sqrt(static_cast<float>(x * x + z * z));
                    if (dist <= radius && dist > radius - 1.5f) {
                        auto hw = generateHalfWall(pos + glm::ivec3(x + radius, height, z + radius),
                                                    Facing::North, 1, material);
                        result.voxels.insert(result.voxels.end(), hw.voxels.begin(), hw.voxels.end());
                    }
                }
            }
        }
    }

    // Location marker
    result.locations.push_back({
        "", "Tower",
        glm::vec3(pos) + glm::vec3(radius, 1.0f, radius),
        static_cast<float>(radius),
        LocationType::GuardPost
    });

    return result;
}

// ============================================================================
// JSON-driven generation
// ============================================================================

StructureResult StructureGenerator::generateFromJson(const nlohmann::json& def) {
    std::string type = def.value("type", "");

    glm::ivec3 pos(0);
    if (def.contains("position")) {
        pos.x = def["position"].value("x", 0);
        pos.y = def["position"].value("y", 0);
        pos.z = def["position"].value("z", 0);
    }

    Facing facing = facingFromString(def.value("facing", "south"));
    MaterialPalette materials;
    if (def.contains("materials")) {
        materials = MaterialPalette::fromJson(def["materials"]);
    }
    std::string material = def.value("material", "Stone");
    bool furnished = def.value("furnished", true);
    DetailLevel detail = detailLevelFromString(def.value("detail_level", "detailed"));

    if (type == "box") {
        int w = def.value("width", 5);
        int h = def.value("height", 5);
        int d = def.value("depth", 5);
        bool hollow = def.value("hollow", false);
        return generateBox(pos, w, h, d, material, hollow);

    } else if (type == "room") {
        int w = def.value("width", 6);
        int h = def.value("height", 4);
        int d = def.value("depth", 6);
        return generateRoom(pos, w, h, d, materials);

    } else if (type == "house") {
        int w = def.value("width", 8);
        int d = def.value("depth", 10);
        int h = def.value("height", 5);
        int windows = def.value("windows", 2);
        return generateHouse(pos, w, d, h, materials, facing, windows, furnished, detail);

    } else if (type == "tavern") {
        int w = def.value("width", 14);
        int d = def.value("depth", 18);
        int stories = def.value("stories", 2);
        return generateTavern(pos, w, d, stories, materials, facing, furnished, detail);

    } else if (type == "tower") {
        int r = def.value("radius", 4);
        int h = def.value("height", 12);
        return generateTower(pos, r, h, material, facing, detail);

    } else if (type == "wall") {
        glm::ivec3 end(0);
        if (def.contains("end")) {
            end.x = def["end"].value("x", 0);
            end.y = def["end"].value("y", 0);
            end.z = def["end"].value("z", 0);
        }
        int h = def.value("height", 4);
        int thickness = def.value("thickness", 1);
        return generateWallSegment(pos, end, h, material, thickness);

    } else if (type == "staircase") {
        int h = def.value("height", 5);
        int w = def.value("width", 2);
        return generateStaircase(pos, facing, h, w, material);

    } else if (type == "subcube_staircase") {
        int h = def.value("height", 5);
        int w = def.value("width", 2);
        return generateSubcubeStaircase(pos, facing, h, w, material);

    } else if (type == "table") {
        return generateTable(pos, facing, material);

    } else if (type == "chair") {
        return generateChair(pos, facing, material);

    } else if (type == "counter") {
        int length = def.value("length", 4);
        return generateCounter(pos, facing, length, material);

    } else if (type == "bed") {
        return generateBed(pos, facing, material);

    } else if (type == "window_frame") {
        int w = def.value("width", 2);
        int h = def.value("height", 2);
        return generateWindowFrame(pos, facing, w, h, material);

    } else if (type == "door_frame") {
        int w = def.value("width", 2);
        int h = def.value("height", 3);
        return generateDoorFrame(pos, facing, w, h, material);

    } else if (type == "railing") {
        int length = def.value("length", 4);
        return generateRailing(pos, facing, length, material);

    } else if (type == "half_wall") {
        int length = def.value("length", 4);
        return generateHalfWall(pos, facing, length, material);

    } else if (type == "pitched_roof") {
        int w = def.value("width", 8);
        int d = def.value("depth", 10);
        return generatePitchedRoof(pos, facing, w, d, material);
    }

    LOG_WARN("StructureGenerator", "Unknown structure type: " + type);
    return {};
}

nlohmann::json StructureGenerator::getStructureTypes() {
    using json = nlohmann::json;
    json types = json::array();

    types.push_back({
        {"type", "box"}, {"description", "Solid or hollow box"},
        {"params", {{"position", "ivec3"}, {"width", "int(5)"}, {"height", "int(5)"},
                    {"depth", "int(5)"}, {"material", "string(Stone)"}, {"hollow", "bool(false)"}}}
    });
    types.push_back({
        {"type", "room"}, {"description", "Enclosed room with floor, walls, and ceiling"},
        {"params", {{"position", "ivec3"}, {"width", "int(6)"}, {"height", "int(4)"},
                    {"depth", "int(6)"}, {"materials", "MaterialPalette"}}}
    });
    types.push_back({
        {"type", "house"}, {"description", "Simple house with door, windows, and optional furniture"},
        {"params", {{"position", "ivec3"}, {"width", "int(8)"}, {"depth", "int(10)"},
                    {"height", "int(5)"}, {"materials", "MaterialPalette"},
                    {"facing", "north|east|south|west"}, {"windows", "int(2)"}, {"furnished", "bool(true)"},
                    {"detail_level", "rough|detailed|fine (default: detailed)"}}}
    });
    types.push_back({
        {"type", "tavern"}, {"description", "Multi-story tavern with bar, tables, and upstairs rooms"},
        {"params", {{"position", "ivec3"}, {"width", "int(14)"}, {"depth", "int(18)"},
                    {"stories", "int(2)"}, {"materials", "MaterialPalette"},
                    {"facing", "north|east|south|west"}, {"furnished", "bool(true)"},
                    {"detail_level", "rough|detailed|fine (default: detailed)"}}}
    });
    types.push_back({
        {"type", "tower"}, {"description", "Cylindrical tower with door and spiral staircase"},
        {"params", {{"position", "ivec3"}, {"radius", "int(4)"}, {"height", "int(12)"},
                    {"material", "string(Stone)"}, {"facing", "north|east|south|west"},
                    {"detail_level", "rough|detailed|fine (default: detailed)"}}}
    });
    types.push_back({
        {"type", "wall"}, {"description", "Freestanding wall between two points"},
        {"params", {{"position", "ivec3 (start)"}, {"end", "ivec3"}, {"height", "int(4)"},
                    {"material", "string(Stone)"}, {"thickness", "int(1)"}}}
    });
    types.push_back({
        {"type", "staircase"}, {"description", "Ascending staircase"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"height", "int(5)"},
                    {"width", "int(2)"}, {"material", "string(Stone)"}}}
    });
    types.push_back({
        {"type", "subcube_staircase"}, {"description", "Subcube-resolution staircase (3 steps per block height)"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"height", "int(5)"},
                    {"width", "int(2)"}, {"material", "string(Stone)"}}}
    });
    types.push_back({
        {"type", "table"}, {"description", "3x2 table with legs"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"material", "string(Wood)"}}}
    });
    types.push_back({
        {"type", "chair"}, {"description", "Single chair with back"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"material", "string(Wood)"}}}
    });
    types.push_back({
        {"type", "counter"}, {"description", "Bar/shop counter"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"length", "int(4)"},
                    {"material", "string(Wood)"}}}
    });
    types.push_back({
        {"type", "bed"}, {"description", "2x3 bed with headboard"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"material", "string(Wood)"}}}
    });
    types.push_back({
        {"type", "window_frame"}, {"description", "Subcube window frame surround"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"width", "int(2)"},
                    {"height", "int(2)"}, {"material", "string(Wood)"}}}
    });
    types.push_back({
        {"type", "door_frame"}, {"description", "Subcube door frame surround"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"width", "int(2)"},
                    {"height", "int(3)"}, {"material", "string(Wood)"}}}
    });
    types.push_back({
        {"type", "railing"}, {"description", "Subcube railing / bannister"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"length", "int(4)"},
                    {"material", "string(Wood)"}}}
    });
    types.push_back({
        {"type", "half_wall"}, {"description", "Half-height wall using subcubes"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"length", "int(4)"},
                    {"material", "string(Stone)"}}}
    });
    types.push_back({
        {"type", "pitched_roof"}, {"description", "Pitched roof using subcubes (gable shape)"},
        {"params", {{"position", "ivec3"}, {"facing", "direction"}, {"width", "int(8)"},
                    {"depth", "int(10)"}, {"material", "string(Wood)"}}}
    });

    return types;
}

// ============================================================================
// Placement
// ============================================================================

PlacementResult StructureGenerator::place(ChunkManager* chunkManager, const StructureResult& structure) {
    PlacementResult result;
    result.locations = structure.locations;

    if (!chunkManager) {
        result.failed = static_cast<int>(structure.voxels.size());
        return result;
    }

    if (structure.voxels.size() > 100000) {
        LOG_WARN("StructureGenerator", "Structure has " + std::to_string(structure.voxels.size()) +
                 " voxels, exceeds 100k limit");
        result.failed = static_cast<int>(structure.voxels.size());
        return result;
    }

    for (const auto& voxel : structure.voxels) {
        bool ok = false;
        switch (voxel.level) {
        case VoxelLevel::Subcube:
            ok = chunkManager->m_voxelModificationSystem.addSubcubeWithMaterial(
                voxel.position, voxel.subcubePos, voxel.material.empty() ? "Default" : voxel.material);
            break;
        case VoxelLevel::Microcube:
            ok = chunkManager->m_voxelModificationSystem.addMicrocubeWithMaterial(
                voxel.position, voxel.subcubePos, voxel.microcubePos,
                voxel.material.empty() ? "Default" : voxel.material);
            break;
        default: // Cube
            if (!voxel.material.empty()) {
                ok = chunkManager->m_voxelModificationSystem.addCubeWithMaterial(voxel.position, voxel.material);
            } else {
                ok = chunkManager->addCube(voxel.position);
            }
            break;
        }
        if (ok) result.placed++;
        else result.failed++;
    }

    return result;
}

int StructureGenerator::removeVoxels(ChunkManager* chunkManager, const std::vector<glm::ivec3>& positions) {
    if (!chunkManager) return 0;

    int removed = 0;
    for (const auto& pos : positions) {
        if (chunkManager->removeCube(pos)) {
            removed++;
        }
    }
    return removed;
}

} // namespace Core
} // namespace Phyxel
