#include "core/ObjectTemplateManager.h"
#include "core/WorldGenerator.h"
#include "core/ProceduralTree.h"
#include "core/ChunkManager.h"
#include "core/DynamicObjectManager.h"
#include "core/PlacedObjectManager.h"
#include "core/KinematicVoxelManager.h"
#include "core/KinematicAnimator.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "physics/PhysicsWorld.h"
#include "utils/CoordinateUtils.h"
#include "utils/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <unordered_set>
#include <algorithm>

namespace Phyxel {

namespace fs = std::filesystem;

ObjectTemplateManager::ObjectTemplateManager(ChunkManager* chunkMgr, DynamicObjectManager* dynamicMgr)
    : m_chunkManager(chunkMgr), m_dynamicObjectManager(dynamicMgr) {
}

void ObjectTemplateManager::loadTemplates(const std::string& directoryPath) {
    if (!fs::exists(directoryPath)) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template directory does not exist: " << directoryPath);
        return;
    }

    // RECURSIVE scan over the category taxonomy (furniture/, nature/, items/,
    // weapons/, ...). STEMS are the reference key (world DBs, flora, and
    // FurnitureCatalog store stems), so stems must be UNIQUE across the whole
    // library: a duplicate stem is a loud COLLISION and the second file is
    // skipped — never a silent overwrite (the silent-substitution bug class
    // of 2026-08-06). Subdirectory templates also get a relative-path alias
    // ("items/torch") matching resolveItemTemplate's path-qualified keys.
    size_t loaded = 0, collisions = 0;
    for (const auto& entry : fs::recursive_directory_iterator(directoryPath)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".voxel" && ext != ".txt") continue;
        if (ext == ".txt") {
            LOG_WARN_FMT("ObjectTemplateManager",
                "Template '" << entry.path().filename().string()
                << "' uses legacy .txt extension — rename to .voxel");
        }

        const std::string stem = entry.path().stem().string();
        if (m_templates.count(stem) || m_aliases.count(stem)) {
            LOG_ERROR_FMT("ObjectTemplateManager", "STEM COLLISION: '" << stem
                          << "' already registered — skipping "
                          << entry.path().string()
                          << " (stems must be unique across the template library)");
            ++collisions;
            continue;
        }

        if (loadTemplate(entry.path().string())) {
            ++loaded;
            std::string rel = fs::relative(entry.path(), directoryPath).generic_string();
            if (const auto dot = rel.rfind(ext); dot != std::string::npos) rel.erase(dot);
            if (rel != stem && !m_templates.count(rel)) m_aliases[rel] = stem;
        }
    }
    LOG_INFO_FMT("ObjectTemplateManager", "Template scan: " << loaded << " loaded from "
                 << directoryPath << (collisions ? (", " + std::to_string(collisions)
                 + " STEM COLLISIONS (skipped)") : std::string()));
}

bool ObjectTemplateManager::loadTemplate(const std::string& filePath, const std::string& registryName) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Failed to open template file: " << filePath);
        return false;
    }

    auto tmpl = std::make_unique<VoxelTemplate>();
    tmpl->name = registryName.empty() ? fs::path(filePath).stem().string() : registryName;
    tmpl->sourceFilePath = fs::absolute(filePath).string();

    std::string line;
    while (std::getline(file, line)) {
        parseLine(line, *tmpl);
    }

    // Reject the WHOLE template on a format-contract violation (fine-grid
    // rules) — a half-loaded file registering silently is exactly the class
    // of bug the fine tier's parse contract exists to prevent.
    if (tmpl->parseError) {
        LOG_ERROR_FMT("ObjectTemplateManager", "REJECTED template '" << tmpl->name
                      << "': " << tmpl->parseErrorReason << " (" << filePath << ")");
        return false;
    }

    LOG_INFO_FMT("ObjectTemplateManager", "Loaded template: " << tmpl->name << " with "
                 << tmpl->cubes.size() << " cubes, "
                 << tmpl->subcubes.size() << " subcubes, "
                 << tmpl->microcubes.size() << " microcubes, "
                 << tmpl->fineVoxels.size() << " fine voxels"
                 << (tmpl->isFineGrid()
                         ? " (grid " + std::to_string(tmpl->fineGridResolution) + ", kinematic-only), "
                         : ", ")
                 << tmpl->interactionPoints.size() << " interaction points, "
                 << tmpl->parts.size() << " parts");

    // Grow the flora decoration margin so decorateChunk's planFlora window always reaches a
    // plant rooted this-template-radius away in a neighbor chunk. A fixed margin silently
    // clipped any canopy wider than it at chunk seams (Increment B #1 blocker).
    const int r = templateFootprintRadius(*tmpl);
    if (r > kFloraMarginCap)
        LOG_WARN_FMT("ObjectTemplateManager", "Template '" << tmpl->name << "' half-footprint "
                     << r << " exceeds flora margin cap " << kFloraMarginCap
                     << "; used as flora its canopy beyond " << kFloraMarginCap
                     << " columns will clip at chunk seams");
    m_floraMarginColumns = std::min(kFloraMarginCap, std::max(m_floraMarginColumns, r));

    m_templates[tmpl->name] = std::move(tmpl);
    return true;
}

// Max column overhang of a template from its stamp anchor (decorateChunk centers on
// base = worldPos - maxExtent/2, so a voxel at relative rx sits |rx - maxX/2| columns from the
// trunk column). Returns the larger of the X and Z overhangs — the radius the flora margin must
// cover so no seam clips this template's footprint.
int ObjectTemplateManager::templateFootprintRadius(const VoxelTemplate& t) {
    glm::ivec3 mn(0), mx(0);
    auto acc = [&](const glm::ivec3& p) { mn = glm::min(mn, p); mx = glm::max(mx, p); };
    for (const auto& c : t.cubes)      acc(c.relativePos);
    for (const auto& s : t.subcubes)   acc(s.parentRelativePos);
    for (const auto& m : t.microcubes) acc(m.parentRelativePos);
    const int halfX = mx.x / 2, halfZ = mx.z / 2;   // matches the stamp's maxExtent/2 centering
    const int overX = std::max(mx.x - halfX, halfX - mn.x);
    const int overZ = std::max(mx.z - halfZ, halfZ - mn.z);
    return std::max(overX, overZ);
}

bool ObjectTemplateManager::canBakeStatic(const VoxelTemplate& tmpl) {
    // Fine-grid templates are kinematic-only: the chunk store bottoms out at
    // the 9-per-cube micro grid, which cannot represent 1/27 or 1/81 cells.
    // Refuse loudly — never silently downsample (spawn as a prop instead).
    return !tmpl.isFineGrid();
}

float ObjectTemplateManager::getTemplateFacingYaw(const std::string& name) const {
    auto it = m_templates.find(name);
    if (it == m_templates.end()) return 0.0f;
    return it->second->facingYaw;
}

