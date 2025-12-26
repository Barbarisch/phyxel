import trimesh
import numpy as np
from scipy import ndimage

def voxelize_mesh(mesh, target_size=None, pitch=None, resolution_mode='auto', hollow=False, thicken=0, shell=False, shell_thickness=1):
    """
    Voxelizes a trimesh object with robust post-processing (filling, thickening).
    
    Args:
        mesh: trimesh.Trimesh object
        target_size: float, target size in world units (optional, used to calculate scale if pitch is None)
        pitch: float, voxel size in world units (optional, overrides target_size)
        resolution_mode: 'auto', 'cube', 'subcube', 'microcube' (affects pitch if not provided)
        hollow: bool, if True, skips interior filling
        thicken: int, number of dilation iterations
        shell: bool, if True, generates a hollow shell
        shell_thickness: int, thickness of the shell
        
    Returns:
        numpy.ndarray: Boolean 3D array representing the voxel grid
        float: The pitch used
        float: The scale factor applied to the mesh
    """
    
    # Calculate scale/pitch
    scale = 1.0
    if pitch is None:
        if target_size is not None:
            extents = mesh.extents
            if np.max(extents) > 0:
                scale = target_size / np.max(extents)
                mesh.apply_scale(scale)
        
        # Determine pitch based on resolution mode
        if resolution_mode == 'auto' or resolution_mode == 'microcube':
            pitch = 1.0 / 9.0
        elif resolution_mode == 'subcube':
            pitch = 1.0 / 3.0
        else: # cube
            pitch = 1.0
    
    # Voxelize
    try:
        voxel_grid = mesh.voxelized(pitch=pitch)
        matrix = voxel_grid.matrix
    except Exception as e:
        print(f"Error during voxelization: {e}")
        return None, pitch, scale
    
    # Ensure matrix is boolean for ndimage operations
    matrix = matrix.astype(bool)

    # Process Volume (Shell vs Solid vs Raw)
    if shell:
        # 1. Create solid volume (fill everything inside)
        solid_matrix = ndimage.binary_fill_holes(matrix)
        # 2. Erode to find the core
        eroded_matrix = ndimage.binary_erosion(solid_matrix, iterations=shell_thickness)
        # 3. Subtract core from solid to get shell
        matrix = solid_matrix & ~eroded_matrix
        
    elif not hollow:
        # Use scipy binary_fill_holes for robust interior filling
        matrix = ndimage.binary_fill_holes(matrix)
        
        # Optional: Binary closing to smooth surface and fill small gaps
        matrix = ndimage.binary_closing(matrix, iterations=1)
    
    # Thicken (Dilation)
    if thicken > 0:
        matrix = ndimage.binary_dilation(matrix, iterations=thicken)

    return matrix.astype(bool), pitch, scale
