#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Core { struct InteractionPointDef; }

struct TemplateCube {
    glm::ivec3 relativePos;
    std::string material;
    /// Index into VoxelTemplate::parts. 0 is the implicit "default" part
    /// (static, no hinge) for backward compatibility.
    int partId = 0;
    /// Packed 0xRRGGBB per-voxel tint multiplier; 0xFFFFFF = no tint.
    /// Decouples color from material (see docs/VoxelAppearanceModel.md).
    uint32_t tint = 0xFFFFFFu;
};

struct TemplateSubcube {
    glm::ivec3 parentRelativePos;
    glm::ivec3 subcubePos;
    std::string material;
    int partId = 0;
    uint32_t tint = 0xFFFFFFu;   ///< Packed 0xRRGGBB per-voxel tint; 0xFFFFFF = none.
    uint8_t state = 0;           ///< Voxel state: 0 normal,1 flaming,2 smoldering,3 charred,4 wet.
};

struct TemplateMicrocube {
    glm::ivec3 parentRelativePos;
    glm::ivec3 subcubePos;
    glm::ivec3 microcubePos;
    std::string material;
    int partId = 0;
    uint32_t tint = 0xFFFFFFu;   ///< Packed 0xRRGGBB per-voxel tint; 0xFFFFFF = none.
    uint8_t state = 0;           ///< Voxel state: 0 normal,1 flaming,2 smoldering,3 charred,4 wet.
};

/// One cell of a fine-grid template — the finer-than-microcube item tier.
/// `pos` is a template-local min-corner cell coordinate on a uniform grid of
/// `VoxelTemplate::fineGridResolution` cells per cube edge (27 or 81, i.e.
/// 9·3^k so every fine scale stays an exact multiple of the engine's voxel
/// ladder). Authored via `V x y z Material [tint=#rrggbb] [state=...]` lines
/// after a `# grid: N` header. Fine templates are kinematic/prop-only.
struct TemplateFineVoxel {
    glm::ivec3 pos;
    std::string material;
    int partId = 0;
    uint32_t tint = 0xFFFFFFu;   ///< Packed 0xRRGGBB per-voxel tint; 0xFFFFFF = none.
    uint8_t state = 0;           ///< Voxel state: 0 normal,1 flaming,2 smoldering,3 charred,4 wet.
};

/// Composite-part metadata. A template ships at least one part — the
/// implicit "default" part (index 0, static, no hinge) created on demand
/// the first time any voxel is added. Authors override this by emitting
/// `# part: <name>` directives in the source file; voxels that follow are
/// tagged with the current part until the next directive.
///
/// Movable parts carry hinge + axis metadata so the kinematic spawn path
/// (Phase C0b) can spin one KinematicVoxelObject per movable part. Static
/// parts are baked into chunks like today.
struct VoxelTemplatePart {
    std::string name = "default";   ///< Author-chosen label (unique per template).
    bool movable = false;            ///< True when a `hinge=` directive is present.

    /// Hinge point in template-local cube coordinates. Authors can supply
    /// either an explicit `x,y,z` triple or a keyword that the spawn path
    /// resolves against the part's bounding box (e.g. `left_bottom`,
    /// `back_top`). `hingeKeyword` empty + `hingeExplicit` true => use the
    /// explicit vector. Otherwise the keyword is consulted at spawn time.
    bool hingeExplicit = false;
    glm::vec3 hingeLocal{0.0f};
    std::string hingeKeyword;

    /// Rotation axis for pivot-style parts. One of "x", "y", "z" (lowercase).
    /// Translation-style parts (drawers, slide doors) will use a separate
    /// `slide_axis=` directive added in Phase H — until then this field is
    /// purely for hinges.
    std::string axis = "y";

    /// Phase H — slide-only movable part. Set true when the directive
    /// contains `slide=<axis>` instead of `hinge=`. `slideDirLocal` is a
    /// unit vector in template-local space (e.g. {0,0,1} for `slide=z+`,
    /// {-1,0,0} for `slide=x-`).
    bool slide = false;
    glm::vec3 slideDirLocal{0.0f};
};

