#include "core/ChunkStorage.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>

namespace VulkanCube {

ChunkStorage::ChunkStorage() {
    cubes.reserve(32 * 32 * 32);
    staticSubcubes.reserve(1000);
}

ChunkStorage::~ChunkStorage() {
    clear();
}

// =============================================================================
// CORE VOXEL MANAGEMENT - Extracted from Chunk
// =============================================================================

void ChunkStorage::addCube(const glm::ivec3& localPos, std::unique_ptr<Cube> cube) {
    if (!isValidLocalPosition(localPos)) return;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) {
        cubes.resize(32 * 32 * 32, nullptr);
    }
    
    // If cube already exists, replace it
    if (cubes[index]) {
        delete cubes[index];
    }
    
    cubes[index] = cube.release();
    updateCubeMap(localPos, cubes[index]);
}

void ChunkStorage::addCube(Cube* cube) {
    // Simple push_back for compatibility with existing usage patterns
    cubes.push_back(cube);
    if (cube) {
        glm::ivec3 localPos = cube->getPosition();
        updateCubeMap(localPos, cube);
    }
}

bool ChunkStorage::addCubeWithColor(const glm::ivec3& localPos, const glm::vec3& color) {
    if (!isValidLocalPosition(localPos)) return false;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) {
        cubes.resize(32 * 32 * 32, nullptr);
    }
    
    // If cube already exists, just update its color
    if (cubes[index]) {
        cubes[index]->setColor(color);
        cubes[index]->setOriginalColor(color);
        cubes[index]->setBroken(false);
    } else {
        // Create new cube
        cubes[index] = new Cube(localPos, color);
        cubes[index]->setOriginalColor(color);
    }
    
    updateCubeMap(localPos, cubes[index]);
    return true;
}

bool ChunkStorage::removeCube(const glm::ivec3& localPos) {
    if (!isValidLocalPosition(localPos)) return false;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size() || !cubes[index]) return false;
    
    // Delete the cube
    delete cubes[index];
    cubes[index] = nullptr;
    
    // Update cube map
    updateCubeMap(localPos, nullptr);
    
    // Clear voxel type
    clearVoxelType(localPos);
    
    return true;
}

Cube* ChunkStorage::getCubeAt(const glm::ivec3& localPos) const {
    if (!isValidLocalPosition(localPos)) return nullptr;
    
    size_t index = localToIndex(localPos);
    if (index >= cubes.size()) return nullptr;
    
    return cubes[index];
}

// =============================================================================
// SUBCUBE MANAGEMENT - Extracted from Chunk
// =============================================================================

void ChunkStorage::addStaticSubcube(std::unique_ptr<Subcube> subcube) {
    if (!subcube) return;
    
    glm::ivec3 parentPos = subcube->getPosition(); // World position
    glm::ivec3 localPos = subcube->getLocalPosition(); // 0-2 for each axis
    
    staticSubcubes.push_back(subcube.release());
    
    // Update subcube map for O(1) lookup
    updateSubcubeMap(parentPos, localPos, staticSubcubes.back());
}

void ChunkStorage::addStaticSubcube(Subcube* subcube) {
    // Raw pointer overload for compatibility with existing usage patterns
    if (!subcube) return;
    staticSubcubes.push_back(subcube);
    
    glm::ivec3 parentPos = subcube->getPosition(); // World position
    glm::ivec3 localPos = subcube->getLocalPosition(); // 0-2 for each axis
    updateSubcubeMap(parentPos, localPos, subcube);
}

bool ChunkStorage::removeStaticSubcube(const glm::ivec3& parentPos, const glm::ivec3& localPos) {
    // Find and remove the subcube
    auto it = std::find_if(staticSubcubes.begin(), staticSubcubes.end(),
        [&](const Subcube* subcube) {
            return subcube && 
                   subcube->getPosition() == parentPos && 
                   subcube->getLocalPosition() == localPos;
        });
    
    if (it != staticSubcubes.end()) {
        delete *it;
        staticSubcubes.erase(it);
        
        // Update subcube map
        updateSubcubeMap(parentPos, localPos, nullptr);
        return true;
    }
    
    return false;
}

std::vector<Subcube*> ChunkStorage::getStaticSubcubesAt(const glm::ivec3& worldPos) const {
    std::vector<Subcube*> result;
    
    // Note: This method takes a world position and finds subcubes at that position
    for (Subcube* subcube : staticSubcubes) {
        if (subcube && subcube->getPosition() == worldPos) {
            result.push_back(subcube);
        }
    }
    return result;
}

