#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Phyxel {

/// WorldForge realization ledger (docs/WorldForge.md M2): WHICH planned sites have been
/// built into this world — the genuinely unrecomputable state ("store what you can't
/// recompute"), persisted at world.db world_meta["worldforge_ledger"]. The plan itself is
/// never stored (it re-bakes from the recipe); the ledger records outcomes against the
/// plan's hash so drift (params edited after sites were built) is detectable, never silent.
/// Header-only (WorldRecipe pattern).
struct WorldForgeLedger {
    struct SiteEntry {
        int id = -1;
        std::string tier;
        std::string status = "pending";   // "pending" | "built" | "refused"
        std::string error;                // refusal reason (grounding, residency_timeout, ...)
        int buildings = 0;                // queued building count from the settlement plan
        int lotFailures = 0;
        int residents = 0;
    };

    uint64_t planHash = 0;
    std::vector<SiteEntry> sites;

    SiteEntry* find(int id) {
        for (auto& s : sites)
            if (s.id == id) return &s;
        return nullptr;
    }
    SiteEntry& ensure(int id, const std::string& tier) {
        if (SiteEntry* e = find(id)) return *e;
        SiteEntry e;
        e.id = id;
        e.tier = tier;
        sites.push_back(std::move(e));
        return sites.back();
    }
    /// True when the stored hash no longer matches the live plan — sites were built against
    /// a DIFFERENT plan (params/seed edited since). Callers surface this, never overwrite.
    bool stale(uint64_t livePlanHash) const { return planHash != 0 && planHash != livePlanHash; }

    std::string toJson() const {
        nlohmann::json root;
        root["planHash"] = planHash;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& s : sites)
            arr.push_back({{"id", s.id}, {"tier", s.tier}, {"status", s.status},
                           {"error", s.error}, {"buildings", s.buildings},
                           {"lotFailures", s.lotFailures}, {"residents", s.residents}});
        root["sites"] = arr;
        return root.dump();
    }

    static WorldForgeLedger fromJson(const std::string& s) {
        WorldForgeLedger l;
        try {
            nlohmann::json root = nlohmann::json::parse(s);
            l.planHash = root.value("planHash", 0ull);
            if (root.contains("sites") && root["sites"].is_array())
                for (const auto& e : root["sites"]) {
                    SiteEntry se;
                    se.id = e.value("id", -1);
                    se.tier = e.value("tier", std::string());
                    se.status = e.value("status", std::string("pending"));
                    se.error = e.value("error", std::string());
                    se.buildings = e.value("buildings", 0);
                    se.lotFailures = e.value("lotFailures", 0);
                    se.residents = e.value("residents", 0);
                    l.sites.push_back(std::move(se));
                }
        } catch (...) {
            // Unreadable ledger -> empty (a fresh build re-records; chunks/objects are the
            // ground truth, the ledger is bookkeeping).
        }
        return l;
    }
};

}  // namespace Phyxel
