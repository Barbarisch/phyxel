#include "core/MaterialRegistry.h"
#include "utils/Logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cassert>

namespace Phyxel {
namespace Core {

using json = nlohmann::json;

MaterialRegistry& MaterialRegistry::instance() {
    static MaterialRegistry inst;
    return inst;
}

// ---- JSON Loading ----

static MaterialPhysics parsePhysics(const json& j) {
    MaterialPhysics p;
    if (j.contains("mass"))                 p.mass = j["mass"].get<float>();
    if (j.contains("friction"))             p.friction = j["friction"].get<float>();
    if (j.contains("restitution"))          p.restitution = j["restitution"].get<float>();
    if (j.contains("linearDamping"))        p.linearDamping = j["linearDamping"].get<float>();
    if (j.contains("angularDamping"))       p.angularDamping = j["angularDamping"].get<float>();
    if (j.contains("breakForceMultiplier")) p.breakForceMultiplier = j["breakForceMultiplier"].get<float>();
    if (j.contains("bondStrength"))         p.bondStrength = j["bondStrength"].get<float>();
    if (j.contains("angularVelocityScale")) p.angularVelocityScale = j["angularVelocityScale"].get<float>();
    if (j.contains("metallic"))             p.metallic = j["metallic"].get<float>();
    if (j.contains("roughness"))            p.roughness = j["roughness"].get<float>();
    if (j.contains("colorTint")) {
        auto& ct = j["colorTint"];
        p.colorTint = glm::vec3(ct[0].get<float>(), ct[1].get<float>(), ct[2].get<float>());
    }
    return p;
}

static json serializePhysics(const MaterialPhysics& p) {
    return json{
        {"mass", p.mass},
        {"friction", p.friction},
        {"restitution", p.restitution},
        {"linearDamping", p.linearDamping},
        {"angularDamping", p.angularDamping},
        {"breakForceMultiplier", p.breakForceMultiplier},
        {"bondStrength", p.bondStrength},
        {"angularVelocityScale", p.angularVelocityScale},
        {"colorTint", {p.colorTint.x, p.colorTint.y, p.colorTint.z}},
        {"metallic", p.metallic},
        {"roughness", p.roughness}
    };
}

static MaterialTextures parseTextures(const json& j) {
    MaterialTextures t;
    t.faceFiles[0] = j.value("side_n", "");
    t.faceFiles[1] = j.value("side_s", "");
    t.faceFiles[2] = j.value("side_e", "");
    t.faceFiles[3] = j.value("side_w", "");
    t.faceFiles[4] = j.value("top", "");
    t.faceFiles[5] = j.value("bottom", "");
    return t;
}

static json serializeTextures(const MaterialTextures& t) {
    return json{
        {"side_n", t.faceFiles[0]},
        {"side_s", t.faceFiles[1]},
        {"side_e", t.faceFiles[2]},
        {"side_w", t.faceFiles[3]},
        {"top", t.faceFiles[4]},
        {"bottom", t.faceFiles[5]}
    };
}

bool MaterialRegistry::loadFromJson(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR_FMT("MaterialRegistry", "Failed to open materials file: " << path);
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const json::parse_error& e) {
        LOG_ERROR_FMT("MaterialRegistry", "JSON parse error in " << path << ": " << e.what());
        return false;
    }

    materials_.clear();
    nameToID_.clear();

    if (!root.contains("materials") || !root["materials"].is_array()) {
        LOG_ERROR("MaterialRegistry", "materials.json missing 'materials' array");
        return false;
    }

    for (const auto& matJson : root["materials"]) {
        MaterialDef def;
        def.name = matJson.value("name", "");
        def.description = matJson.value("description", "");
        def.category = matJson.value("category", "material");
        def.emissive = matJson.value("emissive", false);

        if (def.name.empty()) {
            LOG_WARN("MaterialRegistry", "Skipping material with empty name");
            continue;
        }

        if (matJson.contains("physics")) {
            def.physics = parsePhysics(matJson["physics"]);
        }

        if (matJson.contains("textures")) {
            def.textures = parseTextures(matJson["textures"]);
        }

        materials_.push_back(std::move(def));
    }

    // Assign IDs and build lookup structures
    assignAtlasIndices();
    rebuildLookupCache();

    LOG_INFO_FMT("MaterialRegistry", "Loaded " << materials_.size()
                 << " materials (" << getTextureCount() << " texture slots) from " << path);

    notifyChanged();
    return true;
}

bool MaterialRegistry::saveToJson(const std::string& path) const {
    json root;
    root["version"] = 1;
    root["atlas"] = json{
        {"textureSize", 18},
        {"padding", 1},
        {"texturesPerRow", 6}
    };

    json materialsArray = json::array();
    for (const auto& mat : materials_) {
        json matJson;
        matJson["name"] = mat.name;
        matJson["description"] = mat.description;
        matJson["category"] = mat.category;
        if (mat.emissive) matJson["emissive"] = true;
        if (mat.hasPhysics()) {
            matJson["physics"] = serializePhysics(mat.physics);
        }
        matJson["textures"] = serializeTextures(mat.textures);
        materialsArray.push_back(std::move(matJson));
    }
    root["materials"] = materialsArray;

    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR_FMT("MaterialRegistry", "Failed to write materials file: " << path);
        return false;
    }
    file << root.dump(2);
    return true;
}

// ---- Atlas Index Assignment ----

void MaterialRegistry::assignAtlasIndices() {
    // Materials are already in the order from JSON (which preserves the atlas layout).
    // Each material gets 6 consecutive atlas slots: materialID * 6 + faceID.
    for (int i = 0; i < static_cast<int>(materials_.size()); i++) {
        materials_[i].materialID = i;
        for (int face = 0; face < 6; face++) {
            materials_[i].atlasIndices[face] = static_cast<uint16_t>(i * 6 + face);
        }
    }
}

