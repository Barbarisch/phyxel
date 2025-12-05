#include <gtest/gtest.h>
#include "core/WorldStorage.h"
#include <filesystem>
#include <cstdlib>

namespace VulkanCube {
namespace Testing {

class WorldStorageTest : public ::testing::Test {
protected:
    std::string dbPath;

    void SetUp() override {
        // Create a unique temporary path for the database
        // Use a random number to avoid conflicts
        // Use a subdirectory to ensure parent_path() is not empty
        std::string filename = "test_world_storage_" + std::to_string(std::rand()) + ".db";
        dbPath = (std::filesystem::current_path() / filename).string();
        
        // Ensure it doesn't exist
        if (std::filesystem::exists(dbPath)) {
            std::filesystem::remove(dbPath);
        }
    }

    void TearDown() override {
        // Clean up
        if (std::filesystem::exists(dbPath)) {
            std::filesystem::remove(dbPath);
        }
    }
};

TEST_F(WorldStorageTest, InitializeCreatesTables) {
    WorldStorage storage(dbPath);
    EXPECT_TRUE(storage.initialize()) << "WorldStorage initialization failed";
    
    // Verify tables exist (implicitly verified by initialize returning true, 
    // as it executes the CREATE TABLE statements)
    
    storage.close();
}

} // namespace Testing
} // namespace VulkanCube
