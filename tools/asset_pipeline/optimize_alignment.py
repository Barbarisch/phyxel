import numpy as np
import sys
import os

# Add current directory to path to allow importing template_writer
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from template_writer import analyze_voxel_data

def calculate_complexity(matrix, fill_threshold=1.0):
    """
    Calculates the complexity (number of primitives) for a given voxel matrix.
    Returns tuple: (score, num_cubes, num_subcubes, num_microcubes)
    Lower score is better.
    """
    analysis = analyze_voxel_data(matrix, fill_threshold)
    
    num_cubes = np.sum(analysis['cubes_mask'])
    num_subcubes = np.sum(analysis['subcubes_mask'])
    num_microcubes = np.sum(analysis['microcubes_mask'])
    
    # Score: Simple sum of primitives
    score = num_cubes + num_subcubes + num_microcubes
    
    return score, num_cubes, num_subcubes, num_microcubes

def find_optimal_offset(matrix, fill_threshold=1.0, verbose=False):
    """
    Finds the optimal offset (0-8, 0-8, 0-8) to minimize primitive count.
    Returns (best_offset, best_matrix, stats)
    """
    best_score = float('inf')
    best_offset = (0, 0, 0)
    best_stats = (0, 0, 0)
    
    print(f"Optimizing alignment (searching 9x9x9 offsets)...")
    
    # Iterate through all possible offsets within a 9x9x9 block
    # This covers all possible alignments relative to the grid
    # We check 0, 3, 6 first (coarse search) then refine?
    # No, let's do full search, it's robust.
    
    for dx in range(9):
        for dy in range(9):
            for dz in range(9):
                # Construct shifted matrix:
                # Just pad front with (dx, dy, dz).
                padding = [(dx, 0), (dy, 0), (dz, 0)]
                shifted_matrix = np.pad(matrix, padding, mode='constant', constant_values=0)
                
                score, nc, ns, nm = calculate_complexity(shifted_matrix, fill_threshold)
                
                if score < best_score:
                    best_score = score
                    best_offset = (dx, dy, dz)
                    best_stats = (nc, ns, nm)
                    if verbose:
                        print(f"New best: {best_offset} -> {score} (C:{nc} S:{ns} M:{nm})")
    
    print(f"Optimization complete. Best offset: {best_offset}, Score: {best_score}")
    
    # Return the best shifted matrix ready for writing
    padding = [(best_offset[0], 0), (best_offset[1], 0), (best_offset[2], 0)]
    best_matrix = np.pad(matrix, padding, mode='constant', constant_values=0)
    
    return best_offset, best_matrix, best_stats

if __name__ == "__main__":
    # Test with a random matrix
    print("Running test...")
    test_matrix = np.zeros((20, 20, 20), dtype=bool)
    test_matrix[5:14, 5:14, 5:14] = True # A 9x9x9 block at (5,5,5)
    # This is misaligned. Optimal offset should be (4,4,4) to move it to (9,9,9) or similar?
    # If we shift it by (4,4,4), it becomes at (9,9,9) which is aligned.
    
    offset, matrix, stats = find_optimal_offset(test_matrix, verbose=True)
    print(f"Result: {offset}, Stats: {stats}")
