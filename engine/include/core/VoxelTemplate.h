#pragma once

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
};

struct TemplateSubcube {
    glm::ivec3 parentRelativePos;
    glm::ivec3 subcubePos;
    std::string material;
    int partId = 0;
};

struct TemplateMicrocube {
    glm::ivec3 parentRelativePos;
    glm::ivec3 subcubePos;
    glm::ivec3 microcubePos;
    std::string material;
    int partId = 0;
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

class VoxelTemplate {
public:
    std::string name;
    std::vector<TemplateCube> cubes;
    std::vector<TemplateSubcube> subcubes;
    std::vector<TemplateMicrocube> microcubes;

    /// Composite parts declared via `# part:` directives. Always non-empty
    /// after load — index 0 is the implicit default part. Templates that
    /// never use the directive end up with exactly one entry, matching the
    /// pre-Phase-C0 behavior.
    std::vector<VoxelTemplatePart> parts;

    /// Canonical facing direction (radians, yaw) at rotation=0.
    /// Parsed from "# facing_yaw: X" header in the .txt file.
    /// yaw=0 → +Z, yaw=π → -Z (BlockBench front convention).
    float facingYaw = 0.0f;

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

    void addCube(const glm::ivec3& pos, const std::string& mat) {
        ensureDefaultPart();
        cubes.push_back({pos, mat, currentPartId});
    }

    void addSubcube(const glm::ivec3& parentPos, const glm::ivec3& subPos, const std::string& mat) {
        ensureDefaultPart();
        subcubes.push_back({parentPos, subPos, mat, currentPartId});
    }

    void addMicrocube(const glm::ivec3& parentPos, const glm::ivec3& subPos, const glm::ivec3& microPos, const std::string& mat) {
        ensureDefaultPart();
        microcubes.push_back({parentPos, subPos, microPos, mat, currentPartId});
    }
};

} // namespace Phyxel
