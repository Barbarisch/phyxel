#pragma once

#include "core/water/WaterCore.h"
#include "core/water/WaterLook.h"

#include <functional>
#include <limits>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// WaterCore Phase D slice D1 (docs/WaterCore.md 16.2): Tier A, the body records. Persisted in
// world_meta["water_bodies"] as JSON. Generation bodies are imported from the bake's
// WaterBodyIndex (origin "generation", mass = the bake's estimate); active volumes credit their
// write-backs per column: a column the bake assigns to a body credits that body with
// (written - seeded); columns that belong to no bake body form or extend an "av" pond whose mass
// is EXACTLY the sum of its spans (P3: A.mass = sum of B over the body's columns, checked by
// verify()). Displaced water (a solid placed into a span, rule 3) is debited and remembered.
namespace Phyxel::Core::Water {

struct WaterBodyRecord {
    int         id = 0;              ///< generation: the bake's body id (>= 0); av ponds: negative, unique
    std::string cls = "pond";        ///< ocean | lake | pond | river | puddle
    float       level = 0.0f;        ///< world Y (mean run top for av ponds)
    double      mass = 0.0;          ///< m^3 (an ESTIMATE for generation bodies, exact for av ponds)
    double      displaced = 0.0;     ///< m^3 debited by solids placed into this body's spans
    glm::ivec2  bboxMin{0}, bboxMax{0};   ///< world voxel columns, inclusive
    std::string origin = "av";       ///< generation | av | region (G3: stored water named for its look - no mass role, never credited)
    WaterLook   look;                ///< G3: the body's look profile (unset knobs = derived); persisted only when set
};

class WaterBodyTable {
public:
    /// Import the bake's bodies (replaces previous generation rows, keeps av rows).
    void importGeneration(const std::vector<WaterBodyRecord>& bodies);

    /// Credit a write-back. `written` is the per-column mass that was written (held columns
    /// excluded), `seeded` the per-column mass those columns' spans held BEFORE the write (the
    /// record moves by the difference; a column with no prior record takes its full written mass),
    /// `bakeBodyAt(x, z)` the bake body id of a column or -1. Returns the id credited for the
    /// most columns (the record a caller may report), 0 when nothing was written.
    struct ColumnMass { int x = 0, z = 0; double mass = 0.0; float top = 0.0f; };
    int credit(const std::vector<ColumnMass>& written, const std::vector<ColumnMass>& seeded,
               const std::function<int(int, int)>& bakeBodyAt);

    /// Rule 3: a solid displaced `m3` of span water in column (x, z); debit the record that owns the
    /// column (bake body, else the av pond whose bbox holds it, else a new "displaced-only" row is
    /// NOT created - the loss is counted in `orphanDisplaced()` so the ledger stays honest).
    void displace(int x, int z, double m3, const std::function<int(int, int)>& bakeBodyAt);
    double orphanDisplaced() const { return m_orphanDisplaced; }

    const std::vector<WaterBodyRecord>& records() const { return m_records; }
    std::vector<WaterBodyRecord>& recordsMutable() { return m_records; }   ///< the reconcile route (A.mass := sum of B)
    const WaterBodyRecord* find(int id) const;
    WaterBodyRecord* findMutable(int id);
    /// G3: the body that owns a world column - the generation body under it (`bakeBodyAt`), else the
    /// av pond whose box holds it; kNoBody when neither. A pure function of the column and the table.
    static constexpr int kNoBody = -2147483647;
    /// `topY` (optional): the column's stored surface; a box-matched av/region record must sit within
    /// 5 cm of it, so a pond behind a beach (another level, inside the sea's box) is not the sea.
    int bodyAt(int x, int z, const std::function<int(int, int)>& bakeBodyAt, float topY = std::numeric_limits<float>::quiet_NaN()) const;
    /// G3: the look of the body that owns a column (all unset when no body or no record).
    WaterLook lookAt(int x, int z, const std::function<int(int, int)>& bakeBodyAt, float topY = std::numeric_limits<float>::quiet_NaN()) const;
    /// G3: name a region of stored water (origin "region", a fresh negative id). Returns the id.
    int addRegion(WaterBodyRecord r);
    double avMass() const;            ///< sum of av-origin masses (the ledger's `bodies`)
    double displacedTotal() const;    ///< sum of displaced over every record + orphan

    std::string serialize() const;    ///< JSON
    bool load(const std::string& json);
    size_t size() const { return m_records.size(); }

private:
    WaterBodyRecord* avPondAt(int x, int z);
    std::vector<WaterBodyRecord> m_records;
    int m_nextAvId = -1;
    double m_orphanDisplaced = 0.0;
};

}  // namespace Phyxel::Core::Water