void ObjectTemplateManager::parseLine(const std::string& line, VoxelTemplate& tmpl) {
    if (line.empty()) return;

    // Records a fine-grid format-contract violation; loadTemplate() rejects the
    // whole file so a broken template can't half-load silently.
    auto formatError = [&tmpl](const std::string& reason) {
        if (!tmpl.parseError) {  // keep the FIRST violation as the reason
            tmpl.parseError = true;
            tmpl.parseErrorReason = reason;
        }
    };

    // Parse metadata headers
    if (line[0] == '#') {
        // Fine-grid tier declaration: "# grid: N" (N = cells per cube edge,
        // 27 or 81 — 9*3^k so every fine scale stays an exact multiple of the
        // voxel ladder and of the kinematic culling lattice). Must precede all
        // geometry; V lines are only legal after it; C/S/M become illegal.
        const std::string gridKey = "# grid:";
        if (line.compare(0, gridKey.size(), gridKey) == 0) {
            int n = 0;
            try { n = std::stoi(line.substr(gridKey.size())); } catch (...) {}
            if (n != 27 && n != 81) {
                // Invalid lattice: anything not 9*3^k would not divide 1/9 and
                // would corrupt adjacency culling (span-rounding failure).
                formatError("invalid # grid value " + std::to_string(n) +
                            " (allowed: 27, 81)");
            } else if (!tmpl.cubes.empty() || !tmpl.subcubes.empty() ||
                       !tmpl.microcubes.empty() || !tmpl.fineVoxels.empty()) {
                formatError("# grid must precede all geometry lines");
            } else {
                tmpl.fineGridResolution = n;
            }
            return;
        }

        // Check for "# facing_yaw: X.XXX"
        const std::string facingKey = "# facing_yaw:";
        if (line.compare(0, facingKey.size(), facingKey) == 0) {
            try {
                tmpl.facingYaw = std::stof(line.substr(facingKey.size()));
            } catch (...) {}
            return;
        }

        // Semantic class: "# category: <name>" (gen_tree.py writes
        // "# category:     nature"; furniture files may omit it). The value
        // can carry leading padding, so trim surrounding whitespace.
        const std::string categoryKey = "# category:";
        if (line.compare(0, categoryKey.size(), categoryKey) == 0) {
            std::string val = line.substr(categoryKey.size());
            size_t b = val.find_first_not_of(" \t\r\n");
            size_t e = val.find_last_not_of(" \t\r\n");
            if (b != std::string::npos)
                tmpl.category = val.substr(b, e - b + 1);
            return;
        }

        // Planar projected surface (Tier 2 decorated prop — rug/painting/banner):
        //   "# surface: texture=<material> projection=planar axis=<x|y|z>"
        // One image is stretched across the object footprint along the two axes
        // perpendicular to `axis` (see docs/VoxelAppearanceModel.md §7 Phase 3).
        const std::string surfaceKey = "# surface:";
        if (line.compare(0, surfaceKey.size(), surfaceKey) == 0) {
            std::istringstream iss(line.substr(surfaceKey.size()));
            std::string token;
            while (iss >> token) {
                auto eq = token.find('=');
                if (eq == std::string::npos) continue;
                std::string key = token.substr(0, eq);
                std::string val = token.substr(eq + 1);
                if (key == "texture")          tmpl.surface.texture = val;
                else if (key == "projection")  tmpl.surface.projection = val;
                else if (key == "axis" && !val.empty())
                    tmpl.surface.axis = static_cast<char>(std::tolower(val[0]));
            }
            if (tmpl.surface.texture.empty())
                LOG_WARN_FMT("ObjectTemplateManager", "# surface: missing texture= in line: " << line);
            return;
        }

        // Composite-part directive:
        //   "# part: <name>"                              (static part)
        //   "# part: <name> hinge=<keyword|x,y,z> axis=<x|y|z>"  (movable part)
        // Voxels emitted after this line are tagged with the new part until
        // the next `# part:` directive or end of file. Backward-compatible:
        // files that never use the directive end up with one implicit
        // "default" part (created on demand by VoxelTemplate::addCube etc.).
        const std::string partKey = "# part:";
        if (line.compare(0, partKey.size(), partKey) == 0) {
            std::istringstream iss(line.substr(partKey.size()));
            std::string partName;
            iss >> partName;
            if (partName.empty()) {
                LOG_WARN_FMT("ObjectTemplateManager", "Empty part name in line: " << line);
                return;
            }
            VoxelTemplatePart part;
            part.name = partName;
            std::string token;
            while (iss >> token) {
                auto eq = token.find('=');
                if (eq == std::string::npos) continue;
                std::string key = token.substr(0, eq);
                std::string val = token.substr(eq + 1);
                if (key == "hinge") {
                    part.movable = true;
                    // Try parsing "x,y,z" first; if that fails treat as keyword.
                    float hx, hy, hz; char c1, c2;
                    std::istringstream vs(val);
                    if ((vs >> hx >> c1 >> hy >> c2 >> hz) && c1 == ',' && c2 == ',') {
                        part.hingeExplicit = true;
                        part.hingeLocal = {hx, hy, hz};
                    } else {
                        part.hingeKeyword = val;
                    }
                } else if (key == "axis") {
                    part.axis = val;
                } else if (key == "slide") {
                    // slide=x+ | x- | y+ | y- | z+ | z- (or trailing sign optional => +)
                    part.movable = true;
                    part.slide = true;
                    if (val.empty()) {
                        LOG_WARN_FMT("ObjectTemplateManager",
                            "Empty slide direction in line: " << line);
                    } else {
                        char ax = static_cast<char>(std::tolower(val[0]));
                        float sign = 1.0f;
                        if (val.size() > 1) {
                            if (val[1] == '-') sign = -1.0f;
                            else if (val[1] == '+') sign = 1.0f;
                        }
                        glm::vec3 dir{0.0f};
                        if      (ax == 'x') dir.x = sign;
                        else if (ax == 'y') dir.y = sign;
                        else if (ax == 'z') dir.z = sign;
                        else LOG_WARN_FMT("ObjectTemplateManager",
                            "Unknown slide axis '" << val << "' in line: " << line);
                        part.slideDirLocal = dir;
                        part.axis = std::string(1, ax);
                    }
                } else {
                    LOG_WARN_FMT("ObjectTemplateManager",
                        "Unknown part attribute '" << key << "' in line: " << line);
                }
            }
            // Reject duplicate names — they'd produce ambiguous partId routing.
            for (const auto& existing : tmpl.parts) {
                if (existing.name == part.name) {
                    LOG_WARN_FMT("ObjectTemplateManager",
                        "Duplicate part name '" << part.name << "' in template '"
                        << tmpl.name << "' — keeping first definition");
                    // Still switch currentPartId to the existing entry so
                    // authors can re-open a part across the file.
                    for (size_t i = 0; i < tmpl.parts.size(); ++i) {
                        if (tmpl.parts[i].name == part.name) {
                            tmpl.currentPartId = static_cast<int>(i);
                            break;
                        }
                    }
                    return;
                }
            }
            tmpl.ensureDefaultPart();
            tmpl.parts.push_back(part);
            tmpl.currentPartId = static_cast<int>(tmpl.parts.size()) - 1;
            return;
        }

        // New format: "# interaction_point: pointId type localX localY localZ facingYaw group1,group2,..."
        // Optional trailing fields: radius promptText viewAngle
        // Only asset-level data; per-archetype offsets live in JSON profiles.
        const std::string interactionPointKey = "# interaction_point:";
        if (line.compare(0, interactionPointKey.size(), interactionPointKey) == 0) {
            std::istringstream iss(line.substr(interactionPointKey.size()));
            Core::InteractionPointDef def;
            std::string groupsStr;
            iss >> def.pointId >> def.type
                >> def.localOffset.x >> def.localOffset.y >> def.localOffset.z
                >> def.facingYaw >> groupsStr;
            if (!iss.fail()) {
                // Parse comma-separated supported groups
                if (!groupsStr.empty() && groupsStr != "*") {
                    std::istringstream groupStream(groupsStr);
                    std::string group;
                    while (std::getline(groupStream, group, ',')) {
                        if (!group.empty()) def.supportedGroups.push_back(group);
                    }
                }
                // Optional: radius
                float radius = 0.0f;
                if (iss >> radius) {
                    def.interactionRadius = radius;
                }
                // Optional: promptText (quoted string)
                std::string prompt;
                if (iss >> std::ws && iss.peek() == '"') {
                    iss.get(); // consume opening quote
                    std::getline(iss, prompt, '"');
                    def.promptText = prompt;
                }
                // Optional: viewAngle
                float viewAngle = 0.0f;
                if (iss >> viewAngle) {
                    def.viewAngleHalf = viewAngle;
                }
                // Optional: require_compatibility flag (0/1, defaults to 1).
                // Authors append "0" to mark a forgiving point — compat-check
                // errors become warnings and `can_interact` returns true.
                int requireCompat = 1;
                if (iss >> requireCompat) {
                    def.requireCompatibility = (requireCompat != 0);
                }
                tmpl.interactionPoints.push_back(def);
            } else {
                LOG_WARN_FMT("ObjectTemplateManager", "Failed to parse interaction_point line: " << line);
            }
            return;
        }

        // Legacy format: "# interaction: pointId type localX localY localZ facingYaw sitDownXYZ sittingIdleXYZ sitStandUpXYZ blendDuration seatHeightOffset"
        const std::string interactionKey = "# interaction:";
        if (line.compare(0, interactionKey.size(), interactionKey) == 0) {
            std::istringstream iss(line.substr(interactionKey.size()));
            Core::InteractionPointDef def;
            iss >> def.pointId >> def.type
                >> def.localOffset.x >> def.localOffset.y >> def.localOffset.z
                >> def.facingYaw
                >> def.sitDownOffset.x >> def.sitDownOffset.y >> def.sitDownOffset.z
                >> def.sittingIdleOffset.x >> def.sittingIdleOffset.y >> def.sittingIdleOffset.z
                >> def.sitStandUpOffset.x >> def.sitStandUpOffset.y >> def.sitStandUpOffset.z
                >> def.sitBlendDuration >> def.seatHeightOffset;
            if (!iss.fail()) {
                tmpl.interactionPoints.push_back(def);
            } else {
                LOG_WARN_FMT("ObjectTemplateManager", "Failed to parse interaction line: " << line);
            }
            return;
        }

        return;
    }

    std::stringstream ss(line);
    char type;
    ss >> type;

    // Optional trailing `tint=#rrggbb` and/or `state=<name>` tokens (any order after
    // the material). tint -> packed 0xRRGGBB (0xFFFFFF when absent); state -> enum
    // (0 normal,1 flaming,2 smoldering,3 charred,4 wet,5 mossy). Decouples color +
    // state from material (docs/VoxelAppearanceModel.md).
    auto parseExtras = [](std::stringstream& s, uint32_t& tint, uint8_t& state) {
        tint = 0xFFFFFFu; state = 0;
        std::string tok;
        while (s >> tok) {
            if (tok.rfind("tint=", 0) == 0) {
                std::string hex = tok.substr(5);
                if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
                if (hex.size() == 6) {
                    try { tint = static_cast<uint32_t>(std::stoul(hex, nullptr, 16)); }
                    catch (...) {}
                }
            } else if (tok.rfind("state=", 0) == 0) {
                std::string v = tok.substr(6);
                if      (v == "flaming")    state = 1;
                else if (v == "smoldering") state = 2;
                else if (v == "charred")    state = 3;
                else if (v == "wet")        state = 4;
                else if (v == "mossy")      state = 5;
                else                        state = 0;  // "normal" / unknown
            }
        }
    };

    uint32_t tint; uint8_t state;
    if (type == 'C' || type == 'S' || type == 'M') {
        // One lattice per file: legacy tiers are illegal once # grid declared
        // (mixed lattices would break the exact-cell culling + merge contract).
        if (tmpl.isFineGrid()) {
            formatError(std::string("legacy '") + type +
                        "' line in a fine-grid (# grid) template");
            return;
        }
    }
    if (type == 'C') {
        int x, y, z;
        std::string mat;
        ss >> x >> y >> z >> mat;
        parseExtras(ss, tint, state);
        tmpl.addCube({x, y, z}, mat, tint);   // cube greedy path carries no tint/state yet
    } else if (type == 'S') {
        int px, py, pz, sx, sy, sz;
        std::string mat;
        ss >> px >> py >> pz >> sx >> sy >> sz >> mat;
        parseExtras(ss, tint, state);
        tmpl.addSubcube({px, py, pz}, {sx, sy, sz}, mat, tint, state);
    } else if (type == 'M') {
        int px, py, pz, sx, sy, sz, mx, my, mz;
        std::string mat;
        ss >> px >> py >> pz >> sx >> sy >> sz >> mx >> my >> mz >> mat;
        parseExtras(ss, tint, state);
        tmpl.addMicrocube({px, py, pz}, {sx, sy, sz}, {mx, my, mz}, mat, tint, state);
    } else if (type == 'V') {
        // Fine-grid voxel: "V x y z Material [tint=...] [state=...]" — integer
        // min-corner cell coords on the declared # grid lattice (cells >= 0).
        if (!tmpl.isFineGrid()) {
            formatError("V line before # grid header (fine scale undefined)");
            return;
        }
        int x, y, z;
        std::string mat;
        ss >> x >> y >> z >> mat;
        if (ss.fail() || mat.empty()) {
            formatError("malformed V line: " + line);
            return;
        }
        parseExtras(ss, tint, state);
        tmpl.addFineVoxel({x, y, z}, mat, tint, state);
    }
}

