#pragma once
// docs/MaterialSimPlan.md §6a — MatSim P1a: the CPU reference material cellular automaton.
//
// Pure C++, headless, no engine dependencies. This is the SOURCE OF TRUTH the P1b GPU port must
// match bit-for-bit. Nothing in the engine calls it yet (P1b adds the /api/matsim lab routes);
// MatSim is inert until a lab box is created.
//
// Model (§6a):
//  - A lab box of cells, axis-aligned in world space, at cells_per_m in {1, 3, 9}.
//  - One uint32 per cell: material (8 bits) | fill (24 bits). Material 0 = empty (fill 0),
//    material 255 = SOLID. Fill is integer and nests exactly across cell sizes:
//    FULL(1/9 m) = 256, FULL(1/3 m) = 6,912, FULL(1 m) = 186,624.
//  - Margolus 2x2x2 block update on ping-pong grids: a block reads the previous grid and writes
//    only its own 8 cells, so the result is order-independent and every rule conserves mass by
//    construction. Block parity alternates by tick and is taken from WORLD cell coordinates;
//    every choice (pair order, remainders) hashes (world cell, tick, seed). Chunks never enter.
//  - Sleep is cost only: 8^3 world-aligned bricks skip when they and their 26 neighbours moved
//    nothing on both of the last two ticks; a skipped block writes its input back, which is
//    exactly what an evaluated ineligible block does.
//  - Outside the box is SOLID (a closed lab). Unknown occupancy is SOLID and counted.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <atomic>
#include <vector>

namespace Phyxel::Core::MatSim {

using Cell = uint32_t;

constexpr uint8_t kEmpty = 0;
constexpr uint8_t kSolid = 255;
constexpr uint32_t kFillMask = 0x00FFFFFFu;
constexpr uint32_t kUnitsPerMicro = 256;   ///< U: fill units in one full 1/9 m cell

inline uint8_t cellMaterial(Cell c) { return static_cast<uint8_t>(c >> 24); }
inline uint32_t cellFill(Cell c) { return c & kFillMask; }
inline Cell makeCell(uint8_t material, uint32_t fill) {
    return (static_cast<uint32_t>(material) << 24) | (fill & kFillMask);
}

/// Units in a full cell at this resolution: 256 * (9 / cells_per_m)^3. 0 for an invalid size.
uint32_t fullUnits(int cellsPerM);
bool validCellsPerM(int cellsPerM);

enum class Phase : uint8_t { Empty, Liquid, Powder, Solid };

struct MaterialDef {
    std::string name;
    Phase phase = Phase::Empty;
    /// Angle of repose as tan(theta) in 1/1000 (powders). Liquids: 0 (they level).
    /// Grounded values: Wikipedia "Angle of repose" table (T. J. Glover, Pocket Ref, 1995;
    /// snow: Rikkers & Rodriguez, Telluride Magazine 2009), fetched 2026-11-03.
    uint32_t reposeTanMilli = 0;
};

/// The MatSim material table (P1): Empty, Water, Sand (34 deg), Snow (38 deg), Gravel (45 deg),
/// SOLID. Ids are stable for the lab; names match resources/materials.json where it has them.
const std::array<MaterialDef, 256>& materialTable();
/// Id by name; returns false for an unknown name (no fallback material).
bool materialId(const std::string& name, uint8_t& out);

namespace Mat {
constexpr uint8_t Water = 1;
constexpr uint8_t Sand = 2;
constexpr uint8_t Snow = 3;
constexpr uint8_t Gravel = 4;
} // namespace Mat

/// Occupancy at MICRO (1/9 m) world coordinates.
enum class Occ : uint8_t { Empty, Solid, Unknown };
using MicroSolidFn = std::function<Occ(int mx, int my, int mz)>;

struct IVec3 {
    int x = 0, y = 0, z = 0;
    bool operator==(const IVec3& o) const { return x == o.x && y == o.y && z == o.z; }
};

/// Per-material fill totals (grid + trapped), uint64.
using Ledger = std::array<uint64_t, 256>;

struct StepStats {
    int ticks = 0;
    uint64_t eligibleBlocks = 0;   ///< blocks that moved anything, summed over the ticks
    uint64_t evaluatedBlocks = 0;  ///< blocks actually evaluated (not skipped by sleep)
    uint64_t lastTickEligible = 0; ///< eligible blocks on the final tick
    uint64_t headChanges = 0;      ///< §6d: cells whose head word changed, summed over the ticks
    uint64_t lastTickHeadChanges = 0;
    uint64_t pressurePassesSkipped = 0;   ///< 6k: ticks whose head field was provably unchanged (quiet last tick), so not rebuilt
    uint64_t lastTickHeadBricks = 0;      ///< 6l diagnostic: bricks holding a head change on the final tick
    uint64_t lastTickMovedBricks = 0;     ///< 6l diagnostic: bricks marked (moved or head change) on the final tick
    uint64_t bricks = 0;                  ///< bricks in the box
    uint64_t lastTickPressureBricks = 0;  ///< 6l: bricks the pressure pass computed on the final tick (0 = skipped)
    double pressureMs = 0.0;              ///< 6l diagnostic: wall time in the pressure pass, summed over the ticks
    /// At rest on the final tick: nothing moved and no head changed.
    bool lastTickQuiet() const { return lastTickEligible == 0 && lastTickHeadChanges == 0; }
};

struct FillResult {
    uint64_t cellsWritten = 0;
    uint64_t unitsWritten = 0;
    uint64_t solidSkipped = 0;
};

struct Config {
    uint32_t seed = 1;
    bool sleep = true;
    /// §6d: the hydrostatic pressure rule (head field + pressure-driven flow). Off reproduces the
    /// P1 rules exactly (the A/B control: a U-tube never levels without it).
    bool pressure = true;
    /// 6l: under `sleep`, rebuild the head field only in bricks whose inputs changed since the last pass (exact; off =
    /// the whole box every tick, the A/B control).
    bool pressureIncremental = true;
    /// Ticks per second the box is stepped at; only the orifice flow cap (Torricelli) uses it.
    uint32_t tickHz = 60;
    /// §6j: the thin-film floor, a fraction of a full cell (0 = off, the old rules exactly). Liquid
    /// on a solid surface does not spread thinner than this: the level rule over a 2x2 layer that
    /// rests on solid gathers the water into the fullest cells instead (a bead), and the slide rule
    /// moves nothing into an empty cell on solid unless it brings at least this much. Falls, the
    /// pressure rule and water over water are untouched. Host CA only (the GPU port refuses it).
    float filmFloor = 0.0f;
    /// TEST-ONLY control: the first gravity move after arming drops exactly one unit, so the
    /// ledger test can prove it sees a loss. Never set outside tests.
    bool testLeakOneUnit = false;
};

class MatSimCore {
public:
    MatSimCore() = default;

