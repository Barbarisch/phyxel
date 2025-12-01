/**
 * Unit tests for Frustum culling utilities
 * 
 * Tests frustum plane extraction, AABB culling, point/sphere tests.
 */

#include <gtest/gtest.h>
#include "utils/Frustum.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace VulkanCube::Utils;

// ============================================================================
// Plane Tests
// ============================================================================

TEST(FrustumTest, Plane_DistanceToPoint_OnPlane) {
    // Plane at origin facing +Z
    Plane plane(glm::vec3(0.0f, 0.0f, 1.0f), 0.0f);
    
    // Point on the plane
    float dist = plane.distanceToPoint(glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(dist, 0.0f);
}

TEST(FrustumTest, Plane_DistanceToPoint_InFront) {
    // Plane at origin facing +Z
    Plane plane(glm::vec3(0.0f, 0.0f, 1.0f), 0.0f);
    
    // Point in front of plane (positive side)
    float dist = plane.distanceToPoint(glm::vec3(0.0f, 0.0f, 5.0f));
    EXPECT_FLOAT_EQ(dist, 5.0f);
}

TEST(FrustumTest, Plane_DistanceToPoint_Behind) {
    // Plane at origin facing +Z
    Plane plane(glm::vec3(0.0f, 0.0f, 1.0f), 0.0f);
    
    // Point behind plane (negative side)
    float dist = plane.distanceToPoint(glm::vec3(0.0f, 0.0f, -5.0f));
    EXPECT_FLOAT_EQ(dist, -5.0f);
}

TEST(FrustumTest, Plane_DistanceToPoint_WithOffset) {
    // Plane offset from origin
    Plane plane(glm::vec3(1.0f, 0.0f, 0.0f), -10.0f);
    
    // Point at x=10 should be on the plane
    float dist = plane.distanceToPoint(glm::vec3(10.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(dist, 0.0f);
}

// ============================================================================
// AABB Tests
// ============================================================================

TEST(FrustumTest, AABB_GetCenter) {
    AABB aabb(glm::vec3(-1.0f, -2.0f, -3.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    glm::vec3 center = aabb.getCenter();
    EXPECT_FLOAT_EQ(center.x, 0.0f);
    EXPECT_FLOAT_EQ(center.y, 0.0f);
    EXPECT_FLOAT_EQ(center.z, 0.0f);
}

TEST(FrustumTest, AABB_GetSize) {
    AABB aabb(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.0f, 4.0f, 6.0f));
    glm::vec3 size = aabb.getSize();
    EXPECT_FLOAT_EQ(size.x, 2.0f);
    EXPECT_FLOAT_EQ(size.y, 4.0f);
    EXPECT_FLOAT_EQ(size.z, 6.0f);
}

TEST(FrustumTest, AABB_GetHalfExtents) {
    AABB aabb(glm::vec3(-2.0f, -4.0f, -6.0f), glm::vec3(2.0f, 4.0f, 6.0f));
    glm::vec3 halfExtents = aabb.getHalfExtents();
    EXPECT_FLOAT_EQ(halfExtents.x, 2.0f);
    EXPECT_FLOAT_EQ(halfExtents.y, 4.0f);
    EXPECT_FLOAT_EQ(halfExtents.z, 6.0f);
}

// ============================================================================
// Frustum Extraction Tests
// ============================================================================

TEST(FrustumTest, ExtractFromMatrix_PerspectiveProjection) {
    // Create a standard perspective projection
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), 
                                 glm::vec3(0.0f, 0.0f, 0.0f), 
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 viewProjection = projection * view;
    
    Frustum frustum;
    frustum.extractFromMatrix(viewProjection);
    
    // All planes should have normalized normals (length ~= 1.0)
    for (const auto& plane : frustum.planes) {
        float length = glm::length(plane.normal);
        EXPECT_NEAR(length, 1.0f, 0.001f) << "Plane normal not normalized";
    }
}

TEST(FrustumTest, ExtractFromMatrix_OrthographicProjection) {
    // Create orthographic projection
    glm::mat4 projection = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::mat4(1.0f); // Identity view
    glm::mat4 viewProjection = projection * view;
    
    Frustum frustum;
    frustum.extractFromMatrix(viewProjection);
    
    // All planes should have normalized normals
    for (const auto& plane : frustum.planes) {
        float length = glm::length(plane.normal);
        EXPECT_NEAR(length, 1.0f, 0.001f);
    }
}

// ============================================================================
// Point Containment Tests
// ============================================================================

TEST(FrustumTest, Contains_PointInsideFrustum) {
    // Simple frustum looking down -Z axis
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Point in front of camera (inside frustum)
    EXPECT_TRUE(frustum.contains(glm::vec3(0.0f, 0.0f, -10.0f)));
}

TEST(FrustumTest, Contains_PointBehindFrustum) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Point behind camera (outside frustum)
    EXPECT_FALSE(frustum.contains(glm::vec3(0.0f, 0.0f, 10.0f)));
}

TEST(FrustumTest, Contains_PointBeyondFarPlane) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Point beyond far plane
    EXPECT_FALSE(frustum.contains(glm::vec3(0.0f, 0.0f, -200.0f)));
}

// ============================================================================
// AABB Intersection Tests
// ============================================================================

TEST(FrustumTest, AABB_CompletelyInside) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Small AABB in front of camera
    AABB aabb(glm::vec3(-1.0f, -1.0f, -11.0f), glm::vec3(1.0f, 1.0f, -9.0f));
    EXPECT_TRUE(frustum.intersects(aabb));
}