int ObjectTemplateManager::decorateFlora(WorldGenerator& generator,
                                         int colMinX, int colMinZ, int colMaxX, int colMaxZ) {
    auto placements = generator.planFlora(colMinX, colMinZ, colMaxX, colMaxZ);
    constexpr size_t kFloraCap = 3000;  // guard against runaway loads
    if (placements.size() > kFloraCap) placements.resize(kFloraCap);

    int placed = 0;
    for (const auto& p : placements) {
        const VoxelTemplate* t = getTemplate(p.templateName);
        if (!t) continue;
        // Footprint extents (cube cells) so we center the trunk on the sampled column instead
        // of anchoring the template's min corner there.
        glm::ivec3 mx(0);
        for (const auto& c : t->cubes)      mx = glm::max(mx, c.relativePos);
        for (const auto& s : t->subcubes)   mx = glm::max(mx, s.parentRelativePos);
        for (const auto& m : t->microcubes) mx = glm::max(mx, m.parentRelativePos);
        glm::vec3 base(static_cast<float>(p.worldX - mx.x / 2),
                       static_cast<float>(p.surfaceY + 1),
                       static_cast<float>(p.worldZ - mx.z / 2));
        if (spawnTemplate(p.templateName, base, /*isStatic*/ true, /*rotation*/ 0)) ++placed;
    }
    LOG_INFO_FMT("ObjectTemplateManager", "decorateFlora: placed " << placed << " / "
                 << placements.size() << " planned plants");
    return placed;
}