/// Planar projected-surface authoring metadata, parsed from a
/// `# surface: texture=<name> projection=planar axis=<x|y|z>` header.
/// When `texture` is non-empty the object is a Tier-2 decorated prop: a single
/// image is projected across its footprint along `axis` (rug, painting,
/// banner, mosaic). See docs/VoxelAppearanceModel.md §7 Phase 3.
struct VoxelSurface {
    std::string texture;        ///< Surface texture provider (a material name); empty = none.
    std::string projection = "planar";
    char        axis = 'y';     ///< 'x' | 'y' | 'z'
    bool active() const { return !texture.empty(); }
};

class VoxelTemplate {
public:
    std::string name;
    std::vector<TemplateCube> cubes;
    std::vector<TemplateSubcube> subcubes;
    std::vector<TemplateMicrocube> microcubes;

    /// Fine-grid tier (finer-than-microcube items). 0 = legacy C/S/M
    /// template. When > 0 (27 or 81 cells per cube edge), `fineVoxels`
    /// holds ALL geometry — mixing V with C/S/M lines is rejected at parse
    /// (one lattice per file keeps face culling and merging exact). Fine
    /// templates are kinematic-only: the static chunk-bake path refuses
    /// them because the 9-per-cube micro grid cannot represent finer cells.
    int fineGridResolution = 0;
    std::vector<TemplateFineVoxel> fineVoxels;
    bool isFineGrid() const { return fineGridResolution > 0; }

    /// Set when the file violates the format contract (V before # grid,
    /// V mixed with C/S/M, invalid grid value). loadTemplate() rejects the
    /// whole template so a broken file cannot half-load silently.
    bool parseError = false;
    std::string parseErrorReason;

    /// Composite parts declared via `# part:` directives. Always non-empty
    /// after load — index 0 is the implicit default part. Templates that
    /// never use the directive end up with exactly one entry, matching the
    /// pre-Phase-C0 behavior.
    std::vector<VoxelTemplatePart> parts;

    /// Canonical facing direction (radians, yaw) at rotation=0.
    /// Parsed from "# facing_yaw: X" header in the .txt file.
    /// yaw=0 → +Z, yaw=π → -Z (BlockBench front convention).
    float facingYaw = 0.0f;

    /// Semantic class parsed from a "# category:" header (e.g. "furniture",
    /// "nature", "building"). Drives whether the object is treated as
    /// knock-over/grab furniture or inert static scenery. Empty when the
    /// source file declares no category. See DynamicFurnitureManager.
    std::string category;

    /// Optional planar projected surface (rug/painting/banner). Parsed from a
    /// "# surface:" header. Inactive (empty texture) for ordinary templates.
    VoxelSurface surface;

    /// Interaction points parsed from "# interaction:" headers in the .txt file.
    std::vector<Core::InteractionPointDef> interactionPoints;

    /// Absolute path to the source .txt file (for saving back).
    std::string sourceFilePath;

    /// Ensure the implicit "default" part exists at index 0. Idempotent.
    /// Called lazily by the addCube/addSubcube/addMicrocube helpers so any
    /// caller that bypasses the parser still sees a valid `parts` vector.
    int ensureDefaultPart() {
        if (parts.empty()) {
            parts.push_back(VoxelTemplatePart{});  // name="default", movable=false
        }
        return 0;
    }

    /// Currently-active part index for the parser. Updated by `# part:`
    /// directives; defaults to 0 (the implicit default part). Not persisted
    /// — only used during load.
    int currentPartId = 0;

    void addCube(const glm::ivec3& pos, const std::string& mat, uint32_t tint = 0xFFFFFFu) {
        ensureDefaultPart();
        cubes.push_back({pos, mat, currentPartId, tint});
    }

    void addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subPos, const std::string& mat, uint32_t tint = 0xFFFFFFu, uint8_t state = 0) {
        ensureDefaultPart();
        subcubes.push_back({parentPos, subPos, mat, currentPartId, tint, state});
    }

    void addMicrocube(const glm::ivec3& parentPos, const glm::ivec3& subPos, const glm::ivec3& microPos, const std::string& mat, uint32_t tint = 0xFFFFFFu, uint8_t state = 0) {
        ensureDefaultPart();
        microcubes.push_back({parentPos, subPos, microPos, mat, currentPartId, tint, state});
    }

    void addFineVoxel(const glm::ivec3& pos, const std::string& mat, uint32_t tint = 0xFFFFFFu, uint8_t state = 0) {
        ensureDefaultPart();
        fineVoxels.push_back({pos, mat, currentPartId, tint, state});
    }
};

} // namespace Phyxel
