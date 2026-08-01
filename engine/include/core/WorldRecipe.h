#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "core/WorldConstants.h"

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
    // An additional vegetation band with its own spacing/density (sparse giants over a dense
    // understory). Mirrors WorldGenerator::Biome::FloraLayer for persistence.
    struct FloraLayerTune {
        float density = 0.0f;
        int   spacing = 6;
        std::string mode = "pool";
        float fullness = 0.85f;
        std::vector<FloraItem> items;
    };
    struct BiomeTune {
        std::string name;
        float heightScale = 1.0f;          // terrain extremeness for this biome
        std::string floraMode = "pool";    // "pool" (stamp templates) | "procedural" (generate)
        float floraFullness = 0.85f;       // canopy density for procedural generation
        float floraDensity = 0.0f;
        int   floraSpacing = 6;
        std::vector<FloraItem> flora;            // layer 0
        std::vector<FloraLayerTune> extraLayers; // additional bands
    };

    // A control point of the continentalness → base-elevation shaping spline (the "how tall"
    // art-direction curve; see WorldGenerator::m_continentalHeightSpline). x = continentalness [0,1],
    // y = base world-Y. Empty heightSpline → the engine's default ramp.
    struct SplinePoint {
        float x = 0.0f;
        float y = 0.0f;
    };

    int version = 1;
    uint32_t seed = 0;
    std::string type = "Perlin";
    float climateFrequency = 0.002f;       // biome size (lower = bigger biomes)
    // Sea level the world was BAKED against (hydrology outlet + seabed/altitude material gates).
    // Sourced from game.json `water.seaLevel` on first load, then persisted here — the terrain
    // carve and the water table are only coherent with the level they were generated for, so the
    // stored value wins over a later game.json edit (the loader WARNs on a mismatch).
    float seaLevelY = Core::kSeaLevelY;
    std::vector<BiomeTune> biomes;
    std::vector<SplinePoint> heightSpline; // continentalness → base elevation; empty = engine default

    std::string toJson() const {
        nlohmann::json root;
        root["version"] = version;
        root["seed"] = seed;
        root["type"] = type;
        root["climateFrequency"] = climateFrequency;
        root["seaLevelY"] = seaLevelY;
        nlohmann::json barr = nlohmann::json::array();
        for (const auto& b : biomes) {
            nlohmann::json items = nlohmann::json::array();
            for (const auto& f : b.flora)
                items.push_back({{"template", f.templateName}, {"weight", f.weight}});
            nlohmann::json layers = nlohmann::json::array();
            for (const auto& L : b.extraLayers) {
                nlohmann::json litems = nlohmann::json::array();
                for (const auto& f : L.items)
                    litems.push_back({{"template", f.templateName}, {"weight", f.weight}});
                layers.push_back({{"mode", L.mode}, {"fullness", L.fullness},
                                  {"density", L.density}, {"spacing", L.spacing},
                                  {"items", litems}});
            }
            barr.push_back({
                {"name", b.name},
                {"extremeness", {{"heightScale", b.heightScale}}},
                {"flora", {{"mode", b.floraMode}, {"fullness", b.floraFullness},
                           {"density", b.floraDensity}, {"spacing", b.floraSpacing},
                           {"items", items}}},
                {"floraLayers", layers},
            });
        }
        root["biomes"] = barr;
        if (!heightSpline.empty()) {
            nlohmann::json sp = nlohmann::json::array();
            for (const auto& p : heightSpline) sp.push_back({{"x", p.x}, {"y", p.y}});
            root["heightSpline"] = sp;
        }
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
            // Missing key (recipes persisted before seaLevel plumbing) → the engine default the
            // old bake actually used, so legacy worlds keep byte-identical behavior.
            r.seaLevelY = root.value("seaLevelY", Core::kSeaLevelY);
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
                    if (b.contains("floraLayers") && b["floraLayers"].is_array()) {
                        for (const auto& lj : b["floraLayers"]) {
                            FloraLayerTune L;
                            L.mode = lj.value("mode", std::string("pool"));
                            L.fullness = lj.value("fullness", 0.85f);
                            L.density = lj.value("density", 0.0f);
                            L.spacing = lj.value("spacing", 6);
                            if (lj.contains("items") && lj["items"].is_array())
                                for (const auto& it : lj["items"])
                                    L.items.push_back({it.value("template", ""), it.value("weight", 1)});
                            if (!L.items.empty()) bt.extraLayers.push_back(std::move(L));
                        }
                    }
                    r.biomes.push_back(std::move(bt));
                }
            }
            if (root.contains("heightSpline") && root["heightSpline"].is_array())
                for (const auto& p : root["heightSpline"])
                    r.heightSpline.push_back({p.value("x", 0.0f), p.value("y", 0.0f)});
        } catch (...) {
            // Bad/empty input -> caller falls back to synthesizing from the category library.
        }
        return r;
    }
};

} // namespace Phyxel