namespace {
// Per-tree-type defaults for procedural generation (materials + base height). Mirrors the
// gen_tree.py archetype table so procedural and pooled trees of the same type theme alike.
struct TreeTypeInfo { const char* log; const char* leaf; int baseHeight; };
TreeTypeInfo treeTypeInfo(const std::string& type) {
    if (type == "birch")     return {"LogBirch",   "LeafBirch",  8};
    if (type == "spruce")    return {"LogSpruce",  "LeafSpruce", 9};
    if (type == "pine")      return {"LogPine",    "LeafSpruce", 12};
    if (type == "fir")       return {"LogPine",    "LeafSpruce", 14};
    if (type == "jungle")    return {"LogJungle",  "LeafJungle", 13};
    if (type == "palm")      return {"LogPalm",    "LeafJungle", 8};
    if (type == "willow")    return {"Log",        "Leaf",       8};
    if (type == "redwood")   return {"LogRedwood", "Leaf",       48};  // megaflora
    if (type == "elder_oak") return {"LogRedwood", "Leaf",       36};  // megaflora
    if (type == "acacia")    return {"Log",        "Leaf",       6};
    if (type == "dead")      return {"LogSpruce",  "",           6};
    if (type == "bush")      return {"",           "Leaf",       2};  // height doubles as radius
    if (type == "autumn")    return {"Log",        "LeafAutumn", 7};
    return {"Log", "Leaf", 7};  // oak / unknown
}
uint32_t posHash(int x, int z, uint32_t salt) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16; return h;
}
}  // namespace

void ObjectTemplateManager::decorateChunk(Chunk& chunk, const glm::ivec3& chunkCoord,
                                          WorldGenerator& generator) {
    constexpr int CS = 32;
    const int wx0 = chunkCoord.x * CS, wy0 = chunkCoord.y * CS, wz0 = chunkCoord.z * CS;
    const int wx1 = wx0 + CS - 1, wy1 = wy0 + CS - 1, wz1 = wz0 + CS - 1;
    // Inflate by the widest loaded template's half-footprint (not a fixed 12) so a plant rooted
    // in a neighbor chunk still contributes its overhang here — no silent seam clip of wide canopies.
    const int margin = m_floraMarginColumns;

    // planFlora is order-independent, so the placements whose footprint reaches this chunk are
    // identical to those a whole-region pass (or a neighbor chunk) would compute.
    auto placements = generator.planFlora(wx0 - margin, wz0 - margin, wx1 + margin, wz1 + margin, 0);
    if (placements.empty()) return;
    const uint32_t worldSeed = generator.getSeed();

    auto inChunk = [&](const glm::ivec3& w) {
        return w.x >= wx0 && w.x <= wx1 && w.y >= wy0 && w.y <= wy1 && w.z >= wz0 && w.z <= wz1;
    };
    // Clip-stamp a template's voxels into just this chunk (its footprint centered on the column).
    auto stamp = [&](const VoxelTemplate& t, int worldX, int surfaceY, int worldZ) {
        glm::ivec3 mx(0);
        for (const auto& c : t.cubes)      mx = glm::max(mx, c.relativePos);
        for (const auto& s : t.subcubes)   mx = glm::max(mx, s.parentRelativePos);
        for (const auto& m : t.microcubes) mx = glm::max(mx, m.parentRelativePos);
        const glm::ivec3 base(worldX - mx.x / 2, surfaceY + 1, worldZ - mx.z / 2);
        for (const auto& c : t.cubes) {
            glm::ivec3 w = base + c.relativePos;
            if (inChunk(w)) chunk.addCube(Utils::CoordinateUtils::worldToLocalCoord(w), c.material);
        }
        for (const auto& s : t.subcubes) {
            glm::ivec3 w = base + s.parentRelativePos;
            if (inChunk(w)) chunk.addSubcube(Utils::CoordinateUtils::worldToLocalCoord(w), s.subcubePos, s.material);
        }
        for (const auto& m : t.microcubes) {
            glm::ivec3 w = base + m.parentRelativePos;
            if (inChunk(w)) chunk.addMicrocube(Utils::CoordinateUtils::worldToLocalCoord(w),
                                               m.subcubePos, m.microcubePos, m.material);
        }
    };

    for (const auto& p : placements) {
        if (p.procedural) {
            // Generate a unique tree deterministically from (position, world seed) so every chunk
            // that clips this tree produces the identical voxels (seamless).
            TreeTypeInfo info = treeTypeInfo(p.templateName);
            const uint32_t treeSeed = posHash(p.worldX, p.worldZ, worldSeed);
            int height = info.baseHeight + static_cast<int>(posHash(p.worldX, p.worldZ, worldSeed ^ 0x55u) % 5) - 2;
            VoxelTemplate gen = ProceduralTree::generate(p.templateName, std::max(2, height),
                                                         p.fullness, treeSeed, info.log, info.leaf);
            if (!gen.cubes.empty() || !gen.subcubes.empty()) stamp(gen, p.worldX, p.surfaceY, p.worldZ);
        } else if (const VoxelTemplate* t = getTemplate(p.templateName)) {
            stamp(*t, p.worldX, p.surfaceY, p.worldZ);
        }
    }
}

const VoxelTemplate* ObjectTemplateManager::getTemplate(const std::string& name) const {
    auto it = m_templates.find(name);
    if (it != m_templates.end()) {
        return it->second.get();
    }
    // Relative-path alias ("items/torch" -> stem) from the recursive scan.
    auto al = m_aliases.find(name);
    if (al != m_aliases.end()) {
        it = m_templates.find(al->second);
        if (it != m_templates.end()) return it->second.get();
    }
    return nullptr;
}

std::vector<std::string> ObjectTemplateManager::getTemplateNames() const {
    std::vector<std::string> names;
    names.reserve(m_templates.size());
    for (const auto& [name, tmpl] : m_templates) {
        names.push_back(name);
    }
    return names;
}

