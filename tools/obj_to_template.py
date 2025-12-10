import argparse
import trimesh
import numpy as np
import os
import sys
from collections import defaultdict
from scipy import ndimage

def convert_obj_to_template(obj_path, output_path, material_name, target_size, resolution_mode='auto', hollow=False, fill_threshold=1.0, thicken=0, shell=False, shell_thickness=1):
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

    # Calculate scale to fit in target_size (World Units / Cubes)
    extents = mesh.extents
    if np.max(extents) == 0:
        print("Error: Mesh has zero extents")
        return
        
    scale = target_size / np.max(extents)
    print(f"Scaling mesh by factor: {scale:.4f} to fit in {target_size} world units (Cubes)")
    
    # Apply scale
    mesh.apply_scale(scale)
    
    # Determine pitch (voxel size in world units)
    if resolution_mode == 'auto':
        pitch = 1.0 / 9.0
        print("Mode: Auto (Adaptive resolution, base 1/9 Cube)")
    elif resolution_mode == 'microcube':
        pitch = 1.0 / 9.0
        print("Mode: Microcube (1/9 Cube resolution)")
    elif resolution_mode == 'subcube':
        pitch = 1.0 / 3.0
        print("Mode: Subcube (1/3 Cube resolution)")
    else: # cube
        pitch = 1.0
        print("Mode: Cube (1 Cube resolution)")

    # Voxelize
    print(f"Voxelizing with pitch {pitch:.4f}...")
    try:
        voxel_grid = mesh.voxelized(pitch=pitch)
        matrix = voxel_grid.matrix
        print(f"Initial voxel count: {np.sum(matrix)}")
    except Exception as e:
        print(f"Error during voxelization: {e}")
        print("Ensure the mesh is watertight (manifold) for best results.")
        return
    
    # Ensure matrix is boolean for ndimage operations
    matrix = matrix.astype(bool)

    # Process Volume (Shell vs Solid vs Raw)
    if shell:
        print(f"Generating hollow shell (thickness={shell_thickness})...")
        # 1. Create solid volume (fill everything inside)
        solid_matrix = ndimage.binary_fill_holes(matrix)
        # 2. Erode to find the core
        eroded_matrix = ndimage.binary_erosion(solid_matrix, iterations=shell_thickness)
        # 3. Subtract core from solid to get shell
        matrix = solid_matrix & ~eroded_matrix
        print(f"Voxel count after shell generation: {np.sum(matrix)}")
        
    elif not hollow:
        # Use scipy binary_fill_holes for robust interior filling
        print("Filling mesh interior...")
        matrix = ndimage.binary_fill_holes(matrix)
        print(f"Voxel count after fill: {np.sum(matrix)}")
        
        # Optional: Binary closing to smooth surface and fill small gaps
        # This helps the optimization logic find complete blocks
        print("Applying morphological closing...")
        matrix = ndimage.binary_closing(matrix, iterations=1)
        print(f"Voxel count after closing: {np.sum(matrix)}")
    
    # Thicken (Dilation)
    if thicken > 0:
        print(f"Thickening mesh (dilation iterations={thicken})...")
        matrix = ndimage.binary_dilation(matrix, iterations=thicken)
        print(f"Voxel count after thickening: {np.sum(matrix)}")

    # Ensure matrix is boolean
    matrix = matrix.astype(bool)

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
    indices = indices[np.lexsort((indices[:,0], indices[:,2], indices[:,1]))]
    
    print(f"Processing {len(indices)} voxels...")
    
    with open(output_path, 'w') as f:
        f.write(f"# Generated from {os.path.basename(obj_path)}\n")
        f.write(f"# Target Size: {target_size}\n")
        f.write(f"# Resolution: {resolution_mode}\n")
        f.write(f"# Material: {material_name}\n")
        
        if resolution_mode == 'auto':
            f.write(f"# Format: [Type] [Coords...] {material_name}\n")
            f.write(f"# Legend: C=Cube, S=Subcube, M=Microcube\n\n")
            
            # Pad matrix to multiple of 9 for easy reshaping
            shape = np.array(matrix.shape)
            pad_shape = (np.ceil(shape / 9) * 9).astype(int)
            padding = [(0, pad_shape[i] - shape[i]) for i in range(3)]
            
            padded_matrix = np.pad(matrix, padding, mode='constant', constant_values=0)
            
            # --- Level 1: Cubes (9x9x9) ---
            cx, cy, cz = pad_shape // 9
            cubes_view = padded_matrix.reshape(cx, 9, cy, 9, cz, 9)
            cubes_counts = cubes_view.sum(axis=(1, 3, 5))
            cubes_mask = cubes_counts >= (729 * fill_threshold)
            
            # --- Level 2: Subcubes (3x3x3) ---
            sx, sy, sz = pad_shape // 3
            subcubes_view = padded_matrix.reshape(sx, 3, sy, 3, sz, 3)
            subcubes_counts = subcubes_view.sum(axis=(1, 3, 5))
            subcubes_mask = subcubes_counts >= (27 * fill_threshold)
            
            # Exclude subcubes that are inside full cubes
            cubes_mask_upscaled = cubes_mask.repeat(3, axis=0).repeat(3, axis=1).repeat(3, axis=2)
            final_subcubes_mask = subcubes_mask & ~cubes_mask_upscaled
            
            # --- Level 3: Microcubes ---
            # Exclude microcubes inside full cubes OR full subcubes
            cubes_mask_micro = cubes_mask.repeat(9, axis=0).repeat(9, axis=1).repeat(9, axis=2)
            subcubes_mask_micro = final_subcubes_mask.repeat(3, axis=0).repeat(3, axis=1).repeat(3, axis=2)
            final_microcubes_mask = padded_matrix & ~cubes_mask_micro & ~subcubes_mask_micro
            
            # --- Output Generation ---
            count_c = 0
            count_s = 0
            count_m = 0
            
            # Get coordinates
            c_coords = np.argwhere(cubes_mask)
            s_coords = np.argwhere(final_subcubes_mask)
            m_coords = np.argwhere(final_microcubes_mask)
            
            # Sort (Y, Z, X)
            if len(c_coords) > 0:
                c_coords = c_coords[np.lexsort((c_coords[:,0], c_coords[:,2], c_coords[:,1]))]
            if len(s_coords) > 0:
                s_coords = s_coords[np.lexsort((s_coords[:,0], s_coords[:,2], s_coords[:,1]))]
            if len(m_coords) > 0:
                m_coords = m_coords[np.lexsort((m_coords[:,0], m_coords[:,2], m_coords[:,1]))]
            
            for c in c_coords:
                f.write(f"C {c[0]} {c[1]} {c[2]} {material_name}\n")
                count_c += 1
                
            for s in s_coords:
                cx, rx = divmod(s[0], 3)
                cy, ry = divmod(s[1], 3)
                cz, rz = divmod(s[2], 3)
                f.write(f"S {cx} {cy} {cz} {rx} {ry} {rz} {material_name}\n")
                count_s += 1
                
            for m in m_coords:
                cx, rem_x = divmod(m[0], 9)
                cy, rem_y = divmod(m[1], 9)
                cz, rem_z = divmod(m[2], 9)
                sx, micro_x = divmod(rem_x, 3)
                sy, micro_y = divmod(rem_y, 3)
                sz, micro_z = divmod(rem_z, 3)
                f.write(f"M {cx} {cy} {cz} {sx} {sy} {sz} {micro_x} {micro_y} {micro_z} {material_name}\n")
                count_m += 1
            
            print(f"Optimization Results: {count_c} Cubes, {count_s} Subcubes, {count_m} Microcubes")
            
            total_primitives = count_c + count_s + count_m
            if total_primitives > 0:
                # Calculate effective voxel coverage
                covered_voxels = (count_c * 729) + (count_s * 27) + (count_m * 1)
                # Note: This is an approximation if threshold < 1.0, as we count empty space as "covered"
                
                print(f"Total Primitives: {total_primitives}")
                print(f"Compression Ratio: {covered_voxels / total_primitives:.2f} voxels per primitive (avg)")
            
            if total_primitives == 0:
                print("Error: No voxels were written! Check your fill threshold or input model.")

        elif resolution_mode == 'microcube':
             f.write(f"# Format: M CubeX CubeY CubeZ SubX SubY SubZ MicroX MicroY MicroZ Material\n\n")
             for idx in indices:
                x, y, z = idx
                cx, rem_x = divmod(x, 9)
                cy, rem_y = divmod(y, 9)
                cz, rem_z = divmod(z, 9)
                sx, mx = divmod(rem_x, 3)
                sy, my = divmod(rem_y, 3)
                sz, mz = divmod(rem_z, 3)
                f.write(f"M {cx} {cy} {cz} {sx} {sy} {sz} {mx} {my} {mz} {material_name}\n")

        elif resolution_mode == 'subcube':
             f.write(f"# Format: S CubeX CubeY CubeZ SubX SubY SubZ Material\n\n")
             for idx in indices:
                x, y, z = idx
                cx, sx = divmod(x, 3)
                cy, sy = divmod(y, 3)
                cz, sz = divmod(z, 3)
                f.write(f"S {cx} {cy} {cz} {sx} {sy} {sz} {material_name}\n")

        else: # cube
             f.write(f"# Format: C CubeX CubeY CubeZ Material\n\n")
             for idx in indices:
                x, y, z = idx
                f.write(f"C {x} {y} {z} {material_name}\n")
                
    print(f"Success! Template saved to {output_path}")

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
    
    args = parser.parse_args()
    
    if not os.path.exists(args.input):
        print(f"Error: Input file '{args.input}' not found.")
        sys.exit(1)
        
    convert_obj_to_template(args.input, args.output, args.material, args.size, args.resolution, args.hollow, args.fill_threshold, args.thicken, args.shell, args.shell_thickness)