void MaterialRegistry::rebuildLookupCache() {
    nameToID_.clear();
    memset(faceIndexCache_, 0xFF, sizeof(faceIndexCache_)); // Fill with INVALID_TEXTURE_INDEX (0xFFFF)

    defaultMaterialID_ = -1;
    hoverMaterialID_ = -1;
    grassdirtMaterialID_ = -1;
    placeholderIndex_ = 0; // Will be overwritten if placeholder exists

    for (const auto& mat : materials_) {
        nameToID_[mat.name] = mat.materialID;

        for (int face = 0; face < 6; face++) {
            faceIndexCache_[mat.materialID][face] = mat.atlasIndices[face];
        }

        if (mat.name == "Default")     defaultMaterialID_ = mat.materialID;
        if (mat.name == "hover")       hoverMaterialID_ = mat.materialID;
        if (mat.name == "grassdirt")   grassdirtMaterialID_ = mat.materialID;
        if (mat.name == "placeholder") placeholderIndex_ = mat.atlasIndices[5]; // bottom face
    }
}

// ---- Lookups ----

int MaterialRegistry::getMaterialID(const std::string& name) const {
    auto it = nameToID_.find(name);
    if (it != nameToID_.end()) return it->second;
    return defaultMaterialID_; // Fallback to Default, matching old behavior
}

uint16_t MaterialRegistry::getTextureIndex(const std::string& materialName, int faceID) const {
    if (faceID < 0 || faceID >= 6) return placeholderIndex_;
    int matID = getMaterialID(materialName);
    if (matID < 0) return placeholderIndex_;
    return faceIndexCache_[matID][faceID];
}

uint16_t MaterialRegistry::getTextureIndex(int materialID, int faceID) const {
    if (materialID < 0 || materialID >= static_cast<int>(materials_.size())) return placeholderIndex_;
    if (faceID < 0 || faceID >= 6) return placeholderIndex_;
    return faceIndexCache_[materialID][faceID];
}

const MaterialDef* MaterialRegistry::getMaterial(const std::string& name) const {
    auto it = nameToID_.find(name);
    if (it == nameToID_.end()) return nullptr;
    return &materials_[it->second];
}

const MaterialDef* MaterialRegistry::getMaterial(int materialID) const {
    if (materialID < 0 || materialID >= static_cast<int>(materials_.size())) return nullptr;
    return &materials_[materialID];
}

std::vector<std::string> MaterialRegistry::getAllMaterialNames() const {
    std::vector<std::string> names;
    names.reserve(materials_.size());
    for (const auto& mat : materials_) {
        names.push_back(mat.name);
    }
    return names;
}

bool MaterialRegistry::hasMaterial(const std::string& name) const {
    return nameToID_.find(name) != nameToID_.end();
}

// ---- Runtime Mutation ----

int MaterialRegistry::addMaterial(const MaterialDef& def) {
    if (def.name.empty() || hasMaterial(def.name)) {
        LOG_WARN_FMT("MaterialRegistry", "Cannot add material: empty name or already exists: " << def.name);
        return -1;
    }
    if (static_cast<int>(materials_.size()) >= MAX_MATERIALS) {
        LOG_ERROR("MaterialRegistry", "Maximum material count reached");
        return -1;
    }

    MaterialDef newDef = def;
    newDef.materialID = static_cast<int>(materials_.size());
    for (int face = 0; face < 6; face++) {
        newDef.atlasIndices[face] = static_cast<uint16_t>(newDef.materialID * 6 + face);
    }

    materials_.push_back(std::move(newDef));
    rebuildLookupCache();
    notifyChanged();

    LOG_INFO_FMT("MaterialRegistry", "Added material '" << def.name << "' (ID " << materials_.back().materialID << ")");
    return materials_.back().materialID;
}

bool MaterialRegistry::removeMaterial(const std::string& name) {
    auto it = nameToID_.find(name);
    if (it == nameToID_.end()) return false;

    // Don't allow removing system materials
    if (materials_[it->second].category == "system") {
        LOG_WARN_FMT("MaterialRegistry", "Cannot remove system material: " << name);
        return false;
    }

    int removedID = it->second;
    materials_.erase(materials_.begin() + removedID);

    // Reassign IDs and rebuild
    assignAtlasIndices();
    rebuildLookupCache();
    notifyChanged();

    LOG_INFO_FMT("MaterialRegistry", "Removed material '" << name << "'");
    return true;
}

// ---- Convenience Accessors ----

uint16_t MaterialRegistry::getHoverTextureIndex(int faceID) const {
    if (hoverMaterialID_ < 0 || faceID < 0 || faceID >= 6) return placeholderIndex_;
    return faceIndexCache_[hoverMaterialID_][faceID];
}

uint16_t MaterialRegistry::getGrassdirtTextureIndex(int faceID) const {
    if (grassdirtMaterialID_ < 0 || faceID < 0 || faceID >= 6) return placeholderIndex_;
    return faceIndexCache_[grassdirtMaterialID_][faceID];
}

const MaterialPhysics& MaterialRegistry::getPhysics(const std::string& name) const {
    static const MaterialPhysics defaultPhysics;
    auto it = nameToID_.find(name);
    if (it == nameToID_.end()) return defaultPhysics;
    return materials_[it->second].physics;
}

void MaterialRegistry::notifyChanged() {
    for (auto& cb : changeCallbacks_) {
        if (cb) cb();
    }
}

} // namespace Core
} // namespace Phyxel
