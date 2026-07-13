#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <sqlite3.h>

namespace Phyxel {
namespace Core {

// ============================================================================
// RuntimeEntityStore — persistence for RUNTIME-spawned entities (spawn_entity),
// so they survive save/reload with the SAME stable uuid.
//
// Authored NPCs and the player come from game.json and are re-created there;
// they are NOT stored here. This table holds only the minimal "spawn recipe"
// needed to reconstruct an ad-hoc entity: its uuid + legacy id, the spawn type
// tag, the animation file it was built from, and its last position.
//
// Written ONLY during save_world (bundled with the chunk save), never eagerly —
// mirrors PlacedObjectManager's ghost-record discipline.
// ============================================================================
struct RuntimeEntity {
    std::string uuid;       ///< Stable RFC-4122 v4 identity (survives reload)
    std::string id;         ///< Legacy registry id at spawn time
    std::string type;       ///< Spawn type tag (e.g. "animated")
    std::string animFile;   ///< Animation file the character was created from
    glm::vec3   position{0.0f};
};

namespace RuntimeEntityStore {

/// Replace the runtime_entities table (keyed by uuid) with `entities`. Creates the
/// table if missing. Returns false on a DB error.
bool saveToDb(sqlite3* db, const std::vector<RuntimeEntity>& entities);

/// Load all runtime entities from the world DB (empty vector if none / no table).
std::vector<RuntimeEntity> loadFromDb(sqlite3* db);

} // namespace RuntimeEntityStore
} // namespace Core
} // namespace Phyxel
