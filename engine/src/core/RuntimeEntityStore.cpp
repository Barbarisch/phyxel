#include "core/RuntimeEntityStore.h"

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {
namespace RuntimeEntityStore {

static const char* kCreateSql = R"(
    CREATE TABLE IF NOT EXISTS runtime_entities (
        uuid      TEXT PRIMARY KEY,
        id        TEXT,
        type      TEXT,
        anim_file TEXT,
        x REAL, y REAL, z REAL
    );
)";

bool saveToDb(sqlite3* db, const std::vector<RuntimeEntity>& entities) {
    if (!db) return false;

    char* err = nullptr;
    sqlite3_exec(db, kCreateSql, nullptr, nullptr, &err);
    if (err) {
        LOG_ERROR("RuntimeEntityStore", "create table failed: {}", err);
        sqlite3_free(err);
        return false;
    }

    // The in-memory set is authoritative at save time: clear then re-insert so a
    // removed entity does not linger (and a moved one gets its new position).
    sqlite3_exec(db, "DELETE FROM runtime_entities;", nullptr, nullptr, nullptr);

    const char* insSql =
        "INSERT OR REPLACE INTO runtime_entities (uuid,id,type,anim_file,x,y,z) VALUES (?,?,?,?,?,?,?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, insSql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("RuntimeEntityStore", "prepare insert failed: {}", sqlite3_errmsg(db));
        return false;
    }

    bool ok = true;
    for (const auto& e : entities) {
        sqlite3_bind_text(stmt, 1, e.uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, e.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, e.type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, e.animFile.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, e.position.x);
        sqlite3_bind_double(stmt, 6, e.position.y);
        sqlite3_bind_double(stmt, 7, e.position.z);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOG_ERROR("RuntimeEntityStore", "insert failed for {}: {}", e.uuid, sqlite3_errmsg(db));
            ok = false;
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    if (ok) LOG_INFO("RuntimeEntityStore", "Saved {} runtime entities", entities.size());
    return ok;
}

std::vector<RuntimeEntity> loadFromDb(sqlite3* db) {
    std::vector<RuntimeEntity> out;
    if (!db) return out;

    sqlite3_exec(db, kCreateSql, nullptr, nullptr, nullptr);

    const char* selSql = "SELECT uuid,id,type,anim_file,x,y,z FROM runtime_entities;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, selSql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("RuntimeEntityStore", "prepare select failed: {}", sqlite3_errmsg(db));
        return out;
    }

    auto col = [&](int c) {
        const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, c));
        return t ? std::string(t) : std::string();
    };
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RuntimeEntity e;
        e.uuid = col(0);
        e.id = col(1);
        e.type = col(2);
        e.animFile = col(3);
        e.position = glm::vec3(static_cast<float>(sqlite3_column_double(stmt, 4)),
                               static_cast<float>(sqlite3_column_double(stmt, 5)),
                               static_cast<float>(sqlite3_column_double(stmt, 6)));
        out.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);

    if (!out.empty()) LOG_INFO("RuntimeEntityStore", "Loaded {} runtime entities", out.size());
    return out;
}

} // namespace RuntimeEntityStore
} // namespace Core
} // namespace Phyxel
