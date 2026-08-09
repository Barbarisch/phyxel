#include "core/StructureGenerator.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "utils/CoordinateUtils.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace Phyxel {
namespace Core {

namespace {
/// Ordering so local voxel cells can live in a std::set (dedup of shared walls).
struct IVec3Less {
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    }
};
using CellSet = std::set<glm::ivec3, IVec3Less>;

/// Edge-adjacency between two XZ footprints rect = (x, z, w, d).
struct SharedWall {
    bool ok = false;
    char axis = 'x';   ///< 'x' = vertical wall at x=coord running along z; 'z' = horizontal at z=coord
    int  coord = 0, lo = 0, hi = 0;
};

SharedWall sharedWall(const glm::ivec4& a, const glm::ivec4& b) {
    const int ax0 = a.x, az0 = a.y, ax1 = a.x + a.z, az1 = a.y + a.w;
    const int bx0 = b.x, bz0 = b.y, bx1 = b.x + b.z, bz1 = b.y + b.w;
    SharedWall r;
    if (ax1 == bx0 || bx1 == ax0) {
        r.coord = (ax1 == bx0) ? ax1 : ax0;
        r.lo = std::max(az0, bz0);
        r.hi = std::min(az1, bz1);
        if (r.hi - r.lo > 0) { r.ok = true; r.axis = 'x'; return r; }
    }
    if (az1 == bz0 || bz1 == az0) {
        r.coord = (az1 == bz0) ? az1 : az0;
        r.lo = std::max(ax0, bx0);
        r.hi = std::min(ax1, bx1);
        if (r.hi - r.lo > 0) { r.ok = true; r.axis = 'z'; return r; }
    }
    r.ok = false;
    return r;
}
} // namespace

// ============================================================================
// MaterialPalette
// ============================================================================

