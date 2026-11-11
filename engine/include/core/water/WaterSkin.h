#pragma once

#include "core/water/WaterCore.h"
#include "core/water/WaterSurfaceMesh.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

// WaterCore §51.4 S3 - the far SKIN (docs/WaterCore.md 51.4): recorded water beyond the record tiles (s_locality.drawM) drawn as
// coarse surface fields - the pond surface's own mesh, tiler, shader and material - on clipmap rings whose cell doubles with
// distance, built from a SUMMARY of the record per 4 x 4 m world cell.
//
//   the record (spans) --summarize--> 4 m cells, kept per chunk column (a storage unit: no cell straddles one)
//                      --rings------> per level k (cell base x 2^k, N x N round the viewer), one field per world tile of 36 cells
//
// A cell's look is a function of world position and the record alone: the 4 m cells are world-aligned, a coarser cell aggregates
// its world-aligned children (wet when at least half its columns are, its top their highest), a field is a world tile. The camera
// decides only WHICH level draws a place (cost), never what a level's cell holds.
namespace Phyxel::Core::Water {

constexpr int kSkinCellM = 4;            ///< the summary's cell (m): divides the record tile (8) and the chunk (32)
constexpr int kSkinCellsPerChunk = 8;    ///< 32 / 4

/// One summary cell: `wet` = its wet world columns (a 4 m cell holds 16; an aggregate up to its area), the highest run top and
/// the lowest run bottom over them (+-1e30 when dry), and the rectangle they span. A drawn cell covers that rectangle only, and is
/// drawn when at least half the rectangle is wet: a straight shore lies exactly on the shore at any cell size; two waters far apart
/// in one coarse cell (a sparse rectangle) are not joined into one sheet.
struct SkinCell {
    int wet = 0;
    float top = -1e30f, bottom = 1e30f;
    int x0 = 0, z0 = 0, x1 = -1, z1 = -1;   ///< the wet columns' extent (world columns, inclusive; x1 < x0 when dry)
    bool operator==(const SkinCell& o) const {
        return wet == o.wet && top == o.top && bottom == o.bottom && (wet == 0 || (x0 == o.x0 && z0 == o.z0 && x1 == o.x1 && z1 == o.z1));
    }
};

/// Adds the runs' columns into 4 m cells keyed by world cell index (floor division). A column is wet when one of its runs is at
/// least 1 mm thick (the surface extraction's own floor); its top is its highest such run's, its bottom its lowest.
void summarizeSkinCells(const std::vector<ColumnRuns>& runs, std::map<std::pair<int, int>, SkinCell>& out);

class WaterSkin {
public:
    struct Settings {
        bool on = false;              ///< ships OFF until the owner signs it off (51.4 item 6)
        float farM = 1024.0f;         ///< the skin's reach (m, horizontal); cells whose centre is farther are not drawn
        int baseCellM = 4;            ///< level 0's cell: 4 or 8 (a level-0 cell must lie inside one 8 m record tile, or the seam moves)
        int ringCells = 64;           ///< N: cells per side of a level (a multiple of 4, so each level sits on the next one's lattice)
        int summariesPerCall = 32;    ///< chunk columns summarized per call, nearest first (bounds a frame; late, never blank)
    };
    static inline bool s_wholeCellsForTest = false;   ///< TEST ONLY: the red control - a cell drawn whole when half its AREA is wet
    struct Stats {
        int levels = 0, fields = 0, wetCells = 0, ringCells = 0;
        int summaries = 0, summariesBuiltLastCall = 0, pending = 0, fieldsRebuiltLastCall = 0;
        long summariesBuilt = 0;
        double msLastCall = 0.0, msMax = 0.0;
    };
    using Reader = std::function<void(const glm::ivec3&, const glm::ivec3&, std::vector<ColumnRuns>&)>;
    struct Inputs {
        glm::vec3 view{0.0f};
        float drawM = 48.0f;                  ///< the record tiles' radius
        bool recordTilesOn = true;            ///< false: no record tiles are drawn, the skin masks none
        int recordTileM = 8;
        const Reader* reader = nullptr;       ///< the record (null: the skin draws nothing)
        uint64_t spanRevision = 0;            ///< any change: the summaries held are re-read (nearest first)
        uint64_t residentGlobal = 0;          ///< 32.4: the resident set's stamp - when it moves, each summary's column stamp is asked
        const std::function<uint64_t(int, int)>* residentColumn = nullptr;
        const std::vector<std::pair<glm::ivec3, glm::ivec3>>* volumeBoxes = nullptr;   ///< live volumes draw their own columns
        uint64_t volumeBoxesRevision = 0;
        std::function<WaterLookPacked(int, int, float)> look;   ///< the body's look at a world column
    };
    static constexpr int kYLo = -128, kYHi = 1023;   ///< the band a chunk column's summary reads (chunks y -4..31)
    static constexpr int kMaxLevels = 8;
    static constexpr int kSourceBit = 0x20000000;    ///< a skin field's source (record tiles: 0x40000000; volumes count up from 1)
    static bool isSkinField(const WaterSurfaceField& f) { return f.source > 0 && (f.source & kSourceBit) != 0 && (f.source & 0x40000000) == 0; }