Subcube* ChunkStorage::getSubcubeAt(const glm::ivec3& parentPos, const glm::ivec3& subcubePos) const {
    auto parentIt = subcubeMap.find(parentPos);
    if (parentIt == subcubeMap.end()) return nullptr;
    
    auto subcubeIt = parentIt->second.find(subcubePos);
    return (subcubeIt != parentIt->second.end()) ? subcubeIt->second : nullptr;
}

// =============================================================================
// SPATIAL QUERIES - Extracted from Chunk
// =============================================================================

bool ChunkStorage::isValidLocalPosition(const glm::ivec3& localPos) const {
    return localPos.x >= 0 && localPos.x < 32 &&
           localPos.y >= 0 && localPos.y < 32 &&
           localPos.z >= 0 && localPos.z < 32;
}

size_t ChunkStorage::localToIndex(const glm::ivec3& localPos) const {
    // X-major order (X outermost, Z innermost): z + y*32 + x*1024
    return localPos.z + localPos.y * 32 + localPos.x * 32 * 32;
}

glm::ivec3 ChunkStorage::indexToLocal(size_t index) const {
    // Reverse the localToIndex calculation
    int x = index / (32 * 32);
    int y = (index % (32 * 32)) / 32;
    int z = index % 32;
    return glm::ivec3(x, y, z);
}

// =============================================================================
// VOXEL TYPE MANAGEMENT - Extracted from Chunk
// =============================================================================

VoxelLocation::Type ChunkStorage::getVoxelType(const glm::ivec3& localPos) const {
    auto it = voxelTypeMap.find(localPos);
    if (it != voxelTypeMap.end()) {
        return it->second;
    }
    return VoxelLocation::EMPTY; // Default to empty if not found
}

void ChunkStorage::setVoxelType(const glm::ivec3& localPos, VoxelLocation::Type type) {
    if (type == VoxelLocation::EMPTY) {
        clearVoxelType(localPos);
    } else {
        voxelTypeMap[localPos] = type;
    }
}

bool ChunkStorage::hasVoxelType(const glm::ivec3& localPos) const {
    return voxelTypeMap.find(localPos) != voxelTypeMap.end();
}

void ChunkStorage::clearVoxelType(const glm::ivec3& localPos) {
    voxelTypeMap.erase(localPos);
}

// =============================================================================
// STATISTICS AND VALIDATION - Extracted from Chunk
// =============================================================================

size_t ChunkStorage::getCubeCount() const {
    size_t count = 0;
    for (const Cube* cube : cubes) {
        if (cube) count++;
    }
    return count;
}

size_t ChunkStorage::getSubcubeCount() const {
    size_t count = 0;
    for (const Subcube* subcube : staticSubcubes) {
        if (subcube) count++;
    }
    return count;
}

size_t ChunkStorage::getVisibleCubeCount() const {
    size_t count = 0;
    for (const Cube* cube : cubes) {
        if (cube && cube->isVisible()) count++;
    }
    return count;
}

size_t ChunkStorage::getVisibleSubcubeCount() const {
    size_t count = 0;
    for (const Subcube* subcube : staticSubcubes) {
        if (subcube && subcube->isVisible()) count++;
    }
    return count;
}

void ChunkStorage::clear() {
    clearCubes();
    clearSubcubes();
    clearVoxelTypes();
}

void ChunkStorage::clearCubes() {
    for (Cube* cube : cubes) {
        delete cube;
    }
    cubes.clear();
    cubeMap.clear();
}

void ChunkStorage::clearSubcubes() {
    for (Subcube* subcube : staticSubcubes) {
        delete subcube;
    }
    staticSubcubes.clear();
    subcubeMap.clear();
}

void ChunkStorage::clearVoxelTypes() {
    voxelTypeMap.clear();
}

// =============================================================================
// HELPER METHODS - Private implementation
// =============================================================================

void ChunkStorage::updateCubeMap(const glm::ivec3& localPos, Cube* cube) {
    if (cube) {
        cubeMap[localPos] = cube;
    } else {
        cubeMap.erase(localPos);
    }
}

void ChunkStorage::updateSubcubeMap(const glm::ivec3& parentPos, const glm::ivec3& localPos, Subcube* subcube) {
    if (subcube) {
        subcubeMap[parentPos][localPos] = subcube;
    } else {
        auto parentIt = subcubeMap.find(parentPos);
        if (parentIt != subcubeMap.end()) {
            parentIt->second.erase(localPos);
            if (parentIt->second.empty()) {
                subcubeMap.erase(parentIt);
            }
        }
    }
}

} // namespace VulkanCube