MaterialPalette MaterialPalette::fromJson(const nlohmann::json& j) {
    MaterialPalette p;
    if (j.is_object()) {
        p.wall      = j.value("wall", "Stone");
        p.floor     = j.value("floor", "Wood");
        p.roof      = j.value("roof", "WoodShingle");
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
    // Each z-row toward the ridge rises 1 subcube; every cell is a SOLID wedge column
    // (full cubes below + a partial subcube layer on top), so rows share faces and the
    // roof reads as one continuous slope — never detached floating strips.
    StructureResult result;

    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < depth; ++z) {
            // Surface height in subcube steps above the eave (0 at the edges).
            int distFromEdge = std::min(z, depth - 1 - z);
            int fullCubes = distFromEdge / 3;
            int topSubY   = distFromEdge % 3;

            for (int cy = 0; cy < fullCubes; ++cy) {
                VoxelPlacement vp;
                vp.position = glm::ivec3(x, cy, z);
                vp.material = material;
                vp.level = VoxelLevel::Cube;
                result.voxels.push_back(vp);
            }
            for (int sy = 0; sy <= topSubY; ++sy) {
                for (int sx = 0; sx < 3; ++sx) {
                    for (int sz = 0; sz < 3; ++sz) {
                        VoxelPlacement vp;
                        vp.position = glm::ivec3(x, fullCubes, z);
                        vp.material = material;
                        vp.level = VoxelLevel::Subcube;
                        vp.subcubePos = glm::ivec3(sx, sy, sz);
                        result.voxels.push_back(vp);
                    }
                }
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
// Wall segment (the composites — house/tavern/tower/BuildingSpec — were REMOVED;
// generated buildings come from StructureBuildService / Structure Generation v2)
// ============================================================================

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
        {"type", "house"}, {"description", "Generated dwelling — ALIASES to Structure Generation v2 "
                                           "(croft/hall_house typology by footprint; style-driven materials)"},
        {"params", {{"position", "ivec3"}, {"width", "int(7)"}, {"depth", "int(6)"},
                    {"stories", "int(1)"}, {"typology", "string(croft|hall_house|longhouse...)"},
                    {"style", "string(timber_cottage)"}}}
    });
    types.push_back({
        {"type", "tavern"}, {"description", "Generated tavern — ALIASES to Structure Generation v2 "
                                            "(tavern typology: taproom/kitchen/service, engine-furnished)"},
        {"params", {{"position", "ivec3"}, {"width", "int(8)"}, {"depth", "int(7)"},
                    {"stories", "int(1)"}, {"style", "string(timber_cottage)"}}}
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

    // Build the structure up in bounded steps instead of all-or-nothing. A large
    // structure (e.g. a multi-story tower) legitimately runs to hundreds of thousands
    // of micro-voxels; the old hard 100k cap silently dropped the WHOLE structure,
    // leaving a wall-less "ghost" (a registered bbox + furniture with no voxels). We
    // now place every voxel in steps and report the true placed/failed counts. Runaway
    // protection belongs upstream in BuildingProgramValidator (footprint/story gates),
    // not in a silent placement drop here.
    const size_t total = structure.voxels.size();
    constexpr size_t kPlacementStep = 25000;   // voxels per step (progress granularity)
    if (total > kPlacementStep) {
        LOG_INFO("StructureGenerator", "Placing " + std::to_string(total) + " voxels in " +
                 std::to_string((total + kPlacementStep - 1) / kPlacementStep) + " steps");
    }

    // BATCH placement: write each voxel STRAIGHT to its chunk, bypassing the per-voxel mod-system
    // wrappers. Those wrappers run, per call, a discarded LOG_DEBUG_FMT (builds an ostringstream) +
    // FaceUpdateCoordinator::updateAfterCubePlace (allocates a vector + a std::set + neighbour
    // lookups). At tens of thousands of voxels that per-voxel STL churn dominated — ~14 s for a 65k-
    // voxel tavern in a Debug build. Here we mark each TOUCHED chunk dirty exactly ONCE at the end
    // (same dirty/neighbour semantics, O(chunks) not O(voxels)); the mesh rebuilds once per chunk at
    // render, so the visual result is identical.
    std::map<Chunk*, glm::ivec3> touched;   // chunk -> a representative world pos (for the single update)
    for (size_t i = 0; i < total; ++i) {
        const auto& voxel = structure.voxels[i];
        // A tall structure spans multiple vertical chunks; materialize the owning chunk first so the
        // structure crosses every vertical seam (KI-3). Existing chunks are a cheap lookup.
        chunkManager->ensureChunkAt(voxel.position);
        Chunk* chunk = chunkManager->getChunkAtFast(voxel.position);
        if (!chunk) { result.failed++; continue; }
        auto ins = touched.emplace(chunk, voxel.position);
        if (ins.second) chunk->beginBulkOperation();   // first touch: defer per-voxel collision
        const glm::ivec3 lp = Utils::CoordinateUtils::worldToLocalCoord(voxel.position);
        // DESTRUCTIVE-WRITE ACCOUNTING: is this cell already occupied by something
        // coarser (or by another fine cell)? Writing over it does not fail — it
        // REPLACES — so without this count the damage is invisible to every caller.
        {
            bool occupied = chunk->getVoxelStore().solid(lp.z + lp.y * 32 + lp.x * 1024);
            if (!occupied && voxel.level == VoxelLevel::Microcube)
                occupied = chunk->getSubcubeAt(lp, voxel.subcubePos) != nullptr ||
                           chunk->getMicrocubeAt(lp, voxel.subcubePos, voxel.microcubePos) != nullptr;
            else if (!occupied && voxel.level == VoxelLevel::Subcube)
                occupied = chunk->getSubcubeAt(lp, voxel.subcubePos) != nullptr;
            if (occupied) {
                ++result.displaced;
                if (result.displacedSample.size() < 32)
                    result.displacedSample.push_back(voxel.position);
            }
        }
        bool ok = false;
        switch (voxel.level) {
        case VoxelLevel::Subcube:
            ok = chunk->addSubcube(lp, voxel.subcubePos, voxel.material.empty() ? "Default" : voxel.material);
            break;
        case VoxelLevel::Microcube:
            ok = chunk->addMicrocube(lp, voxel.subcubePos, voxel.microcubePos,
                                     voxel.material.empty() ? "Default" : voxel.material);
            break;
        default: // Cube
            ok = voxel.material.empty() ? chunk->addCube(lp) : chunk->addCube(lp, voxel.material);
            break;
        }
        if (ok) { result.placed++; touched[chunk] = voxel.position; }
        else result.failed++;

        if (total > kPlacementStep && (i + 1) % kPlacementStep == 0) {
            LOG_INFO("StructureGenerator", "  step " + std::to_string((i + 1) / kPlacementStep) + ": " +
                     std::to_string(i + 1) + "/" + std::to_string(total) + " (" +
                     std::to_string(result.placed) + " placed, " +
                     std::to_string(result.failed) + " failed)");
        }
    }

    // Per touched chunk, ONCE: rebuild all collision shapes (endBulkOperation) + mark faces dirty.
    // Replaces the per-voxel collision add + face update that made a 65k-voxel build freeze ~14 s.
    for (const auto& entry : touched) {
        entry.first->endBulkOperation();
        chunkManager->updateAfterCubePlace(entry.second);
    }

    return result;
}

int StructureGenerator::removeVoxels(ChunkManager* chunkManager, const std::vector<glm::ivec3>& positions) {
    if (!chunkManager) return 0;

    // [no-frozen-engine] bulk-defer per-voxel collision exactly like place(): the naive loop
    // paid collision work per removed cell (measured: excav=1042 ms for one building's cells).
    // Each touched chunk enters bulk mode once; collisions rebuild once per chunk at the end.
    std::map<Chunk*, bool> touched;
    for (const auto& pos : positions)
        if (Chunk* c = chunkManager->getChunkAtFast(pos))
            if (touched.emplace(c, true).second) c->beginBulkOperation();

    int removed = 0;
    for (const auto& pos : positions) {
        if (chunkManager->removeCube(pos)) {
            removed++;
        }
    }

    for (auto& [c, _] : touched) c->endBulkOperation();
    return removed;
}

StructureResult StructureGenerator::planChimneyStack(int cx, int cz, int baseMicroY, int topMicroY,
                                                     const std::string& material, int capRows) {
    StructureResult r;
    if (topMicroY < baseMicroY) return r;
    // FLOOR-divide world micro -> (cube, subcube, microcube) so it's correct at negative world coords.
    auto fdiv = [](int a, int b) { int q = a / b, m = a % b; if (m != 0 && (m < 0) != (b < 0)) --q; return q; };
    auto fmod = [&](int a, int b) { return a - fdiv(a, b) * b; };
    auto emit = [&](int gx, int gy, int gz) {
        VoxelPlacement vp;
        vp.material = material;
        vp.level = VoxelLevel::Microcube;
        vp.position = glm::ivec3(fdiv(gx, 9), fdiv(gy, 9), fdiv(gz, 9));
        const int rx = fmod(gx, 9), ry = fmod(gy, 9), rz = fmod(gz, 9);
        vp.subcubePos   = glm::ivec3(rx / 3, ry / 3, rz / 3);
        vp.microcubePos = glm::ivec3(rx % 3, ry % 3, rz % 3);
        r.voxels.push_back(vp);
    };
    const int half = 2;                              // 5-micro (~0.56 m) square stack: cx-2..cx+2
    const int capBase = topMicroY - std::max(0, capRows) + 1;
    for (int y = baseMicroY; y <= topMicroY; ++y) {
        const bool cap = (y >= capBase);             // solid pot at the top (hides the flue from above)
        for (int dx = -half; dx <= half; ++dx)
            for (int dz = -half; dz <= half; ++dz) {
                const bool ring = (std::abs(dx) == half || std::abs(dz) == half);  // outer wall
                if (cap || ring) emit(cx + dx, y, cz + dz);   // else inner 3x3 = flue void
            }
    }
    return r;
}

int StructureGenerator::planPadLevel(std::vector<int> cellTops) {
    if (cellTops.empty()) return 0;
    std::sort(cellTops.begin(), cellTops.end());
    return cellTops[cellTops.size() / 2];   // median top: minimizes cut+fill, robust to outliers
}

} // namespace Core
} // namespace Phyxel
