#include "utils/FileUtils.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace Phyxel {
namespace Utils {

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

std::vector<char> loadShader(const std::string& shaderPath) {
    try {
        return readFile(shaderPath);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load shader: " + shaderPath + " - " + e.what());
    }
}

} // namespace Utils
} // namespace Phyxel
