
import math

class Vec3:
    def __init__(self, x, y, z):
        self.x = x
        self.y = y
        self.z = z
    
    def __add__(self, other):
        return Vec3(self.x + other.x, self.y + other.y, self.z + other.z)
    
    def __sub__(self, other):
        return Vec3(self.x - other.x, self.y - other.y, self.z - other.z)
    
    def __mul__(self, scalar):
        return Vec3(self.x * scalar, self.y * scalar, self.z * scalar)
        
    def __truediv__(self, other):
        # Element-wise division
        return Vec3(self.x / other.x, self.y / other.y, self.z / other.z)
        
    def __repr__(self):
        return f"({self.x}, {self.y}, {self.z})"

def ray_aabb_intersect(ray_origin, ray_dir, aabb_min, aabb_max):
    # inv_dir = 1.0 / ray_dir
    inv_dir = Vec3(1.0/ray_dir.x, 1.0/ray_dir.y, 1.0/ray_dir.z)
    
    # t1 = (aabb_min - ray_origin) * inv_dir
    t1_x = (aabb_min.x - ray_origin.x) * inv_dir.x
    t1_y = (aabb_min.y - ray_origin.y) * inv_dir.y
    t1_z = (aabb_min.z - ray_origin.z) * inv_dir.z
    
    # t2 = (aabb_max - ray_origin) * inv_dir
    t2_x = (aabb_max.x - ray_origin.x) * inv_dir.x
    t2_y = (aabb_max.y - ray_origin.y) * inv_dir.y
    t2_z = (aabb_max.z - ray_origin.z) * inv_dir.z
    
    t_min_x = min(t1_x, t2_x)
    t_max_x = max(t1_x, t2_x)
    
    t_min_y = min(t1_y, t2_y)
    t_max_y = max(t1_y, t2_y)
    
    t_min_z = min(t1_z, t2_z)
    t_max_z = max(t1_z, t2_z)
    
    t_near = max(max(t_min_x, t_min_y), t_min_z)
    t_far = min(min(t_max_x, t_max_y), t_max_z)
    
    print(f"t_min: ({t_min_x}, {t_min_y}, {t_min_z})")
    print(f"t_max: ({t_max_x}, {t_max_y}, {t_max_z})")
    print(f"t_near: {t_near}, t_far: {t_far}")
    
    if t_near > t_far or t_far < 0.0:
        return False, 0.0
        
    distance = t_near if t_near > 0.0 else t_far
    return True, distance

# Constants from Log
ray_origin = Vec3(65.987, 66.3796, 65.987)
ray_dir = Vec3(-0.486175, -0.531539, -0.693614)

# Voxel AABB (63,64,63)
voxel_min = Vec3(63.0, 64.0, 63.0)
voxel_max = Vec3(64.0, 65.0, 64.0)

print(f"Ray Origin: {ray_origin}")
print(f"Ray Dir: {ray_dir}")
print(f"Voxel Min: {voxel_min}")
print(f"Voxel Max: {voxel_max}")

print("\n--- Test Voxel Intersection ---")
hit, dist = ray_aabb_intersect(ray_origin, ray_dir, voxel_min, voxel_max)
print(f"Hit: {hit}, Dist: {dist}")