    void update(const Settings& s, const Inputs& in);
    const std::vector<const WaterSurfaceField*>& fields() const { return m_out; }
    const Stats& stats() const { return m_stats; }
    /// what the skin draws at world (x, z): the drawn top (false = the skin draws nothing here); `claims` = how many skin fields do
    bool drawnAt(float x, float z, float& top, int* claims = nullptr) const;
    /// the summary's 4 m cell (cx, cz) (false: its chunk column is not summarized)
    bool cellAt(int cx, int cz, SkinCell& out) const;
    void clear();

private:
    // per chunk column: the 4 m cells and the 8 / 16 / 32 m aggregates above them (8x8 + 4x4 + 2x2 + 1)
    struct Summary {
        SkinCell cells[85];
        uint64_t spanRev = ~0ull, res = ~0ull;
        bool built = false;
        uint64_t version = 0;
    };
    static constexpr int kPyrOffset[4] = {0, 64, 80, 84};
    std::unordered_map<uint64_t, Summary> m_sum;
    uint64_t m_resSeen = ~0ull;
    uint64_t m_sumVersion = 0;               // bumped whenever a summary's cells change
    void buildSummary(int ccx, int ccz, Summary& s, const Reader& reader);
    bool aggregate(int level, int sizeM, long gx, long gz, SkinCell& out) const;   // the cell of size sizeM at world cell index (gx, gz)

    struct FieldEntry { WaterSurfaceField field; bool wetAny = false; };
    std::map<std::tuple<int, int, int>, FieldEntry> m_fieldMap;   // (level, tile x, tile z)
    std::vector<const WaterSurfaceField*> m_out;
    uint64_t m_fieldVersion = 0;
    struct Key {
        glm::vec3 view{0.0f}; float drawM = 0.0f, farM = 0.0f; int base = 0, n = 0; bool rec = false; uint64_t sumVersion = 0, boxRev = 0;
        bool operator==(const Key& o) const {
            return view == o.view && drawM == o.drawM && farM == o.farM && base == o.base && n == o.n && rec == o.rec && sumVersion == o.sumVersion && boxRev == o.boxRev;
        }
    };
    Key m_key; bool m_keyValid = false;
    struct LevelKey {
        long gx0 = 0, gz0 = 0, hx0 = 0, hz0 = 0; uint64_t mask = 0; bool valid = false;
        bool operator==(const LevelKey& o) const { return valid && o.valid && gx0 == o.gx0 && gz0 == o.gz0 && hx0 == o.hx0 && hz0 == o.hz0 && mask == o.mask; }
    };
    std::vector<LevelKey> m_levelKeys;
    long m_maskTiles[4] = {0, -1, 0, -1};   // level 0's tiles under the record mask (x0, x1, z0, z1)
    // the summary scan's inputs (it runs only when one moved, or work is pending)
    bool m_scanValid = false; long m_scanCx = 0, m_scanCz = 0; uint64_t m_scanSpanRev = 0, m_scanResGen = 0, m_resGen = 0; float m_scanReach = 0.0f; int m_lastPending = 0;
    Stats m_stats;
};

}  // namespace Phyxel::Core::Water
