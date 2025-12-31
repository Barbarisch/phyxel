import json
import numpy as np
import trimesh
import argparse
import sys
import os
import math
from scipy.spatial.transform import Rotation as R

# Add current directory to sys.path to allow importing local modules
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.append(current_dir)

import voxelizer
import template_writer
import optimize_alignment

def load_bbmodel_as_mesh(file_path):
    try:
        with open(file_path, 'r') as f:
            data = json.load(f)
    except Exception as e:
        print(f"Error reading JSON file: {e}")
        return None

    elements = data.get('elements', [])
    if not elements:
        print(f"Warning: No elements found in {file_path}")
        return None

    meshes = []

    for element in elements:
        # Blockbench coordinates
        pos_from = np.array(element.get('from', [0, 0, 0]), dtype=float)
        pos_to = np.array(element.get('to', [0, 0, 0]), dtype=float)
        
        size = np.abs(pos_to - pos_from)
        center = (pos_from + pos_to) / 2.0

        # Create box
        # trimesh.creation.box creates a box centered at origin
        # If size is zero in any dimension, trimesh might complain or create empty mesh.
        if np.any(size <= 0):
            continue
            
        box = trimesh.creation.box(extents=size)
        
        # Apply transformations
        
        # 1. Translate to center (this puts the box where it should be if no rotation)
        box.apply_translation(center)
        
        rotation_data = element.get('rotation')
        
        if rotation_data is not None:
            # Determine origin
            origin = np.array(element.get('origin', center), dtype=float)
            
            # Check if origin is inside rotation dict (Minecraft format)
            if isinstance(rotation_data, dict) and 'origin' in rotation_data:
                origin = np.array(rotation_data['origin'], dtype=float)
            
            rot_matrix = None
            
            # If rotation is a list (Euler angles) [x, y, z]
            if isinstance(rotation_data, (list, tuple, np.ndarray)):
                euler = rotation_data
                # User said: R.from_euler('xyz', [rx, ry, rz], degrees=True).as_matrix()
                rot_matrix = R.from_euler('xyz', euler, degrees=True).as_matrix()
                
            elif isinstance(rotation_data, dict):
                # Handle Minecraft format: {"origin": [x,y,z], "axis": "x", "angle": 45}
                axis = rotation_data.get('axis')
                angle = rotation_data.get('angle', 0)
                
                if axis and angle:
                    rot_matrix = R.from_euler(axis, angle, degrees=True).as_matrix()
            
            if rot_matrix is not None:
                # Translate -origin
                box.apply_translation(-origin)
                
                # Rotate
                rot_4x4 = np.eye(4)
                rot_4x4[:3, :3] = rot_matrix
                box.apply_transform(rot_4x4)
                
                # Translate +origin
                box.apply_translation(origin)

        meshes.append(box)

    if not meshes:
        return None

    combined = trimesh.util.concatenate(meshes)
    return combined

def main():
    parser = argparse.ArgumentParser(description="Convert Blockbench .bbmodel to Phyxel Voxel Template")
    parser.add_argument("input", help="Input .bbmodel file path")
    parser.add_argument("output", help="Output .txt template file path")
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
        
    print(f"Loading Blockbench model: {args.input}")
    mesh = load_bbmodel_as_mesh(args.input)
    
    if mesh is None:
        print("Failed to load mesh or no elements found.")
        sys.exit(1)
        
    print(f"Voxelizing mesh (Target Size: {args.size}, Mode: {args.resolution})...")
    matrix, pitch, scale = voxelizer.voxelize_mesh(
        mesh, 
        target_size=args.size, 
        resolution_mode=args.resolution, 
        hollow=args.hollow, 
        thicken=args.thicken, 
        shell=args.shell, 
        shell_thickness=args.shell_thickness
    )
    
    if matrix is None:
        print("Voxelization failed.")
        sys.exit(1)
        
    print(f"Voxelization complete. Pitch: {pitch:.4f}, Scale: {scale:.4f}")
    print(f"Final voxel count: {np.sum(matrix)}")
    
    # Optimization step
    if args.optimize:
        print("Running grid alignment optimization...")
        offset, matrix, stats = optimize_alignment.find_optimal_offset(matrix, args.fill_threshold, verbose=False)
        print(f"Optimization applied. Offset: {offset}")
        print(f"Optimized Stats: {stats[0]} Cubes, {stats[1]} Subcubes, {stats[2]} Microcubes")

    print(f"Writing template to {args.output}...")
    template_writer.write_template(
        matrix, 
        args.output, 
        args.material, 
        args.size, 
        args.resolution,
        args.fill_threshold
    )
    print("Done.")

if __name__ == "__main__":
    main()