TEST(FrustumTest, AABB_CompletelyOutside) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // AABB behind camera
    AABB aabb(glm::vec3(-1.0f, -1.0f, 9.0f), glm::vec3(1.0f, 1.0f, 11.0f));
    EXPECT_FALSE(frustum.intersects(aabb));
}

TEST(FrustumTest, AABB_PartiallyIntersecting) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Large AABB that crosses near plane
    AABB aabb(glm::vec3(-1.0f, -1.0f, -2.0f), glm::vec3(1.0f, 1.0f, 2.0f));
    EXPECT_TRUE(frustum.intersects(aabb));
}

TEST(FrustumTest, AABB_AtOrigin) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // AABB at world origin (camera looking at it)
    AABB aabb(glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(frustum.intersects(aabb));
}

TEST(FrustumTest, AABB_OffToSide) {
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // AABB far off to the side (outside narrow FOV)
    AABB aabb(glm::vec3(100.0f, 0.0f, -10.0f), glm::vec3(102.0f, 2.0f, -8.0f));
    EXPECT_FALSE(frustum.intersects(aabb));
}

// ============================================================================
// Sphere Intersection Tests
// ============================================================================

TEST(FrustumTest, Sphere_CompletelyInside) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Small sphere in front of camera
    EXPECT_TRUE(frustum.intersects(glm::vec3(0.0f, 0.0f, -10.0f), 1.0f));
}

TEST(FrustumTest, Sphere_CompletelyOutside) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Sphere behind camera
    EXPECT_FALSE(frustum.intersects(glm::vec3(0.0f, 0.0f, 10.0f), 1.0f));
}

TEST(FrustumTest, Sphere_PartiallyIntersecting) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Large sphere that crosses near plane
    EXPECT_TRUE(frustum.intersects(glm::vec3(0.0f, 0.0f, -1.0f), 5.0f));
}

TEST(FrustumTest, Sphere_TouchingPlane) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Sphere just touching the near plane (center at near + radius)
    EXPECT_TRUE(frustum.intersects(glm::vec3(0.0f, 0.0f, -2.0f), 1.0f));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(FrustumTest, AABB_ZeroSize) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Zero-size AABB (point) in front of camera
    glm::vec3 point(-1.0f, 0.0f, -10.0f);
    AABB aabb(point, point);
    EXPECT_TRUE(frustum.intersects(aabb));
}

TEST(FrustumTest, Sphere_ZeroRadius) {
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // Zero radius sphere (point) in front of camera
    EXPECT_TRUE(frustum.intersects(glm::vec3(0.0f, 0.0f, -10.0f), 0.0f));
}

TEST(FrustumTest, OrthographicFrustum_AABB) {
    // Orthographic projection (no perspective distortion)
    glm::mat4 projection = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    
    Frustum frustum;
    frustum.extractFromMatrix(projection * view);
    
    // AABB inside orthographic view volume
    AABB aabb(glm::vec3(-5.0f, -5.0f, -50.0f), glm::vec3(5.0f, 5.0f, -10.0f));
    EXPECT_TRUE(frustum.intersects(aabb));
    
    // AABB outside orthographic view volume (too far to the side)
    AABB aabb2(glm::vec3(15.0f, 0.0f, -50.0f), glm::vec3(20.0f, 5.0f, -10.0f));
    EXPECT_FALSE(frustum.intersects(aabb2));
}
