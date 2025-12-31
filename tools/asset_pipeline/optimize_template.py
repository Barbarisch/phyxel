import argparse
import numpy as np
import sys
import os
import re

# Add current directory to path
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import template_writer
import optimize_alignment

def parse_template(file_path):
    """
    Parses a template file and returns a boolean voxel matrix.
    """
    print(f"Parsing template: {file_path}")
    
    cubes = []
    subcubes = []
    microcubes = []
    
    max_c = [0, 0, 0]
    
    with open(file_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
                
            parts = line.split()
            type_char = parts[0]
            
            if type_char == 'C':
                # C x y z Mat
                x, y, z = int(parts[1]), int(parts[2]), int(parts[3])
                cubes.append((x, y, z))
                max_c[0] = max(max_c[0], x)
                max_c[1] = max(max_c[1], y)
                max_c[2] = max(max_c[2], z)
                
            elif type_char == 'S':
                # S cx cy cz sx sy sz Mat
                cx, cy, cz = int(parts[1]), int(parts[2]), int(parts[3])
                sx, sy, sz = int(parts[4]), int(parts[5]), int(parts[6])
                subcubes.append((cx, cy, cz, sx, sy, sz))
                max_c[0] = max(max_c[0], cx)
                max_c[1] = max(max_c[1], cy)
                max_c[2] = max(max_c[2], cz)
                
            elif type_char == 'M':
                # M cx cy cz sx sy sz mx my mz Mat
                cx, cy, cz = int(parts[1]), int(parts[2]), int(parts[3])
                sx, sy, sz = int(parts[4]), int(parts[5]), int(parts[6])
                mx, my, mz = int(parts[7]), int(parts[8]), int(parts[9])
                microcubes.append((cx, cy, cz, sx, sy, sz, mx, my, mz))
                max_c[0] = max(max_c[0], cx)
                max_c[1] = max(max_c[1], cy)
                max_c[2] = max(max_c[2], cz)

    # Create matrix
    # Size in microcubes = (max_c + 1) * 9
    shape = ((max_c[0] + 1) * 9, (max_c[1] + 1) * 9, (max_c[2] + 1) * 9)
    print(f"Reconstructing matrix of size {shape}...")
    matrix = np.zeros(shape, dtype=bool)
    
    # Fill Cubes
    for c in cubes:
        x, y, z = c
        matrix[x*9:(x+1)*9, y*9:(y+1)*9, z*9:(z+1)*9] = True
        
    # Fill Subcubes
    for s in subcubes:
        cx, cy, cz, sx, sy, sz = s
        base_x = cx * 9 + sx * 3
        base_y = cy * 9 + sy * 3
        base_z = cz * 9 + sz * 3
        matrix[base_x:base_x+3, base_y:base_y+3, base_z:base_z+3] = True
        
    # Fill Microcubes
    for m in microcubes:
        cx, cy, cz, sx, sy, sz, mx, my, mz = m
        x = cx * 9 + sx * 3 + mx
        y = cy * 9 + sy * 3 + my
        z = cz * 9 + sz * 3 + mz
        matrix[x, y, z] = True
        
    return matrix

def optimize_template_file(input_path, output_path, fill_threshold=1.0):
    if not os.path.exists(input_path):
        print(f"Error: Input file '{input_path}' not found.")
        return

    # 1. Parse existing template
    matrix = parse_template(input_path)
    
    # 2. Optimize
    print("Running grid alignment optimization...")
    offset, optimized_matrix, stats = optimize_alignment.find_optimal_offset(matrix, fill_threshold, verbose=True)
    
    print(f"Optimization applied. Offset: {offset}")
    print(f"Optimized Stats: {stats[0]} Cubes, {stats[1]} Subcubes, {stats[2]} Microcubes")
    
    # 3. Write back
    # We need to extract metadata from the original file or just use defaults
    # Let's read the header to get material and target size
    material = "Stone"
    target_size = 5.0
    
    with open(input_path, 'r') as f:
        content = f.read()
        mat_match = re.search(r"# Material: (.+)", content)
        if mat_match:
            material = mat_match.group(1).strip()
        
        size_match = re.search(r"# Target Size: (.+)", content)
        if size_match:
            try:
                target_size = float(size_match.group(1).strip())
            except:
                pass

    print(f"Writing optimized template to {output_path}...")
    template_writer.write_template(
        optimized_matrix, 
        output_path, 
        material, 
        target_size, 
        'auto',
        fill_threshold
    )
    print("Done.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Optimize existing Phyxel Voxel Template")
    parser.add_argument("input", help="Input .txt template file path")
    parser.add_argument("output", help="Output .txt template file path")
    parser.add_argument("--fill-threshold", type=float, default=1.0, help="Threshold (0.0-1.0) to treat a block as full (default: 1.0)")
    
    args = parser.parse_args()
    
    optimize_template_file(args.input, args.output, args.fill_threshold)
