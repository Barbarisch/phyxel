import numpy as np
import os

def write_template(matrix, output_path, material_name, target_size, resolution_mode, fill_threshold=1.0):
    """
    Writes a voxel matrix to a Phyxel template file (.txt).

    Args:
        matrix (np.ndarray): 3D boolean numpy array representing the voxel data.
        output_path (str): Path to save the output .txt file.
        material_name (str): Name of the material to use for voxels.
        target_size (float): Target size of the object in World Cubes (for metadata).
        resolution_mode (str): 'auto', 'cube', 'subcube', or 'microcube'.
        fill_threshold (float): Threshold (0.0-1.0) to treat a larger block as full in 'auto' mode.
    """
    
    # Get indices for non-auto modes (and for sorting check)
    indices = np.argwhere(matrix)
    # Sort indices for cleaner output (bottom-up: Y, Z, X)
    if len(indices) > 0:
        indices = indices[np.lexsort((indices[:,0], indices[:,2], indices[:,1]))]
    
    print(f"Processing {len(indices)} voxels for output...")
    
    with open(output_path, 'w') as f:
        f.write(f"# Generated from voxel matrix\n")
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
                print(f"Total Primitives: {total_primitives}")
                print(f"Compression Ratio: {covered_voxels / total_primitives:.2f} voxels per primitive (avg)")
            
            if total_primitives == 0:
                print("Warning: No voxels were written! Check your fill threshold or input model.")

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
