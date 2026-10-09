#pragma once

#include "core/water/SeaSwell.h"
#include "core/water/ShoreSolver.h"
#include "core/water/WaterSurfaceMesh.h"

#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// WaterCore Phase G (docs/WaterCore.md 18, 18.5): the shoreline band - a camera-following square
// of `ShoreSolver` columns whose outer ring is the ocean (prescribed from the sea sheet's own swell
// at the sheet's own clock) and whose interior is solved against the voxel bed: the swell shoals,
// breaks, runs up the sand and drains back. The band persists nothing (its water is the stored
// ocean's; nothing ever stays above the swash line) and it never creates water: every column
// starts from the stored spans or dry. Its surface is a Phase F field, drawn by the same mesh; the
// sheet and the span grid leave its columns to it (the D3 mask).
//
// Chunk independence (18.3 key 2): the box is camera-centred (coverage / cost, as terrain), the bed
// and the stored tops are world data, the boundary height is a function of world position and
// time; unknown ground (a non-resident chunk) is a wall - the 5.1 hold rule. The outer ring's width
// is a cost bound only (ShoreBandTest.InteriorIndependentOfOuterWidth).
namespace Phyxel::Core::Water {

struct ShoreBandParams {
    float radius = 24.0f;      ///< m: half-width of the square band around its centre (<= 64)
    int   inner = 12;          ///< columns: the prescribed ocean ring, measured from the box edge (>= 4)
    float ramp = 8.0f;         ///< m: the relaxation ramp at the ring's inner boundary (weight (q / ramp)^2, q = distance into the ring); the rest of the ring is hard ocean
    float cellSize = 1.0f;     ///< m per column (1 or 1/3)
    float runUp = 4.0f;        ///< m above the still level the band covers; higher land is a wall
    float maxDepth = 8.0f;     ///< m below the still level the bed scan reaches; deeper = a flat floor there
    float minOceanDepth = 1.0f;///< m: a ring column shallower than this is a sponge (still level, no velocity), not the wavemaker
    bool  seawardBias = false; ///< move the box centre toward the stored water (up to radius / 2): the ring sits in deeper water
    ShoreParams solver;
};

/// The highest solid's top face in the column within [yLo, yHi): `known` false when the column is
/// unknown (not resident / not packed) - then it is a wall; `top` = yLo when nothing solid is in range.
struct BedSample { bool known = false; float top = 0.0f; };
using BedQuery = std::function<BedSample(float worldX, float worldZ, float yLo, float yHi)>;
/// The stored water surface (world Y) of the voxel column's top run; `wet` false when dry or unknown.
struct StoredTop { bool wet = false; float top = 0.0f; };
using StoredTopQuery = std::function<StoredTop(int worldX, int worldZ)>;

enum class ShoreColumnRole : uint8_t { Free = 0, Prescribed = 1, Wall = 2, Sponge = 3 };

struct ShoreBandRecord {
    bool  on = false;
    glm::vec2 originXZ{0.0f};        ///< world XZ of column (0, 0)'s min corner
    int   n = 0;                     ///< columns per side
    float h = 1.0f;
    float still = 0.0f;              ///< the ocean's still level (world Y) the ring is driven about
    long  columns = 0, prescribed = 0, walls = 0, wet = 0, free = 0, sponge = 0;
    double mass = 0.0;               ///< m^3 in the band after the last tick
    double exchanged = 0.0;          ///< m^3 the ocean ring gave (+) / took (-) since siting
    float runUpMax = 0.0f;           ///< m above still: the highest wet free column's bed this tick (0 = none above)
    long  swashColumns = 0;          ///< free columns dry at rest (bed >= still) that are wet now
    float swashEtaMax = 0.0f;        ///< m above still: the highest surface over those columns this tick
    glm::vec2 centreXZ{0.0f};        ///< the box centre after the seaward bias
    float maxSpeed = 0.0f;           ///< m/s this tick (a blow-up tripwire)
    float meanFreeRise = 0.0f;       ///< m: mean surface of the wet free columns minus still (the pump tripwire)
    float runUpPeak = 0.0f;          ///< the same, since siting
    float foamMax = 0.0f;
    double stepMs = 0.0, fieldMs = 0.0, siteMs = 0.0;
    int   substeps = 0;
    int   sitings = 0;
    uint64_t revision = 0;           ///< bumps on every siting (the mask key)
    long  bedUpdates = 0;            ///< columns re-queried after an edit
};

class ShoreBand {
public:
    /// (Re)site the band around `centreXZ`. Refuses (false + err) when the box exceeds the column cap
    /// or the outer ring holds no stored water (there is no ocean to drive the shore). The still
    /// level is the ring's most common stored top (1 cm bins). Columns the previous siting also held
    /// carry their state over, so walking along the shore does not restart the swell.
    bool site(const glm::vec2& centreXZ, const ShoreBandParams& p, BedQuery bed, StoredTopQuery stored, std::string* err);
    /// True once the centre drifted more than radius / 2 from the band's centre.
    bool needsResite(const glm::vec2& centreXZ) const;
    /// One tick: the ring prescribed from the swell at the sheet's time, the solver stepped, the
    /// surface field rebuilt. `waveTime` is the sheet's clock (WaterRenderPipeline::waveTime()).
    void tick(float dt, float waveTime, const SeaSwellParams& swell);
    /// An edit under the band: the column's bed is re-queried at the next tick and the surface follows
    /// (never below the bed; water over a new hole falls to it). Outside the box: ignored.
    void noteEdit(int worldX, int worldZ);
    /// G3: the body's look for the band's surface (resolved by the caller at the band's centre).
    void setLook(const WaterLookPacked& look) { m_field.look = look; }
    void clear();

    bool active() const { return m_solver != nullptr; }
    const ShoreBandRecord& record() const { return m_rec; }
    const WaterSurfaceField& field() const { return m_field; }
    const ShoreSolver* solver() const { return m_solver.get(); }
    ShoreColumnRole role(int x, int z) const { return m_roles[static_cast<size_t>(x) + static_cast<size_t>(m_rec.n) * z]; }
    /// The voxel box whose columns the span grid / sheet leave to the band (x/z inclusive; y unused).
    std::pair<glm::ivec3, glm::ivec3> maskBox() const;
    glm::vec2 columnCentre(int x, int z) const { return m_rec.originXZ + glm::vec2((x + 0.5f) * m_rec.h, (z + 0.5f) * m_rec.h); }
    static constexpr long kMaxColumns = 65536;

private:
    void rebuildField();
    std::unique_ptr<ShoreSolver> m_solver;
    ShoreBandParams m_params;
    ShoreBandRecord m_rec;
    WaterSurfaceField m_field;
    std::vector<ShoreColumnRole> m_roles;
    std::vector<float> m_stillOfColumn;     // per prescribed column: the stored top it is driven about
    std::vector<float> m_weights;           // per prescribed column: the relaxation weight (1 at the edge -> ~0 inward)
    std::vector<std::pair<int, int>> m_dirty;   // columns to re-bed at the next tick
    BedQuery m_bed;
    glm::vec2 m_centre{0.0f};
};

}  // namespace Phyxel::Core::Water
