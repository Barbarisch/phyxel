#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace Phyxel {

/// Per-world generation recipe, persisted in world.db (world_meta key "recipe").
/// Captures the per-world TUNING — seed, biome size, extremeness, flora — that overrides
/// the shared biome *category* library (resources/biomes.json). Once written, the DB is the
/// runtime source of truth for a world (reproducible, immune to global config edits).
/// See docs/WorldRecipeAndFlora.md. Header-only so the engine glob needs no reconfigure.
struct WorldRecipe {
    struct FloraItem {
        std::string templateName;
        int weight = 1;
    };
    struct BiomeTune {
        std::string name;
        float heightScale = 1.0f;          // terrain extremeness for this biome
        std::string floraMode = "pool";    // "pool" (stamp templates) | "procedural" (generate)
        float floraFullness = 0.85f;       // canopy density for procedural generation
        float floraDensity = 0.0f;
        int   floraSpacing = 6;
        std::vector<FloraItem> flora;
    };

    int version = 1;
    uint32_t seed = 0;
    std::string type = "Perlin";
    float climateFrequency = 0.002f;       // biome size (lower = bigger biomes)
    std::vector<BiomeTune> biomes;

    std::string toJson() const {
        nlohmann::json root;
        root["version"] = version;
        root["seed"] = seed;
        root["type"] = type;
        root["climateFrequency"] = climateFrequency;
        nlohmann::json barr = nlohmann::json::array();
        for (const auto& b : biomes) {
            nlohmann::json items = nlohmann::json::array();
            for (const auto& f : b.flora)
                items.push_back({{"template", f.templateName}, {"weight", f.weight}});
            barr.push_back({
                {"name", b.name},
                {"extremeness", {{"heightScale", b.heightScale}}},
                {"flora", {{"mode", b.floraMode}, {"fullness", b.floraFullness},
                           {"density", b.floraDensity}, {"spacing", b.floraSpacing},
                           {"items", items}}},
            });
        }
        root["biomes"] = barr;
        return root.dump(2);
    }

    static WorldRecipe fromJson(const std::string& s) {
        WorldRecipe r;
        try {
            nlohmann::json root = nlohmann::json::parse(s);
            r.version = root.value("version", 1);
            r.seed = root.value("seed", 0u);
            r.type = root.value("type", std::string("Perlin"));
            r.climateFrequency = root.value("climateFrequency", 0.002f);
            if (root.contains("biomes") && root["biomes"].is_array()) {
                for (const auto& b : root["biomes"]) {
                    BiomeTune bt;
                    bt.name = b.value("name", "");
                    if (b.contains("extremeness"))
                        bt.heightScale = b["extremeness"].value("heightScale", 1.0f);
                    if (b.contains("flora")) {
                        const auto& f = b["flora"];
                        bt.floraMode = f.value("mode", std::string("pool"));
                        bt.floraFullness = f.value("fullness", 0.85f);
                        bt.floraDensity = f.value("density", 0.0f);
                        bt.floraSpacing = f.value("spacing", 6);
                        if (f.contains("items") && f["items"].is_array())
                            for (const auto& it : f["items"])
                                bt.flora.push_back({it.value("template", ""), it.value("weight", 1)});
                    }
                    r.biomes.push_back(std::move(bt));
                }
            }
        } catch (...) {
            // Bad/empty input -> caller falls back to synthesizing from the category library.
        }
        return r;
    }
};

} // namespace Phyxel
