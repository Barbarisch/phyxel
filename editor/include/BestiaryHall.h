#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Phyxel::Core { class NPCManager; }

namespace Phyxel::Editor {

/// The Bestiary Hall: every distinct creature rig staged at once, labelled,
/// and able to play every clip it owns.
///
/// It exists because the bestiary outgrew the way it was being checked. 336
/// stat blocks ride ~46 rigs, and the only proof any of them still looked and
/// moved right was spawning them one at a time and eyeballing a screenshot.
/// The hall makes the whole library visible in one frame, which turns "does
/// the bestiary still work" into a question you can answer by looking.
///
/// Two deliberate choices:
///
/// * **The roster is generated, not hand-listed.** It loads
///   `resources/monsters/visuals/bestiary_hall.json`
///   (tools/creature_forge/gen_hall.py), so adding or retiring a rig updates
///   the hall for free and a hand-maintained list can't drift out of sync.
///
/// * **Selection ghosts the others rather than highlighting the one.** Every
///   unselected creature drops to a low alpha through the translucent
///   character pipeline; the selected one stays fully opaque. This reads at
///   any distance and in any lighting, which matters because character albedo
///   is currently crushed by the open sun-washout gap
///   (docs/StructurePipelineGaps.md 2026-08-21) — so "make the selected one
///   brighter" would have been fighting the very thing that is already broken.
class BestiaryHall {
public:
    /// One staged rig.
    struct Entry {
        std::string id;             ///< rig stem, e.g. "forge_hydra" — the stable key
        std::string name;           ///< display name, e.g. "Hydra (five necks)"
        std::string category;       ///< panel grouping only, no engine meaning
        std::string animFile;
        std::string representative; ///< a monsterId that uses this rig (for tint/faction)
        int   statBlocks = 0;       ///< how many SRD stat blocks ride this rig
        int   boxes = 0;
        int   bones = 0;

        // Measured bind-pose AABB, in rig units (see gen_hall.py).
        float width = 1.0f, height = 1.8f, depth = 1.0f, footY = 0.0f;

        /// Engine-resolved clip per state; EMPTY means this rig genuinely
        /// cannot play that state and the panel greys the button out.
        std::string clipIdle, clipWalk, clipAttack, clipDeath;

        // --- runtime, filled by spawn() ---
        std::string npcName;        ///< empty until staged
        glm::vec3   position{0.0f};
        bool        spawned = false;

        bool canPlay(const std::string& state) const;
    };

    /// Load the generated roster. Returns false (and sets lastError()) if the
    /// file is missing or malformed — the panel then shows the reason rather
    /// than an empty list, because "no creatures" and "roster failed to load"
    /// look identical otherwise.
    bool loadRoster(const std::string& path);

    /// Ground height at a world (x, z). Supplied by the host because the hall
    /// has no world access of its own.
    using GroundSampler = std::function<float(float x, float z)>;

    /// Stage every rig on a grid centred at `origin`, grouped by category.
    /// Despawns any previous staging first, so this is idempotent.
    ///
    /// `sampleGround` is queried PER CREATURE rather than once for the hall.
    /// A single shared floor only works on flat ground; on real terrain it
    /// buries the creatures standing downhill and floats the ones uphill.
    /// When null, `fallbackGroundY` is used for everything.
    bool spawn(Core::NPCManager* npcs, const glm::vec3& origin,
               float fallbackGroundY, const GroundSampler& sampleGround = {});

    /// Remove every staged creature.
    void despawn(Core::NPCManager* npcs);

    /// Select by rig id; empty string clears the selection. Applies the ghost
    /// pass immediately.
    void select(Core::NPCManager* npcs, const std::string& rigId);

    /// Drive one state ("Idle"/"Walk"/"Attack"/"Death"). When `allRigs` is
    /// false only the selection plays it. Rigs that cannot play the state are
    /// skipped rather than forced into a T-pose. Returns how many played.
    int playState(Core::NPCManager* npcs, const std::string& state, bool allRigs);

    /// Re-apply opacity for every staged creature. Called after spawn and on
    /// selection change; also safe to call any time the roster changes.
    void applyGhosting(Core::NPCManager* npcs) const;

    /// Camera framing for one entry: a position and target that fit the
    /// creature's measured height. Returns false for an unstaged id.
    bool focusView(const std::string& rigId, glm::vec3& outPos, glm::vec3& outTarget) const;

    const std::vector<Entry>& entries() const { return m_entries; }
    std::vector<Entry>&       entries()       { return m_entries; }
    const std::string& selected() const  { return m_selectedId; }
    const std::string& lastError() const { return m_lastError; }
    bool  isStaged() const { return m_staged; }
    const glm::vec3& origin() const { return m_origin; }

    /// Opacity given to creatures that are NOT selected while something is.
    /// 0.22 was picked by eye: low enough that the selection is unmistakable,
    /// high enough that you can still read the ghosts' silhouettes for scale.
    static constexpr float kGhostAlpha = 0.22f;

    /// Gap left between neighbours, in world units, on top of their measured
    /// half-widths. Creatures range from a 0.18-wide rodent to a 4.57-wide
    /// ancient dragon, so spacing has to come from the measurement.
    static constexpr float kGapX = 1.6f;
    static constexpr float kGapZ = 3.0f;

    /// Rows never exceed this width; a category wider than this wraps.
    static constexpr float kMaxRowWidth = 46.0f;

private:
    Entry* find(const std::string& rigId);
    const Entry* find(const std::string& rigId) const;

    std::vector<Entry> m_entries;
    std::string m_selectedId;
    std::string m_lastError;
    glm::vec3   m_origin{0.0f};
    float       m_groundY = 0.0f;
    bool        m_staged = false;
};

}  // namespace Phyxel::Editor
