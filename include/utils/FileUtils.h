#pragma once

#include <vector>
#include <string>

namespace VulkanCube {
namespace Utils {

// File I/O utilities
std::vector<char> readFile(const std::string& filename);

// Shader loading
std::vector<char> loadShader(const std::string& shaderPath);

} // namespace Utils
} // namespace VulkanCube