bool ObjectTemplateManager::spawnTemplate(const std::string& name, const glm::vec3& worldPos, bool isStatic, int rotation) {
    const VoxelTemplate* tmpl = getTemplate(name);
    if (!tmpl) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template not found: " << name);
        return false;
    }
    if (!canBakeStatic(*tmpl)) {
        // Fine-grid templates are kinematic-only. Both branches below walk the
        // C/S/M tiers, so a fine template would place NOTHING — refuse loudly
        // instead of silently spawning an empty object.
        LOG_ERROR_FMT("ObjectTemplateManager", "Template '" << name
                      << "' is fine-grid (# grid " << tmpl->fineGridResolution
                      << ") and kinematic-only — spawn it as an item prop "
                      << "(ItemPropManager / spawn_item), not via spawnTemplate");
        return false;
    }

    m_lastSpawnedKinematicIds.clear();

    // Detect movable parts. Backward-compat: when there are no movable parts
    // (the common case), every voxel routes straight to the chunk bake path.
    auto isMovablePart = [&](int partId) -> bool {
        if (partId < 0 || partId >= static_cast<int>(tmpl->parts.size())) return false;
        return tmpl->parts[partId].movable;
    };
    bool hasMovable = false;
    for (const auto& p : tmpl->parts) {
        if (p.movable) { hasMovable = true; break; }
    }
    // Routing to the kinematic manager requires both a movable part AND a
    // wired manager. Without the manager pointer we fall back to the legacy
    // path so command-line tools / tests that bypass Application.cpp still
    // work (movable voxels stay baked, behavior is identical to pre-C0b).
    const bool routeKinematic = hasMovable && (m_kinematicManager != nullptr);

    // Per-voxel tint (docs/VoxelAppearanceModel.md): Phase 1b gives the static chunk
    // InstanceData a tint channel, so tinted, non-movable templates now BAKE TO CHUNKS
    // (gaining collision + greedy meshing) with tint flowing through Chunk::addSubcube/
    // addMicrocube below. Movable parts carry tint via the kinematic gather (v.tint).

    // Normalize rotation to number of 90° steps
    int rotSteps = ((rotation % 360) + 360) % 360 / 90;

    // Compute bounding box of template for rotation pivot
    glm::ivec3 maxExtent(0);
    if (rotSteps > 0) {
        for (const auto& c : tmpl->cubes) {
            maxExtent = glm::max(maxExtent, c.relativePos);
        }
        for (const auto& s : tmpl->subcubes) {
            maxExtent = glm::max(maxExtent, s.parentRelativePos);
        }
        for (const auto& m : tmpl->microcubes) {
            maxExtent = glm::max(maxExtent, m.parentRelativePos);
        }
    }

    // Rotate a block-level offset around Y axis (keeps all offsets non-negative)
    auto rotateOffset = [&](glm::ivec3 pos) -> glm::ivec3 {
        switch (rotSteps) {
            case 1: return glm::ivec3(maxExtent.z - pos.z, pos.y, pos.x);                           // 90° CW
            case 2: return glm::ivec3(maxExtent.x - pos.x, pos.y, maxExtent.z - pos.z);             // 180°
            case 3: return glm::ivec3(pos.z, pos.y, maxExtent.x - pos.x);                           // 270° CW
            default: return pos;
        }
    };

    // Rotate a sub-grid local position (0-2 range) around Y axis
    auto rotateLocal = [&](glm::ivec3 lp) -> glm::ivec3 {
        switch (rotSteps) {
            case 1: return glm::ivec3(2 - lp.z, lp.y, lp.x);
            case 2: return glm::ivec3(2 - lp.x, lp.y, 2 - lp.z);
            case 3: return glm::ivec3(lp.z, lp.y, 2 - lp.x);
            default: return lp;
        }
    };

    glm::ivec3 basePos = glm::round(worldPos);
    std::unordered_set<Chunk*> modifiedChunks;

    // Helper: get-or-create chunk and enable bulk physics mode on first touch
    auto getOrCreateChunk = [&](const glm::ivec3& worldBlockPos) -> Chunk* {
        glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(worldBlockPos);
        auto it = m_chunkManager->chunkMap.find(chunkCoord);
        if (it == m_chunkManager->chunkMap.end()) {
            glm::ivec3 origin = chunkCoord * 32;
            m_chunkManager->createChunk(origin, false);
            it = m_chunkManager->chunkMap.find(chunkCoord);
        }
        if (it == m_chunkManager->chunkMap.end()) return nullptr;
        Chunk* chunk = it->second;
        if (modifiedChunks.insert(chunk).second) {
            chunk->setPhysicsBulkMode(true);
        }
        return chunk;
    };

    // Phase C0b: voxels belonging to movable parts get collected here, keyed
    // by partId, in template-local *unrotated* float coordinates (the centers
    // of each voxel in cube units). Hinge resolution happens after the gather.
    struct PartVoxelAcc {
        std::vector<Core::KinematicVoxel> voxels;
        glm::vec3 aabbMin{ std::numeric_limits<float>::max()};
        glm::vec3 aabbMax{-std::numeric_limits<float>::max()};
        void extend(const glm::vec3& min, const glm::vec3& max) {
            aabbMin = glm::min(aabbMin, min);
            aabbMax = glm::max(aabbMax, max);
        }
    };
    std::unordered_map<int, PartVoxelAcc> movableAcc;

    // Spawn Cubes
    for (const auto& tCube : tmpl->cubes) {
        if (routeKinematic && isMovablePart(tCube.partId)) {
            // Local-space (unrotated) voxel descriptor. We pin world placement
            // and rotation in the kinematic transform later so the hinge can
            // be the rotation pivot.
            Core::KinematicVoxel v;
            v.localPos     = glm::vec3(tCube.relativePos) + glm::vec3(0.5f);
            v.scale        = glm::vec3(1.0f);
            v.parentFrac   = glm::vec3(0.0f);
            v.materialName = tCube.material;
            auto& acc = movableAcc[tCube.partId];
            acc.voxels.push_back(v);
            acc.extend(glm::vec3(tCube.relativePos),
                       glm::vec3(tCube.relativePos) + glm::vec3(1.0f));
            continue;
        }
        glm::ivec3 pos = basePos + rotateOffset(tCube.relativePos);
        glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(pos);
        if (Chunk* chunk = getOrCreateChunk(pos)) {
            chunk->addCube(localPos, tCube.material);
        }
    }

    // Spawn Subcubes
    for (const auto& tSub : tmpl->subcubes) {
        if (routeKinematic && isMovablePart(tSub.partId)) {
            const glm::vec3 parent = glm::vec3(tSub.parentRelativePos);
            const glm::vec3 sub    = glm::vec3(tSub.subcubePos) / 3.0f;
            const glm::vec3 size   = glm::vec3(1.0f / 3.0f);
            Core::KinematicVoxel v;
            v.localPos     = parent + sub + size * 0.5f;
            v.scale        = size;
            v.parentFrac   = sub;
            v.materialName = tSub.material;
            auto& acc = movableAcc[tSub.partId];
            acc.voxels.push_back(v);
            acc.extend(parent + sub, parent + sub + size);
            continue;
        }
        glm::ivec3 parentPos = basePos + rotateOffset(tSub.parentRelativePos);
        glm::ivec3 subPos = rotateLocal(tSub.subcubePos);
        glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
        if (Chunk* chunk = getOrCreateChunk(parentPos)) {
            chunk->addSubcube(localPos, subPos, tSub.material, tSub.tint, tSub.state);
        }
    }

    // Spawn Microcubes
    for (const auto& tMicro : tmpl->microcubes) {
        if (routeKinematic && isMovablePart(tMicro.partId)) {
            const glm::vec3 parent = glm::vec3(tMicro.parentRelativePos);
            const glm::vec3 sub    = glm::vec3(tMicro.subcubePos) / 3.0f;
            const glm::vec3 micro  = glm::vec3(tMicro.microcubePos) / 9.0f;
            const glm::vec3 size   = glm::vec3(1.0f / 9.0f);
            Core::KinematicVoxel v;
            v.localPos     = parent + sub + micro + size * 0.5f;
            v.scale        = size;
            v.parentFrac   = sub + micro;
            v.materialName = tMicro.material;
            auto& acc = movableAcc[tMicro.partId];
            acc.voxels.push_back(v);
            acc.extend(parent + sub + micro, parent + sub + micro + size);
            continue;
        }
        glm::ivec3 parentPos = basePos + rotateOffset(tMicro.parentRelativePos);
        glm::ivec3 subPos = rotateLocal(tMicro.subcubePos);
        glm::ivec3 microPos = rotateLocal(tMicro.microcubePos);
        glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
        if (Chunk* chunk = getOrCreateChunk(parentPos)) {
            chunk->addMicrocube(localPos, subPos, microPos, tMicro.material, tMicro.tint, tMicro.state);
        }
    }

    // Finalise all modified chunks: flush deferred collisions, rebuild faces
    for (Chunk* chunk : modifiedChunks) {
        chunk->batchUpdateCollisions();
        chunk->setPhysicsBulkMode(false);
        chunk->rebuildFaces();
        chunk->updateVulkanBuffer();
    }

    // --------------------------------------------------------------------
    // Phase C0b: emit movable parts as KinematicVoxelObjects.
    //
    // For each accumulated part:
    //   * Resolve the hinge in template-local cube space (either explicit
    //     `x,y,z` from the directive, or a keyword resolved against the
    //     part's AABB).
    //   * Shift every voxel's localPos so the hinge sits at the origin.
    //   * Build the initial world transform as
    //       Translate(worldPos + rotateOffset(hingeLocal)) * Rotate(rotation).
    //     The kinematic transform IS the part's pivot, so future angle
    //     updates only have to multiply a rotation about `axis`.
    // --------------------------------------------------------------------
    if (routeKinematic) {
        auto resolveHingeKeyword = [](const std::string& kw,
                                      const glm::vec3& aabbMin,
                                      const glm::vec3& aabbMax) -> glm::vec3 {
            glm::vec3 mid = 0.5f * (aabbMin + aabbMax);
            glm::vec3 h = mid;
            std::string lower = kw;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            // Tokenize on underscore / whitespace.
            std::istringstream tok(lower);
            std::string seg;
            while (std::getline(tok, seg, '_')) {
                if      (seg == "left")   h.x = aabbMin.x;
                else if (seg == "right")  h.x = aabbMax.x;
                else if (seg == "bottom") h.y = aabbMin.y;
                else if (seg == "top")    h.y = aabbMax.y;
                else if (seg == "front")  h.z = aabbMin.z;
                else if (seg == "back")   h.z = aabbMax.z;
                else if (seg == "center") {} // already midpoint
                else if (!seg.empty()) {
                    LOG_WARN_FMT("ObjectTemplateManager",
                        "Unknown hinge keyword token '" << seg << "'");
                }
            }
            return h;
        };

        // Reuse the integer rotateOffset above by re-implementing it for
        // floats (so the hinge — which can be fractional, e.g. subcube
        // boundary — rotates consistently with the integer cube positions).
        auto rotateOffsetF = [&](glm::vec3 pos) -> glm::vec3 {
            switch (rotSteps) {
                case 1: return glm::vec3(maxExtent.z - pos.z, pos.y, pos.x);
                case 2: return glm::vec3(maxExtent.x - pos.x, pos.y, maxExtent.z - pos.z);
                case 3: return glm::vec3(pos.z, pos.y, maxExtent.x - pos.x);
                default: return pos;
            }
        };
        float rotRad = glm::radians(static_cast<float>(rotSteps * 90));

        for (auto& [partId, acc] : movableAcc) {
            if (acc.voxels.empty()) continue;
            const auto& part = tmpl->parts[partId];

            glm::vec3 hingeLocal;
            if (part.hingeExplicit) {
                hingeLocal = part.hingeLocal;
            } else if (!part.hingeKeyword.empty()) {
                hingeLocal = resolveHingeKeyword(part.hingeKeyword, acc.aabbMin, acc.aabbMax);
            } else if (part.slide) {
                // Slide-only parts use AABB min as the local origin so the
                // voxels keep their authored positions relative to the
                // KVO transform (no rotation pivot needed).
                hingeLocal = acc.aabbMin;
            } else {
                // Movable but no hinge specified — fall back to AABB center.
                hingeLocal = 0.5f * (acc.aabbMin + acc.aabbMax);
                LOG_WARN_FMT("ObjectTemplateManager",
                    "Movable part '" << part.name << "' in template '"
                    << tmpl->name << "' has no hinge — defaulting to AABB center");
            }

            // Shift voxels into hinge-relative space so rotation about the
            // hinge is just a rotation of the object transform.
            for (auto& v : acc.voxels) {
                v.localPos -= hingeLocal;
            }

            // Initial world transform = translate(hingeWorld) * rotate(rotSteps).
            glm::vec3 hingeWorld = glm::vec3(basePos) + rotateOffsetF(hingeLocal);
            glm::mat4 xform = glm::translate(glm::mat4(1.0f), hingeWorld) *
                              glm::rotate(glm::mat4(1.0f), -rotRad, glm::vec3(0, 1, 0));

            std::string idHint = tmpl->name + "_" + part.name;
            std::string kinematicId = m_kinematicManager->add(
                idHint,
                std::move(acc.voxels),
                xform,
                "",      // placedObjectId — wired by the caller via PlacedObject metadata
                false);
            m_lastSpawnedKinematicIds.push_back(kinematicId);

            // Phase C: auto-register with the animator (if wired) so callers can
            // drive setTargetAngle/setTargetOffset without recomputing pivots.
            if (m_animator) {
                Core::KinematicAnimator::PartConfig pc;
                pc.kinematicId = kinematicId;
                pc.hingeWorld  = hingeWorld;
                if      (part.axis == "x") pc.rotationAxis = Core::KinematicAnimator::Axis::X;
                else if (part.axis == "z") pc.rotationAxis = Core::KinematicAnimator::Axis::Z;
                else                        pc.rotationAxis = Core::KinematicAnimator::Axis::Y;
                pc.baseRotationRad = -rotRad;
                if (part.slide) pc.slideDirLocal = part.slideDirLocal;
                m_animator->registerPart(pc);
            }

            LOG_INFO_FMT("ObjectTemplateManager",
                "Spawned kinematic part '" << part.name
                << "' from template '" << tmpl->name
                << "' (id=" << kinematicId
                << ", hingeLocal=(" << hingeLocal.x << ","
                << hingeLocal.y << "," << hingeLocal.z
                << "), axis=" << part.axis << ")");
        }
    }

    return true;
}

