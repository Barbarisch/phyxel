#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <sqlite3.h>

namespace Phyxel {
namespace Core {

class PlayerProfile {
public:
    // Camera state
    glm::vec3 cameraPosition{50.0f, 50.0f, 50.0f};
    float cameraYaw = -135.0f;
    float cameraPitch = -30.0f;

    // Health state
    float health = 100.0f;
    float maxHealth = 100.0f;

    // Respawn state
    glm::vec3 spawnPoint{16.0f, 25.0f, 16.0f};
    int deathCount = 0;

    // Inventory (stored as JSON blob)
    nlohmann::json inventoryData = nlohmann::json::array();

    nlohmann::json toJson() const {
        return {
            {"camera", {
                {"x", cameraPosition.x}, {"y", cameraPosition.y}, {"z", cameraPosition.z},
                {"yaw", cameraYaw}, {"pitch", cameraPitch}
            }},
            {"health", health},
            {"maxHealth", maxHealth},
            {"spawnPoint", {{"x", spawnPoint.x}, {"y", spawnPoint.y}, {"z", spawnPoint.z}}},
            {"deathCount", deathCount},
            {"inventory", inventoryData}
        };
    }

    void fromJson(const nlohmann::json& j) {
        if (j.contains("camera")) {
            auto& c = j["camera"];
            cameraPosition = glm::vec3(c.value("x", 50.0f), c.value("y", 50.0f), c.value("z", 50.0f));
            cameraYaw = c.value("yaw", -135.0f);
            cameraPitch = c.value("pitch", -30.0f);
        }
        health = j.value("health", 100.0f);
        maxHealth = j.value("maxHealth", 100.0f);
        if (j.contains("spawnPoint")) {
            auto& s = j["spawnPoint"];
            spawnPoint = glm::vec3(s.value("x", 16.0f), s.value("y", 25.0f), s.value("z", 16.0f));
        }
        deathCount = j.value("deathCount", 0);
        if (j.contains("inventory")) {
            inventoryData = j["inventory"];
        }
    }

    bool saveToDb(sqlite3* db, const std::string& playerId = "default") const {
        if (!db) return false;

        const char* createSql = R"(
            CREATE TABLE IF NOT EXISTS player_state (
                player_id TEXT PRIMARY KEY,
                profile_json TEXT NOT NULL,
                modified_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );
        )";
        char* err = nullptr;
        sqlite3_exec(db, createSql, nullptr, nullptr, &err);
        if (err) { sqlite3_free(err); return false; }

        const char* upsertSql = R"(
            INSERT OR REPLACE INTO player_state (player_id, profile_json, modified_at)
            VALUES (?, ?, datetime('now'));
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, upsertSql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        std::string jsonStr = toJson().dump();
        sqlite3_bind_text(stmt, 1, playerId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, jsonStr.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    bool loadFromDb(sqlite3* db, const std::string& playerId = "default") {
        if (!db) return false;

        // Ensure table exists (no-op if already there)
        const char* createSql = R"(
            CREATE TABLE IF NOT EXISTS player_state (
                player_id TEXT PRIMARY KEY,
                profile_json TEXT NOT NULL,
                modified_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );
        )";
        sqlite3_exec(db, createSql, nullptr, nullptr, nullptr);

        const char* selectSql = "SELECT profile_json FROM player_state WHERE player_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, selectSql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, playerId.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text) {
                auto j = nlohmann::json::parse(text, nullptr, false);
                if (!j.is_discarded()) {
                    fromJson(j);
                    ok = true;
                }
            }
        }
        sqlite3_finalize(stmt);
        return ok;
    }
};

} // namespace Core
} // namespace Phyxel
