import argparse
import trimesh
import numpy as np
import os
import sys
from collections import defaultdict

# Import voxelizer
try:
    import voxelizer
except ImportError:
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    import voxelizer

def convert_obj_to_template(obj_path, output_path, material_name, target_size, resolution_mode='auto', hollow=False, fill_threshold=1.0, thicken=0, shell=False, shell_thickness=1, optimize=False):
    print(f"Loading mesh: {obj_path}")
    try:
        # Load the mesh
        # force='mesh' ensures we get a mesh object even if it's a scene
        mesh = trimesh.load(obj_path, force='mesh')
        
        # Attempt to repair mesh to ensure better voxelization
        try:
            trimesh.repair.fix_normals(mesh)
            trimesh.repair.fix_inversion(mesh)
            trimesh.repair.fix_winding(mesh)
        except Exception as e:
            print(f"Warning: Mesh repair failed (proceeding anyway): {e}")
    except Exception as e:
        print(f"Error loading mesh: {e}")
        return

    # Use voxelizer
    print(f"Voxelizing (Target Size: {target_size}, Mode: {resolution_mode})...")
    matrix, pitch, scale = voxelizer.voxelize_mesh(mesh, target_size=target_size, resolution_mode=resolution_mode, hollow=hollow, thicken=thicken, shell=shell, shell_thickness=shell_thickness)
    
    if matrix is None:
        return

    print(f"Voxelization complete. Pitch: {pitch:.4f}, Scale: {scale:.4f}")
    print(f"Final voxel count: {np.sum(matrix)}")

    # Crop matrix to bounding box of True values
    true_points = np.argwhere(matrix)
    if true_points.size == 0:
        print("Warning: No voxels generated. Try increasing the --size.")
        return

    min_idx = true_points.min(axis=0)
    max_idx = true_points.max(axis=0) + 1
    matrix = matrix[min_idx[0]:max_idx[0], min_idx[1]:max_idx[1], min_idx[2]:max_idx[2]]
    print(f"Cropped matrix shape: {matrix.shape}")

    # Get indices for non-auto modes (and for sorting check)
    indices = np.argwhere(matrix)
    # Sort indices for cleaner output (bottom-up: Y, Z, X)
    if len(indices) > 0:
        indices = indices[np.lexsort((indices[:,0], indices[:,2], indices[:,1]))]
    
    print(f"Processing {len(indices)} voxels...")
    
    # Use the shared template writer
    try:
        import template_writer
        import optimize_alignment
    except ImportError:
        sys.path.append(os.path.dirname(os.path.abspath(__file__)))
        import template_writer
        import optimize_alignment

    # Optimization step
    if optimize:
        print("Running grid alignment optimization...")
        offset, matrix, stats = optimize_alignment.find_optimal_offset(matrix, fill_threshold, verbose=False)
        print(f"Optimization applied. Offset: {offset}")
        print(f"Optimized Stats: {stats[0]} Cubes, {stats[1]} Subcubes, {stats[2]} Microcubes")

    template_writer.write_template(matrix, output_path, material_name, target_size, resolution_mode, fill_threshold)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert 3D Model (OBJ/STL/PLY) to Phyxel Voxel Template")
    parser.add_argument("input", help="Input 3D model file path")
    parser.add_argument("output", help="Output TXT file path")
    parser.add_argument("--material", default="Stone", help="Material name to use (default: Stone)")
    parser.add_argument("--size", type=float, default=5.0, help="Target size in World Cubes (default: 5.0)")
    parser.add_argument("--resolution", choices=['auto', 'cube', 'subcube', 'microcube'], default='auto', 
                        help="Voxel resolution level (default: auto)")
    parser.add_argument("--hollow", action="store_true", help="Do not fill the interior of the object (keep original voxelization)")
    parser.add_argument("--fill-threshold", type=float, default=1.0, help="Threshold (0.0-1.0) to treat a block as full (default: 1.0)")
    parser.add_argument("--thicken", type=int, default=0, help="Number of dilation iterations to thicken the model (reduces microcubes)")
    parser.add_argument("--shell", action="store_true", help="Force generation of a hollow shell from the outer surface (void interior)")
    parser.add_argument("--shell-thickness", type=int, default=1, help="Wall thickness for shell generation (default: 1)")
    parser.add_argument("--optimize", action="store_true", help="Enable grid alignment optimization to reduce primitive count")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.input):
        print(f"Error: Input file '{args.input}' not found.")
        sys.exit(1)
        
    convert_obj_to_template(args.input, args.output, args.material, args.size, args.resolution, args.hollow, args.fill_threshold, args.thicken, args.shell, args.shell_thickness, args.optimize)