    /// Create a box from world-cell coordinates at `cellsPerM`. Refuses (returns false + reason,
    /// no partial create) for an invalid size, > 16,777,216 cells, a non-positive extent, or any
    /// UNKNOWN micro inside the box.
    bool create(IVec3 originCell, IVec3 sizeCells, int cellsPerM, const MicroSolidFn& solid,
                std::string* reason = nullptr, Config cfg = {});
    /// The same in metres: min and size must land on the cell lattice.
    bool createMetres(double minX, double minY, double minZ, double sizeX, double sizeY, double sizeZ,
                      int cellsPerM, const MicroSolidFn& solid, std::string* reason = nullptr,
                      Config cfg = {});
    void clear();
    bool created() const { return created_; }

    /// Write `material` at `fraction` of a full cell into every non-solid cell of the world-cell
    /// box [lo, lo+size). Overwrites what was there (the expected ledger follows).
    FillResult fillBox(uint8_t material, IVec3 loCell, IVec3 sizeCells, double fraction);
    /// Set one cell directly (non-solid only). Returns false for a solid or out-of-box cell.
    bool setCell(IVec3 worldCell, uint8_t material, uint32_t fill);

    StepStats step(int n = 1);
    /// 6k: make the listed EMPTY cells walls (solid) until unblockAll(). Other cells are skipped; they hold nothing,
    /// so the ledger is untouched. Returns the number blocked. Used to let the CA see cells held by particles.
    size_t blockEmpty(const std::vector<IVec3>& worldCells);
    void unblockAll();

    /// Re-read the solid mask. A cell turning solid while holding fill keeps it TRAPPED (frozen,
    /// in the ledger); a trapped cell turning open gets its contents back.
    void resolidify(const MicroSolidFn& solid);