// ERASE the exact cells spawnTemplateMicro would write, for the same pose. Removal
// used to go through PlacedObjectManager::clearRegion, which deletes whole CUBES
// across the object's bbox — so removing a 0.33 m stool standing against a wall took
// the wall cube with it and punched a hole you could see the interior through. A
// remove must undo what the place did, at the resolution the place used. Shares the
// rasterization with spawnTemplateMicro below via spawnOrEraseMicro.
bool ObjectTemplateManager::eraseTemplateMicro(const std::string& name,
                                               const glm::ivec3& worldMicro, int rotation) {
    return spawnOrEraseMicro(name, worldMicro, rotation, /*erase=*/true);
}

bool ObjectTemplateManager::spawnTemplateMicro(const std::string& name, const glm::ivec3& worldMicro,
                                               int rotation) {
    return spawnOrEraseMicro(name, worldMicro, rotation, /*erase=*/false);
}

bool ObjectTemplateManager::spawnOrEraseMicro(const std::string& name, const glm::ivec3& worldMicro,
                                              int rotation, bool erase) {
    const VoxelTemplate* tmpl = getTemplate(name);
    if (!tmpl) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template not found: " << name);
        return false;
    }
    if (!canBakeStatic(*tmpl)) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template '" << name
                      << "' is fine-grid and kinematic-only — the micro chunk bake "
                      << "cannot represent 1/" << tmpl->fineGridResolution
                      << " cells; spawn it as an item prop instead");
        return false;
    }
    if (!m_chunkManager) return false;

    const int rotSteps = ((rotation % 360) + 360) % 360 / 90;

    // 1) Expand EVERY voxel (cube/subcube/microcube) to template-local MICRO cells. The micro grid
    //    is 1 cube = 9 micro per axis (3 subcubes x 3 microcubes). A cube -> 9^3 micros, a subcube
    //    -> 3^3, a microcube -> 1:1. Furniture is microcube-authored so this is mostly 1:1.
    struct MCell { glm::ivec3 m; std::string mat; };
    std::vector<MCell> cells;
    auto addBox = [&](const glm::ivec3& microOrigin, int span, const std::string& mat) {
        for (int x = 0; x < span; ++x)
            for (int y = 0; y < span; ++y)
                for (int z = 0; z < span; ++z)
                    cells.push_back({microOrigin + glm::ivec3(x, y, z), mat});
    };
    for (const auto& c : tmpl->cubes)      addBox(c.relativePos * 9, 9, c.material);
    for (const auto& s : tmpl->subcubes)   addBox(s.parentRelativePos * 9 + s.subcubePos * 3, 3, s.material);
    for (const auto& m : tmpl->microcubes) addBox(m.parentRelativePos * 9 + m.subcubePos * 3 + m.microcubePos, 1, m.material);
    if (cells.empty()) return false;

    // 2) 90-deg rotation in MICRO space about Y, pivoting on the micro AABB (keeps offsets >= 0).
    glm::ivec3 mx(0);
    for (const auto& c : cells) mx = glm::max(mx, c.m);
    auto rotMicro = [&](glm::ivec3 p) -> glm::ivec3 {
        switch (rotSteps) {
            case 1: return glm::ivec3(mx.z - p.z, p.y, p.x);
            case 2: return glm::ivec3(mx.x - p.x, p.y, mx.z - p.z);
            case 3: return glm::ivec3(p.z, p.y, mx.x - p.x);
            default: return p;
        }
    };

    // 3) Shift to the micro-precise world position, decompose to (cube, subcube, microcube) with
    //    FLOOR division (correct for negative world coords), and write a microcube per cell.
    auto floorDiv = [](int a, int b) { int q = a / b, r = a % b; if (r != 0 && (r < 0) != (b < 0)) --q; return q; };
    auto floorMod = [&](int a, int b) { return a - floorDiv(a, b) * b; };

    std::unordered_set<Chunk*> modifiedChunks;
    auto getOrCreateChunk = [&](const glm::ivec3& worldBlockPos) -> Chunk* {
        glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(worldBlockPos);
        auto it = m_chunkManager->chunkMap.find(chunkCoord);
        if (it == m_chunkManager->chunkMap.end()) {
            m_chunkManager->createChunk(chunkCoord * 32, false);
            it = m_chunkManager->chunkMap.find(chunkCoord);
        }
        if (it == m_chunkManager->chunkMap.end()) return nullptr;
        Chunk* chunk = it->second;
        if (modifiedChunks.insert(chunk).second) chunk->setPhysicsBulkMode(true);
        return chunk;
    };

    for (const auto& cell : cells) {
        const glm::ivec3 gm = worldMicro + rotMicro(cell.m);   // global micro position
        const glm::ivec3 cube(floorDiv(gm.x, 9), floorDiv(gm.y, 9), floorDiv(gm.z, 9));
        const glm::ivec3 rem(floorMod(gm.x, 9), floorMod(gm.y, 9), floorMod(gm.z, 9));
        const glm::ivec3 sub(rem.x / 3, rem.y / 3, rem.z / 3);
        const glm::ivec3 mic(rem.x % 3, rem.y % 3, rem.z % 3);
        if (Chunk* chunk = getOrCreateChunk(cube)) {
            const glm::ivec3 lp = Utils::CoordinateUtils::worldToLocalCoord(cube);
            if (erase) chunk->removeMicrocube(lp, sub, mic);
            else       chunk->addMicrocube(lp, sub, mic, cell.mat);
        }
    }

    for (Chunk* chunk : modifiedChunks) {
        chunk->batchUpdateCollisions();
        chunk->setPhysicsBulkMode(false);
        // [no-frozen-engine] MARK DIRTY instead of the old synchronous rebuildFaces()+
        // updateVulkanBuffer() per touched chunk: the sync remesh made EVERY fixture spawn
        // pay full chunk remeshes + GPU uploads (~150-300 ms Debug each) — 76% of a
        // building's cost (measured: fixtures=11818 ms of TOTAL=15450). The budgeted
        // DirtyChunkTracker (6 ms/frame) remeshes within a few frames instead.
        m_chunkManager->markChunkDirty(chunk);
    }
    return true;
}

