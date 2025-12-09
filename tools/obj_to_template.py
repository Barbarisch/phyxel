import argparse
import trimesh
import numpy as np
import os
import sys
from collections import defaultdict
from scipy import ndimage

def convert_obj_to_template(obj_path, output_path, material_name, target_size, resolution_mode='auto', hollow=False, fill_threshold=1.0):
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
    
    # Fill hollows (optional, usually desired for solid objects)
    if not hollow:
        # Use scipy binary_fill_holes for robust interior filling
        print("Filling mesh interior...")
        matrix = ndimage.binary_fill_holes(matrix)
        print(f"Voxel count after fill: {np.sum(matrix)}")
        
        # Optional: Binary closing to smooth surface and fill small gaps
        # This helps the optimization logic find complete blocks
        print("Applying morphological closing...")
        matrix = ndimage.binary_closing(matrix, iterations=1)
        print(f"Voxel count after closing: {np.sum(matrix)}")
    
    # Get indices
    indices = np.argwhere(matrix)
    
    if len(indices) == 0:
        print("Warning: No voxels generated. Try increasing the --size.")
        return

    # Shift so min is (0,0,0)
    min_idx = np.min(indices, axis=0)
    indices = indices - min_idx
    
    # Sort indices for cleaner output (bottom-up)
    # Sort by Y, then Z, then X
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
            
            # Group by Cube
            cubes = defaultdict(set)
            for idx in indices:
                x, y, z = idx
                cx, rem_x = divmod(x, 9)
                cy, rem_y = divmod(y, 9)
                cz, rem_z = divmod(z, 9)
                cubes[(cx, cy, cz)].add((rem_x, rem_y, rem_z))
            
            count_c = 0
            count_s = 0
            count_m = 0
            
            for c_key in sorted(cubes.keys()):
                c_voxels = cubes[c_key]
                cx, cy, cz = c_key
                
                # Check if full Cube (9x9x9 = 729 microcubes)
                if len(c_voxels) >= 729 * fill_threshold:
                    f.write(f"C {cx} {cy} {cz} {material_name}\n")
                    count_c += 1
                else:
                    # Group by Subcube
                    subcubes = defaultdict(set)
                    for v in c_voxels:
                        rx, ry, rz = v
                        sx, mx = divmod(rx, 3)
                        sy, my = divmod(ry, 3)
                        sz, mz = divmod(rz, 3)
                        subcubes[(sx, sy, sz)].add((mx, my, mz))
                    
                    for s_key in sorted(subcubes.keys()):
                        s_voxels = subcubes[s_key]
                        sx, sy, sz = s_key
                        
                        # Check if full Subcube (3x3x3 = 27 microcubes)
                        if len(s_voxels) >= 27 * fill_threshold:
                            f.write(f"S {cx} {cy} {cz} {sx} {sy} {sz} {material_name}\n")
                            count_s += 1
                        else:
                            # Write Microcubes
                            for m_key in sorted(list(s_voxels)):
                                mx, my, mz = m_key
                                f.write(f"M {cx} {cy} {cz} {sx} {sy} {sz} {mx} {my} {mz} {material_name}\n")
                                count_m += 1
            
            print(f"Optimization Results: {count_c} Cubes, {count_s} Subcubes, {count_m} Microcubes")
            
            if count_c + count_s + count_m == 0:
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
    parser.add_argument("--hollow", action="store_true", help="Do not fill the interior of the object")
    parser.add_argument("--fill-threshold", type=float, default=1.0, help="Threshold (0.0-1.0) to treat a block as full (default: 1.0)")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.input):
        print(f"Error: Input file '{args.input}' not found.")
        sys.exit(1)
        
    convert_obj_to_template(args.input, args.output, args.material, args.size, args.resolution, args.hollow, args.fill_threshold)