    /// Switch cell size live with an exact conserving remap (§3a): split bottom-up, merge = sum.
    /// Refuses (no change) for an invalid size, a box not on the new lattice, a merge whose
    /// children hold two different materials, or > 16.7 M cells.
    bool setCellSize(int cellsPerM, const MicroSolidFn& solid, std::string* reason = nullptr);

    Ledger ledger() const;                 ///< computed by scanning the grid + trapped cells
    const Ledger& expectedLedger() const { return expected_; } ///< maintained by edits only
    uint64_t trappedUnits() const;
    uint64_t unknownCells() const { return unknownCells_; }
    uint64_t tick() const { return tick_; }
    /// 6l: bumped by every change to the state a device copy holds (cells, marks, head/pointers, config, box).
    /// Equal generations = no change in between: the hybrid lab skips re-uploading a device CA that is current.
    uint64_t generation() const { return gen_; }

    int cellsPerM() const { return cpm_; }
    uint32_t full() const { return full_; }
    IVec3 origin() const { return origin_; }
    IVec3 size() const { return size_; }
    bool inBox(IVec3 w) const;
    /// Cell at a world cell coordinate; SOLID outside the box.
    Cell at(IVec3 w) const;
    const std::vector<Cell>& cells() const { return cur_; }
    Config& config() { bumpGen(); return cfg_; }   // 6l: a writable config counts as a change (generation())
    const Config& config() const { return cfg_; }

    // --- P1b: the GPU port (MatSimGpu) steps a device copy of this state and hands it back. ---
    IVec3 brickLo() const { return brickLo_; }
    IVec3 brickDim() const { return brickDim_; }
    const std::vector<uint8_t>& marksTick1() const { return marks1_; }   ///< bricks moved on tick-1
    const std::vector<uint8_t>& marksTick2() const { return marks2_; }   ///< bricks moved on tick-2
    /// Take back a state stepped elsewhere: cells, tick and the two sleep mark sets. Sizes must
    /// match this box (false and no change otherwise). Trapped contents and the expected ledger
    /// are untouched: stepping never reads or writes a solid cell.
    bool adoptStepped(std::vector<Cell> cells, uint64_t tick, std::vector<uint8_t> marks1,
                      std::vector<uint8_t> marks2, std::vector<uint32_t> headPtr,
                      std::vector<uint32_t> head, std::vector<uint32_t> maxPtr);
    /// 6l: the same from raw arrays of this box's sizes (cells, head, pointers: one word per cell; marks: one word
    /// per brick, nonzero = marked), copied into the core's own storage with no allocation. Validates first:
    /// false and no change on a bad pointer.
    bool adoptSteppedFrom(const Cell* cells, uint64_t tick, const uint32_t* marks1, const uint32_t* marks2,
                          const uint32_t* headPtr, const uint32_t* head, const uint32_t* maxPtr);