void ObjectTemplateManager::spawnTemplateSequentially(const std::string& name, const glm::vec3& worldPos, bool isStatic) {
    const VoxelTemplate* tmpl = getTemplate(name);
    if (!tmpl) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template not found: " << name);
        return;
    }
    if (!canBakeStatic(*tmpl)) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Template '" << name
                      << "' is fine-grid and kinematic-only — progressive spawn walks "
                      << "C/S/M tiers and would place nothing; spawn as an item prop");
        return;
    }

    // Create a new pending spawn task
    PendingSpawn spawn;
    spawn.templateName = name;
    spawn.worldPos = worldPos;
    spawn.isStatic = isStatic;
    spawn.templatePtr = tmpl;
    
    m_pendingSpawns.push_back(spawn);
    LOG_INFO_FMT("ObjectTemplateManager", "Queued sequential spawn for template: " << name);
}

void ObjectTemplateManager::update(float deltaTime) {
    if (m_pendingSpawns.empty()) return;

    // Process the first spawn in the queue
    PendingSpawn& spawn = m_pendingSpawns.front();
    const VoxelTemplate* tmpl = spawn.templatePtr;
    
    if (!tmpl) {
        m_pendingSpawns.pop_front();
        return;
    }

    glm::ivec3 basePos = glm::round(spawn.worldPos);
    std::unordered_set<Chunk*> modifiedChunks;
    int processedVoxels = 0;

    // ---------------------------------------------------------
    // Process Cubes Batch
    // ---------------------------------------------------------
    while (spawn.currentCubeIndex < tmpl->cubes.size() && processedVoxels < m_voxelsPerFrame) {
        const auto& tCube = tmpl->cubes[spawn.currentCubeIndex];
        glm::ivec3 pos = basePos + tCube.relativePos;
        
        if (spawn.isStatic) {
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(pos);
            glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(pos);

            Chunk* chunk = nullptr;
            auto it = m_chunkManager->chunkMap.find(chunkCoord);
            if (it != m_chunkManager->chunkMap.end()) {
                chunk = it->second;
            } else {
                // AUTO-CREATE CHUNK:
                // If the template extends into a chunk that doesn't exist yet (e.g. high in the air),
                // we must create it to avoid losing voxels.
                glm::ivec3 origin = chunkCoord * 32;
                m_chunkManager->createChunk(origin, false);

                it = m_chunkManager->chunkMap.find(chunkCoord);
                if (it != m_chunkManager->chunkMap.end()) {
                    chunk = it->second;
                }
            }

            if (chunk) {
                if (chunk->addCube(localPos)) {
                    modifiedChunks.insert(chunk);
                }
            }
        }

        spawn.currentCubeIndex++;
        processedVoxels++;
    }

    // ---------------------------------------------------------
    // Process Subcubes Batch
    // ---------------------------------------------------------
    while (spawn.currentSubcubeIndex < tmpl->subcubes.size() && processedVoxels < m_voxelsPerFrame) {
        const auto& tSub = tmpl->subcubes[spawn.currentSubcubeIndex];
        glm::ivec3 parentPos = basePos + tSub.parentRelativePos;
        
        if (spawn.isStatic) {
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(parentPos);
            glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
            
            Chunk* chunk = nullptr;
            auto it = m_chunkManager->chunkMap.find(chunkCoord);
            if (it != m_chunkManager->chunkMap.end()) {
                chunk = it->second;
            } else {
                 glm::ivec3 origin = chunkCoord * 32;
                m_chunkManager->createChunk(origin, false);
                it = m_chunkManager->chunkMap.find(chunkCoord);
                if (it != m_chunkManager->chunkMap.end()) {
                    chunk = it->second;
                }
            }

            if (chunk) {
                if (chunk->addSubcube(localPos, tSub.subcubePos, tSub.material, tSub.tint, tSub.state)) {
                    modifiedChunks.insert(chunk);
                }
            }
        }

        spawn.currentSubcubeIndex++;
        processedVoxels++;
    }

    // ---------------------------------------------------------
    // Process Microcubes Batch
    // ---------------------------------------------------------
    while (spawn.currentMicrocubeIndex < tmpl->microcubes.size() && processedVoxels < m_voxelsPerFrame) {
        const auto& tMicro = tmpl->microcubes[spawn.currentMicrocubeIndex];
        glm::ivec3 parentPos = basePos + tMicro.parentRelativePos;
        
        if (spawn.isStatic) {
            glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(parentPos);
            glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(parentPos);
            
            Chunk* chunk = nullptr;
            auto it = m_chunkManager->chunkMap.find(chunkCoord);
            if (it != m_chunkManager->chunkMap.end()) {
                chunk = it->second;
            } else {
                 glm::ivec3 origin = chunkCoord * 32;
                m_chunkManager->createChunk(origin, false);
                it = m_chunkManager->chunkMap.find(chunkCoord);
                if (it != m_chunkManager->chunkMap.end()) {
                    chunk = it->second;
                }
            }

            if (chunk) {
                if (chunk->addMicrocube(localPos, tMicro.subcubePos, tMicro.microcubePos, tMicro.material, tMicro.tint, tMicro.state)) {
                    modifiedChunks.insert(chunk);
                }
            }
        }

        spawn.currentMicrocubeIndex++;
        processedVoxels++;
    }

    // Update modified chunks immediately so the user sees the progress
    for (Chunk* chunk : modifiedChunks) {
        chunk->rebuildFaces();
        chunk->updateVulkanBuffer();
    }

    // Check if done
    if (spawn.currentCubeIndex >= tmpl->cubes.size() &&
        spawn.currentSubcubeIndex >= tmpl->subcubes.size() &&
        spawn.currentMicrocubeIndex >= tmpl->microcubes.size()) {
        
        m_pendingSpawns.pop_front();
        LOG_INFO("ObjectTemplateManager", "Finished sequential spawn");
    }
}