    // --- §6d: the pressure field (three words per cell, recomputed every tick). ---
    /// ROUTE pointer: (source box index << 8) | hops, or kNoHead. The nearest free surface this
    /// cell reaches through liquid that stands within the deadband of its head: where a hole
    /// travelling upstream is absorbed. Hole routing uses its hops.
    const std::vector<uint32_t>& headPtr() const { return q_; }
    /// MAX pointer, same packing: the HIGHEST free surface this cell reaches (ties: fewest hops,
    /// then lowest index). Its elevation is the head, the same through a whole connected body.
    const std::vector<uint32_t>& headMaxPtr() const { return m_; }
    /// bit 31 = closed above (liquid or solid over it), bit 30 = has a full liquid face
    /// neighbour, bits 0..29 = head + 1 (0 = none), in fill units above the box floor:
    /// (y - origin.y) * FULL + fill of the source cell.
    const std::vector<uint32_t>& head() const { return h_; }
    uint32_t pressureDeadband() const { return full_ >= 128 ? full_ / 64 : 2; }
    /// Orifice cap constant: units per tick = (K * isqrt(head difference)) >> 10.
    uint32_t orificeK() const { return orificeK_; }

private:
    size_t index(int lx, int ly, int lz) const {
        return static_cast<size_t>(lx) + static_cast<size_t>(size_.x) *
               (static_cast<size_t>(lz) + static_cast<size_t>(size_.z) * static_cast<size_t>(ly));
    }
    Cell readPrev(int wx, int wy, int wz) const;
    /// §6j diagnostics: film gathers that changed a layer, and slides the floor held back (since create).
    uint64_t filmGathers_ = 0, filmHeld_ = 0;
public:
    uint64_t filmGathers() const { return filmGathers_; }
    uint64_t filmHeld() const { return filmHeld_; }
private:
    bool evalBlock(int bx, int by, int bz, int parity);   ///< reads cur_, writes nxt_; returns eligible
    uint64_t pressurePass(std::vector<uint8_t>& fresh);   ///< q_/h_ for this tick; marks changed bricks; returns changed cells
    void resetPressure();
    void copyBlock(int bx, int by, int bz);
    void markBricksOfBlock(int bx, int by, int bz, std::vector<uint8_t>& marks) const;
    int brickOf(int wx, int wy, int wz) const;            ///< index into brick arrays
    void wakeAll();
    uint64_t gen_ = 0;                                    ///< 6l: generation()
    /// Generations are unique across all cores (a process-wide counter): equal generations = the same state,
    /// even for a copy of a core or a new core at the same address.
    void bumpGen() { static std::atomic<uint64_t> next{0}; gen_ = ++next; }
    size_t lastPressureBricks_ = 0;                       ///< 6l: bricks computed by the last pressure pass
    std::vector<std::vector<uint32_t>> srcBricks_;        ///< 6l: per brick, the bricks its cells' head pointers name
    std::vector<IVec3> blocked_;                          ///< 6k: cells made solid by blockEmpty()
    void wakeCells(IVec3 lo, IVec3 hi);                   ///< inclusive world-cell range
    bool solidCell(const MicroSolidFn& fn, int wx, int wy, int wz, int cpm, bool& unknown) const;

    bool created_ = false;
    Config cfg_;
    int cpm_ = 0;
    uint32_t full_ = 0;
    IVec3 origin_{}, size_{};
    std::vector<Cell> cur_, nxt_;
    std::map<size_t, Cell> trapped_;   ///< box index -> contents frozen inside a solid cell
    Ledger expected_{};
    uint64_t tick_ = 0;
    uint64_t unknownCells_ = 0;
    // Sleep: bricks of 8^3 world cells covering the box (+1 brick margin each side for blocks
    // that start one cell outside the box).
    IVec3 brickLo_{}, brickDim_{};
    std::vector<uint8_t> marks1_, marks2_;   ///< moved on tick-1 / tick-2
    std::vector<uint32_t> q_, qn_, h_;       ///< §6d route pointers (q_ ping-pongs with qn_), head words
    std::vector<uint32_t> m_, mn_;           ///< §6d max pointers (ping-pong)
    uint32_t orificeK_ = 0;
};

constexpr uint32_t kNoHead = 0xFFFFFFFFu;
constexpr uint32_t kHeadClosed = 0x80000000u;
constexpr uint32_t kHeadFullNbr = 0x40000000u;
constexpr uint32_t kHeadMask = 0x3FFFFFFFu;
/// Floor integer square root, bit by bit: the CPU and the GPU run this exact algorithm.
uint32_t isqrt32(uint32_t v);
/// Discharge coefficient of an opening in a tank wall, x1000. Wikipedia "Torricelli's law"
/// (citing tec-science 2019): "in the order of 0.65" for round holes, up to 0.67 for
/// rectangular ones. Fetched 2026-11-04.
constexpr uint32_t kDischargeMilli = 650;
uint32_t orificeConstant(uint32_t full, int cellsPerM, uint32_t tickHz);

/// Deterministic 32-bit hash of (world cell, tick, seed) — the only source of "choice".
uint32_t cellHash(int x, int y, int z, uint64_t tick, uint32_t seed);
/// The tick's part of cellHash, as the GPU receives it: low word ^ high word * 0xfd7046c5.
inline uint32_t tickMix(uint64_t tick) {
    return static_cast<uint32_t>(tick) ^ (static_cast<uint32_t>(tick >> 32) * 0xfd7046c5u);
}
/// Rule 3's drop threshold in fill units: tan(theta) x distance x FULL, integer, exactly as the
/// CPU applies it. The GPU's material table is filled from this, so the two cannot drift.
uint32_t slideThreshold(uint8_t material, uint32_t full, bool diagonal);

} // namespace Phyxel::Core::MatSim