std::string ObjectTemplateManager::getTemplatePath(const std::string& name) const {
    auto it = m_templates.find(name);
    if (it == m_templates.end()) return "";
    return it->second->sourceFilePath;
}

bool ObjectTemplateManager::saveInteractionDefs(const std::string& templateName,
                                                 const std::vector<Core::InteractionPointDef>& defs) {
    std::string filePath = getTemplatePath(templateName);
    if (filePath.empty()) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Cannot save interaction defs: template '" << templateName << "' not found or has no source path");
        return false;
    }

    // Read the existing file
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Cannot open template file for reading: " << filePath);
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(inFile, line)) {
        // Skip existing interaction lines (both legacy and new format) — we'll rewrite them
        if (line.compare(0, 14, "# interaction:") == 0)
            continue;
        if (line.compare(0, 20, "# interaction_point:") == 0)
            continue;
        lines.push_back(line);
    }
    inFile.close();

    // Find insertion point: after other "# " metadata lines, before voxel data
    size_t insertIdx = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!lines[i].empty() && lines[i][0] == '#') {
            insertIdx = i + 1;
        } else if (!lines[i].empty()) {
            break; // Hit voxel data
        }
    }

    // Build interaction_point lines (new format: asset-level only)
    std::vector<std::string> interactionLines;
    for (const auto& def : defs) {
        // Build supported groups string
        std::string groupsStr = "*";
        if (!def.supportedGroups.empty()) {
            groupsStr.clear();
            for (size_t i = 0; i < def.supportedGroups.size(); ++i) {
                if (i > 0) groupsStr += ",";
                groupsStr += def.supportedGroups[i];
            }
        }
        char buf[512];
        int len = std::snprintf(buf, sizeof(buf),
            "# interaction_point: %s %s %.4f %.4f %.4f %.6f %s",
            def.pointId.c_str(), def.type.c_str(),
            def.localOffset.x, def.localOffset.y, def.localOffset.z,
            def.facingYaw, groupsStr.c_str());
        // Append optional fields if non-default
        if (def.interactionRadius > 0.0f || !def.promptText.empty() || def.viewAngleHalf > 0.0f) {
            len += std::snprintf(buf + len, sizeof(buf) - len, " %.2f", def.interactionRadius);
        }
        if (!def.promptText.empty() || def.viewAngleHalf > 0.0f) {
            len += std::snprintf(buf + len, sizeof(buf) - len, " \"%s\"", def.promptText.c_str());
        }
        if (def.viewAngleHalf > 0.0f) {
            len += std::snprintf(buf + len, sizeof(buf) - len, " %.1f", def.viewAngleHalf);
        }
        interactionLines.push_back(buf);
    }

    // Insert interaction lines
    lines.insert(lines.begin() + static_cast<int>(insertIdx),
                 interactionLines.begin(), interactionLines.end());

    // Write back
    std::ofstream outFile(filePath);
    if (!outFile.is_open()) {
        LOG_ERROR_FMT("ObjectTemplateManager", "Cannot open template file for writing: " << filePath);
        return false;
    }
    for (const auto& l : lines) {
        outFile << l << "\n";
    }
    outFile.close();

    // Update the in-memory template's interaction points
    auto it = m_templates.find(templateName);
    if (it != m_templates.end()) {
        it->second->interactionPoints = defs;
    }

    LOG_INFO_FMT("ObjectTemplateManager", "Saved " << defs.size() << " interaction defs to " << filePath);
    return true;
}

} // namespace Phyxel
