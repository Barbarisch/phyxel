import json
import struct
import sys
import os
import math
import numpy as np
import argparse

# Try to import local voxelizer module
try:
    import voxelizer
except ImportError:
    # If running from root, add current dir to path
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    try:
        import voxelizer
    except ImportError:
        print("Warning: voxelizer module not found.")
        voxelizer = None

# Try to import pygltflib, if not present, ask user to install
try:
    from pygltflib import GLTF2
except ImportError:
    print("Error: pygltflib is required. Please install it using: pip install pygltflib")
    sys.exit(1)

# Try to import trimesh for voxelization
try:
    import trimesh
    from scipy import ndimage
    from scipy.spatial.transform import Rotation as R_scipy
except ImportError:
    print("Warning: trimesh/scipy not found. Voxelization will be skipped.")
    trimesh = None
    R_scipy = None

def transform_point(pt, matrix):
    # pt is [x, y, z]
    # matrix is 16 floats (column major)
    x, y, z = pt
    # Column major:
    # 0  4  8  12
    # 1  5  9  13
    # 2  6  10 14
    # 3  7  11 15
    nx = matrix[0]*x + matrix[4]*y + matrix[8]*z + matrix[12]
    ny = matrix[1]*x + matrix[5]*y + matrix[9]*z + matrix[13]
    nz = matrix[2]*x + matrix[6]*y + matrix[10]*z + matrix[14]
    return [nx, ny, nz]

def quat_from_axis_angle(axis, angle_rad):
    s = math.sin(angle_rad / 2.0)
    return [axis[0]*s, axis[1]*s, axis[2]*s, math.cos(angle_rad / 2.0)]

def quat_mul(q1, q2):
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return [
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2,
        w1*w2 - x1*x2 - y1*y2 - z1*z2
    ]

def rotate_vector(v, q):
    qx, qy, qz, qw = q
    x, y, z = v
    tx = 2.0 * (qy*z - qz*y)
    ty = 2.0 * (qz*x - qx*z)
    tz = 2.0 * (qx*y - qy*x)
    return [x + qw*tx + (qy*tz - qz*ty), y + qw*ty + (qz*tx - qx*tz), z + qw*tz + (qx*ty - qy*tx)]

def get_node_matrix(node):
    # Returns 4x4 numpy matrix
    if node.matrix:
        return np.array(node.matrix, dtype=float).reshape(4, 4, order='F')
    
    t = node.translation or [0.0, 0.0, 0.0]
    r = node.rotation or [0.0, 0.0, 0.0, 1.0] # x, y, z, w
    s = node.scale or [1.0, 1.0, 1.0]
    
    T = np.eye(4)
    T[0:3, 3] = t
    
    # Quat to Mat
    x, y, z, w = r
    R = np.eye(4)
    R[0:3, 0:3] = np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w)],
        [2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y)]
    ])
    
    S = np.eye(4)
    S[0,0] = s[0]
    S[1,1] = s[1]
    S[2,2] = s[2]
    
    return T @ R @ S

def decompose_matrix(M):
    # Translation
    pos = M[0:3, 3].tolist()
    
    # Scale
    sx = np.linalg.norm(M[0:3, 0])
    sy = np.linalg.norm(M[0:3, 1])
    sz = np.linalg.norm(M[0:3, 2])
    scale = [sx, sy, sz]
    
    # Rotation
    R_mat = M[0:3, 0:3].copy()
    if sx > 1e-6: R_mat[:, 0] /= sx
    if sy > 1e-6: R_mat[:, 1] /= sy
    if sz > 1e-6: R_mat[:, 2] /= sz
    
    # Check for reflection (negative scale)
    if np.linalg.det(R_mat) < 0:
        scale = [sx, sy, -sz]
        R_mat[:, 2] *= -1
    
    # Mat to Quat
    if R_scipy:
        try:
            r = R_scipy.from_matrix(R_mat)
            quat = r.as_quat().tolist() # x, y, z, w
            return pos, quat, scale
        except Exception as e:
            print(f"Scipy decomposition failed: {e}")

    # Fallback (simplified for robustness)
    tr = R_mat.trace()
    if tr > 0:
        S = math.sqrt(tr + 1.0) * 2
        qw = 0.25 * S
        qx = (R_mat[2,1] - R_mat[1,2]) / S
        qy = (R_mat[0,2] - R_mat[2,0]) / S
        qz = (R_mat[1,0] - R_mat[0,1]) / S
    elif (R_mat[0,0] > R_mat[1,1]) and (R_mat[0,0] > R_mat[2,2]):
        S = math.sqrt(1.0 + R_mat[0,0] - R_mat[1,1] - R_mat[2,2]) * 2
        qw = (R_mat[2,1] - R_mat[1,2]) / S
        qx = 0.25 * S
        qy = (R_mat[0,1] + R_mat[1,0]) / S
        qz = (R_mat[0,2] + R_mat[2,0]) / S
    elif R_mat[1,1] > R_mat[2,2]:
        S = math.sqrt(1.0 + R_mat[1,1] - R_mat[0,0] - R_mat[2,2]) * 2
        qw = (R_mat[0,2] - R_mat[2,0]) / S
        qx = (R_mat[0,1] + R_mat[1,0]) / S
        qy = 0.25 * S
        qz = (R_mat[1,2] + R_mat[2,1]) / S
    else:
        S = math.sqrt(1.0 + R_mat[2,2] - R_mat[0,0] - R_mat[1,1]) * 2
        qw = (R_mat[1,0] - R_mat[0,1]) / S
        qx = (R_mat[0,2] + R_mat[2,0]) / S
        qy = (R_mat[1,2] + R_mat[2,1]) / S
        qz = 0.25 * S
        
    return pos, [qx, qy, qz, qw], scale

def extract_animation_data(gltf_path, output_path, scale_factor=1.0, style='voxel', extra_animations=None, rotate_x=0.0, rotate_y=0.0, rotate_z=0.0, resolution='custom', voxel_size=0.05, target_height=None):
    
    # Determine pitch
    pitch = voxel_size
    if resolution == 'cube':
        pitch = 1.0
    elif resolution == 'subcube':
        pitch = 1.0 / 3.0
    elif resolution == 'microcube':
        pitch = 1.0 / 9.0
        
    # Normalization logic
    if target_height is not None and trimesh is not None:
        print(f"Normalizing scale to target height: {target_height}")
        try:
            temp_mesh = trimesh.load(gltf_path, force='mesh')
            extents = temp_mesh.extents
            # Assuming Y is up in GLTF/Trimesh load
            current_height = extents[1] 
            if current_height > 0.001:
                new_scale = target_height / current_height
                print(f"  -> Current height: {current_height:.2f}, Target: {target_height:.2f}")
                print(f"  -> Calculated scale factor: {new_scale:.4f} (overriding user scale {scale_factor})")
                scale_factor = new_scale
            else:
                print("  -> Warning: Mesh height is near zero. Skipping normalization.")
        except Exception as e:
            print(f"  -> Normalization failed: {e}")

    print(f"Loading {gltf_path}...")
    gltf = GLTF2().load(gltf_path)

    # 1. Extract Skeleton (Nodes)
    # We need to find the root node of the skeleton. 
    # Often scenes have a default scene.
    
    # Build parent map
    node_parents = {}
    for i, node in enumerate(gltf.nodes):
        if node.children:
            for child_idx in node.children:
                node_parents[child_idx] = i

    bones = []
    bone_map = {} # name -> index in our bones list
    node_index_to_bone_index = {} # gltf node index -> our bone index
    
    # Store the detected root scale to apply to children and voxels
    root_scale_mult = 1.0

    # Helper to get node hierarchy
    def process_node(node_idx, parent_bone_idx):
        nonlocal root_scale_mult
        node = gltf.nodes[node_idx]
        
        # Extract transform
        pos = node.translation if node.translation else [0.0, 0.0, 0.0]
        rot = node.rotation if node.rotation else [0.0, 0.0, 0.0, 1.0] # x, y, z, w
        scale = node.scale if node.scale else [1.0, 1.0, 1.0]

        # If this is the root bone (parent_bone_idx == -1), apply accumulated parent transforms
        if parent_bone_idx == -1:
            # Walk up parents
            parent_matrices = []
            curr = node_idx
            while curr in node_parents:
                curr = node_parents[curr]
                parent_matrices.append(get_node_matrix(gltf.nodes[curr]))
            
            if parent_matrices:
                print(f"Applying {len(parent_matrices)} parent transforms to root bone...")
                # Multiply from top to bottom
                final_parent_mat = np.eye(4)
                for mat in reversed(parent_matrices):
                    final_parent_mat = final_parent_mat @ mat
                
                # Multiply with local matrix
                local_mat = get_node_matrix(node)
                final_mat = final_parent_mat @ local_mat
                
                # Decompose
                pos, rot, scale = decompose_matrix(final_mat)
                print(f"  -> New Root Transform: Pos={pos}, Rot={rot}, Scale={scale}")
                
                # Check for significant scale
                avg_scale = (scale[0] + scale[1] + scale[2]) / 3.0
                if abs(avg_scale - 1.0) > 0.01:
                    print(f"  -> Detected Root Scale {avg_scale:.2f}. Baking into positions...")
                    root_scale_mult = avg_scale
                    scale = [1.0, 1.0, 1.0] # Reset bone scale to 1

        # Apply scale to position
        # If root, apply ONLY user scale_factor (root pos is in world/parent space, not affected by root scale)
        # If child, apply user scale_factor * root_scale_mult
        
        current_scale_factor = scale_factor
        if parent_bone_idx != -1:
            current_scale_factor *= root_scale_mult
            
        pos = [p * current_scale_factor for p in pos]

        bone_idx = len(bones)
        bone_name = node.name if node.name else f"Bone_{bone_idx}"
        
        bones.append({
            "name": bone_name,
            "parent": parent_bone_idx,
            "pos": pos,
            "rot": rot,
            "scl": scale
        })
        
        bone_map[bone_name] = bone_idx
        node_index_to_bone_index[node_idx] = bone_idx

        if node.children:
            for child_idx in node.children:
                process_node(child_idx, bone_idx)

    # Find root nodes (skins usually point to joints)
    if gltf.skins:
        skin = gltf.skins[0]
        # The joints array lists all bones. The first one is usually the root or we can traverse.
        # However, hierarchy is defined in 'nodes'.
        # Let's find the root of the skeleton.
        # Usually the skin has a 'skeleton' property pointing to the root node.
        root_node_idx = skin.skeleton
        if root_node_idx is None and skin.joints:
             # If skeleton root not explicitly set, find the joint that has no parent in the joints list
             # Or just take the first joint and walk up?
             # Simpler: Just traverse from the first joint in the skin, assuming it's the root or close to it.
             # Better: Find the node in 'joints' that is not a child of any other node in 'joints'.
             
             joint_set = set(skin.joints)
             children_set = set()
             for j_idx in skin.joints:
                 if gltf.nodes[j_idx].children:
                     for c in gltf.nodes[j_idx].children:
                         children_set.add(c)
             
             roots = list(joint_set - children_set)
             if roots:
                 root_node_idx = roots[0]
             else:
                 root_node_idx = skin.joints[0] # Fallback

        process_node(root_node_idx, -1)
    else:
        print("No skins found. Traversing all root nodes as bones.")
        scene = gltf.scenes[gltf.scene if gltf.scene is not None else 0]
        for node_idx in scene.nodes:
            process_node(node_idx, -1)

    print(f"Extracted {len(bones)} bones.")

    if rotate_x != 0.0 or rotate_y != 0.0 or rotate_z != 0.0:
        rx = quat_from_axis_angle([1.0, 0.0, 0.0], math.radians(rotate_x))
        ry = quat_from_axis_angle([0.0, 1.0, 0.0], math.radians(rotate_y))
        rz = quat_from_axis_angle([0.0, 0.0, 1.0], math.radians(rotate_z))
        
        # Combine rotations: Z * Y * X (intrinsic) or just multiply them
        # We'll apply Y then X then Z (arbitrary but standard enough)
        # q = rz * ry * rx
        q_yx = quat_mul(ry, rx)
        rot_q = quat_mul(rz, q_yx)
        
        print(f"Applying rotation X={rotate_x}, Y={rotate_y}, Z={rotate_z} to root bones.")
        for b in bones:
            if b['parent'] == -1:
                b['pos'] = rotate_vector(b['pos'], rot_q)
                b['rot'] = quat_mul(rot_q, b['rot'])

    # 2. Extract Animations
    animations_out = []
    used_anim_names = set()

    def process_gltf_animations(source_gltf, source_name_prefix=""):
        if not source_gltf.animations:
            return

        for anim in source_gltf.animations:
            base_name = anim.name if anim.name else "anim"
            
            # If loading extra animations, use the filename as the animation name
            if source_name_prefix:
                if len(source_gltf.animations) > 1:
                    # If multiple animations in the extra file, combine filename + anim name
                    base_name = f"{source_name_prefix}_{base_name}"
                else:
                    # If single animation, just use the filename
                    base_name = source_name_prefix
            
            # Ensure uniqueness
            anim_name = base_name
            counter = 2
            while anim_name in used_anim_names:
                anim_name = f"{base_name}_{counter}"
                counter += 1
            
            used_anim_names.add(anim_name)
            
            print(f"Processing animation: {anim_name}")
            channels_out = []
            
            for channel in anim.channels:
                target_node_idx = channel.target.node
                
                # Map target node to bone index
                # If this is an extra animation file, the node indices might be different!
                # We need to map by NAME.
                
                target_node = source_gltf.nodes[target_node_idx]
                target_node_name = target_node.name
                
                if target_node_name not in bone_map:
                    # Try fuzzy matching or standard mixamo names
                    if "mixamorig:" in target_node_name:
                        simple_name = target_node_name.split(":")[-1]
                        # Try to find bone with this simple name
                        found = False
                        for bname, bidx in bone_map.items():
                            if simple_name in bname:
                                bone_idx = bidx
                                found = True
                                break
                        if not found:
                            continue
                    else:
                        continue
                else:
                    bone_idx = bone_map[target_node_name]

                path = channel.target.path # translation, rotation, scale
                
                sampler = anim.samplers[channel.sampler]
                input_accessor = source_gltf.accessors[sampler.input]
                output_accessor = source_gltf.accessors[sampler.output]
                
                # Read Input (Time)
                times = read_accessor(source_gltf, input_accessor)
                
                # Read Output (Values)
                values = read_accessor(source_gltf, output_accessor)
                
                # Group into keyframes
                keys = []
                for i, t in enumerate(times):
                    val = values[i]
                    # Flatten if necessary (though read_accessor should handle it)
                    keys.append({"t": t[0] if isinstance(t, list) else t, "v": val})

                channels_out.append({
                    "boneIndex": bone_idx,
                    "type": path,
                    "keys": keys
                })
            
            animations_out.append({
                "name": anim_name,
                "channels": channels_out,
                "root_motion_speed": 0.0
            })

    # Process main file animations
    process_gltf_animations(gltf)

    # Process extra animations
    if extra_animations:
        for extra_file in extra_animations:
            print(f"Loading extra animation: {extra_file}...")
            
            # Handle FBX conversion if needed
            temp_extra = None
            load_path = extra_file
            ext = os.path.splitext(extra_file)[1].lower()
            if ext == '.fbx':
                converted = convert_fbx_to_gltf(extra_file)
                if converted:
                    load_path = converted
                    temp_extra = converted
            
            try:
                extra_gltf = GLTF2().load(load_path)
                # Use filename (without extension) as animation name
                anim_name = os.path.splitext(os.path.basename(extra_file))[0]
                process_gltf_animations(extra_gltf, anim_name)
            except Exception as e:
                print(f"Failed to load extra animation {extra_file}: {e}")
            
            # Cleanup temp
            if temp_extra and os.path.exists(temp_extra):
                try:
                    os.remove(temp_extra)
                    bin_file = temp_extra.replace(".gltf", ".bin")
                    if os.path.exists(bin_file): os.remove(bin_file)
                except: pass

    # 1.25 Analyze and Strip Root Motion
    # We look for the "Hips" or root bone.
    # We calculate the total displacement in X/Z.
    # We calculate speed.
    # We strip the X/Z motion from the keys.
    
    print("Analyzing root motion...")
    hips_id = -1
    for name, idx in bone_map.items():
        if "Hips" in name or "Root" in name or "mixamorig:Hips" in name:
            hips_id = idx
            break
            
    if hips_id != -1:
        print(f"Found root bone: {bones[hips_id]['name']} (ID: {hips_id})")
        
        for anim in animations_out:
            # Find the channel for hips
            hips_channel = None
            for ch in anim['channels']:
                if ch['boneIndex'] == hips_id and ch['type'] == 'translation':
                    hips_channel = ch
                    break
            
            if hips_channel and len(hips_channel['keys']) > 1:
                keys = hips_channel['keys']
                start_pos = keys[0]['v']
                end_pos = keys[-1]['v']
                duration = keys[-1]['t'] - keys[0]['t']
                
                # Calculate displacement on XZ plane
                dx = end_pos[0] - start_pos[0]
                dz = end_pos[2] - start_pos[2]
                dist = math.sqrt(dx*dx + dz*dz)
                
                speed = 0.0
                if duration > 0.001:
                    speed = dist / duration
                
                print(f"Animation '{anim['name']}': Distance {dist:.2f}, Duration {duration:.2f}, Speed {speed:.2f}")
                
                # If speed is significant, store it and strip motion
                if speed > 0.1:
                    anim['root_motion_speed'] = speed
                    print(f"  -> Detected moving animation. Stripping root motion...")
                    
                    # Strip motion: Set X and Z to start_pos (or 0 relative to parent?)
                    # Usually we want to keep the "bobbing" (Y) but remove linear X/Z.
                    # But we also need to ensure it loops correctly.
                    # Simplest approach: Set all X/Z to the value of the first frame.
                    
                    first_x = keys[0]['v'][0]
                    first_z = keys[0]['v'][2]
                    
                    for k in keys:
                        k['v'][0] = first_x
                        k['v'][2] = first_z
                        
    else:
        print("Warning: Could not find Hips/Root bone. Skipping root motion analysis.")

    # 1.5 Extract Bone Bounding Boxes (The "Model")
    # We need to process meshes and find which vertices belong to which bone.
    bone_boxes = {} # bone_index -> {min: [inf,inf,inf], max: [-inf,-inf,-inf]}
    
    skin = None
    ibms = []
    if gltf.skins:
        skin = gltf.skins[0]
        
        # Load Inverse Bind Matrices
        if skin.inverseBindMatrices is not None:
            ibm_accessor = gltf.accessors[skin.inverseBindMatrices]
            ibms = read_accessor(gltf, ibm_accessor)
        
    # Collect all mesh data for voxelization
    all_positions = []
    all_joints = []
    all_weights = []
    all_faces = []
    vertex_offset = 0
    
    # Iterate meshes to collect data
    print(f"Scanning {len(gltf.nodes)} nodes for meshes...")
    for node_idx, node in enumerate(gltf.nodes):
        if node.mesh is not None:
            print(f"Found mesh on node {node_idx} ({node.name})")
            # Check if this node uses the skin (or if the skin is applied globally)
            # Usually the node with the mesh has the 'skin' property
            
            is_valid_mesh = False
            if node.skin is not None:
                is_valid_mesh = True
            else:
                name = (node.name or "").lower()
                if "floor" in name or "plane" in name or "ground" in name:
                    print(f"Skipping mesh node '{node.name}' (likely floor/environment)")
                else:
                    is_valid_mesh = True

            if is_valid_mesh:
                mesh = gltf.meshes[node.mesh]
                for prim in mesh.primitives:
                    if prim.attributes.POSITION is None:
                        continue

                    pos_accessor = gltf.accessors[prim.attributes.POSITION]
                    positions = read_accessor(gltf, pos_accessor)
                    num_vertices = len(positions)
                    
                    # Read indices if available
                    indices = []
                    if prim.indices is not None:
                        indices_accessor = gltf.accessors[prim.indices]
                        indices = read_accessor(gltf, indices_accessor)
                    else:
                        # Generate sequential indices
                        indices = list(range(num_vertices))
                    
                    # Convert indices to faces (triangles) and apply offset
                    # Assuming TRIANGLES mode (4)
                    if prim.mode is None or prim.mode == 4:
                        for i in range(0, len(indices), 3):
                            if i + 2 < len(indices):
                                all_faces.append([
                                    indices[i] + vertex_offset,
                                    indices[i+1] + vertex_offset,
                                    indices[i+2] + vertex_offset
                                ])
                    
                    joints = []
                    weights = []
                    
                    has_skinning = prim.attributes.JOINTS_0 is not None and prim.attributes.WEIGHTS_0 is not None
                    
                    if has_skinning:
                        joints_accessor = gltf.accessors[prim.attributes.JOINTS_0]
                        weights_accessor = gltf.accessors[prim.attributes.WEIGHTS_0]
                        joints = read_accessor(gltf, joints_accessor)
                        weights = read_accessor(gltf, weights_accessor)
                    else:
                        # Rigid binding: assign all vertices to the node
                        # Use node_idx as the joint index, weight 1.0
                        joints = [[node_idx, 0, 0, 0]] * num_vertices
                        weights = [[1.0, 0, 0, 0]] * num_vertices
                        
                        # Apply node scale to positions to match World Space/Trimesh behavior
                        if node.scale:
                            s = node.scale
                            positions = [[p[0]*s[0], p[1]*s[1], p[2]*s[2]] for p in positions]
                    
                    all_positions.extend(positions)
                    all_joints.extend(joints)
                    all_weights.extend(weights)
                    vertex_offset += num_vertices

    voxel_shapes = []
    
    # Update effective scale factor for voxelization
    effective_scale_factor = scale_factor * root_scale_mult
    
    if style == 'voxel' and trimesh is not None:
        voxel_shapes = voxelize_mesh(all_positions, all_faces, all_joints, all_weights, node_index_to_bone_index, skin, ibms, effective_scale_factor, pitch)
    elif style == 'box':
        print("Style is 'box'. Skipping voxelization and generating bounding boxes.")

    # Fallback or if voxelization produced nothing (e.g. no skinning found or trimesh failed or style='box')
    if not voxel_shapes:
        print("Falling back to bounding box generation (or using it for physics)...")
        
        bone_boxes = {}
        
        for i in range(len(all_positions)):
            p = all_positions[i]
            # Apply scale to vertex position for bounding box calculation
            p = [x * effective_scale_factor for x in p]
            
            j = all_joints[i]
            w = all_weights[i]
            
            target_bone_idx = -1
            
            # Find dominant bone
            max_w = 0.0
            joint_idx = -1
            for k in range(4):
                if w[k] > max_w:
                    max_w = w[k]
                    joint_idx = j[k]
            
            if max_w > 0.4 and joint_idx >= 0:
                target_node_idx = -1
                if skin and joint_idx < len(skin.joints):
                    target_node_idx = skin.joints[joint_idx]
                elif not skin:
                    target_node_idx = joint_idx

                if target_node_idx != -1 and target_node_idx in node_index_to_bone_index:
                    target_bone_idx = node_index_to_bone_index[target_node_idx]
                    
                    # Transform point to bone local space using IBM
                    # Note: IBMs are for unscaled space.
                    # P_local = IBM * P_mesh_unscaled
                    # P_local_scaled = P_local * scale_factor
                    # So we should unscale p, apply IBM, then rescale.
                    
                    p_unscaled = [x / effective_scale_factor for x in p]
                    if skin and joint_idx < len(ibms):
                        p_local = transform_point(p_unscaled, ibms[joint_idx])
                        p = [x * effective_scale_factor for x in p_local]
            
            if target_bone_idx != -1:
                if target_bone_idx not in bone_boxes:
                    bone_boxes[target_bone_idx] = {
                        "min": [float('inf')]*3, 
                        "max": [float('-inf')]*3
                    }
                
                bbox = bone_boxes[target_bone_idx]
                for d in range(3):
                    bbox["min"][d] = min(bbox["min"][d], p[d])
                    bbox["max"][d] = max(bbox["max"][d], p[d])
        
        # Convert bone_boxes to voxel_shapes format
        for bid, bbox in bone_boxes.items():
            min_pt = bbox["min"]
            max_pt = bbox["max"]
            size = [max_pt[0] - min_pt[0], max_pt[1] - min_pt[1], max_pt[2] - min_pt[2]]
            center = [(max_pt[0] + min_pt[0])/2, (max_pt[1] + min_pt[1])/2, (max_pt[2] + min_pt[2])/2]
            size = [max(0.05, s) for s in size]
            
            voxel_shapes.append({
                "boneId": bid,
                "size": size,
                "center": center
            })

    # Output Data - Custom Text Format
    with open(output_path, 'w') as f:
        f.write("SKELETON\n")
        f.write(f"BoneCount {len(bones)}\n")
        for b in bones:
            # ID Name ParentID Pos Rot Scale
            # Replace spaces in name
            safe_name = b['name'].replace(" ", "_")
            p = b['pos']
            r = b['rot']
            s = b['scl']
            f.write(f"Bone {bone_map[b['name']]} {safe_name} {b['parent']} {p[0]} {p[1]} {p[2]} {r[0]} {r[1]} {r[2]} {r[3]} {s[0]} {s[1]} {s[2]}\n")
        
        # Write Model Data (Voxel Shapes)
        f.write("MODEL\n")
        f.write(f"BoxCount {len(voxel_shapes)}\n")
        for shape in voxel_shapes:
            bid = shape["boneId"]
            size = shape["size"]
            center = shape["center"]
            f.write(f"Box {bid} {size[0]} {size[1]} {size[2]} {center[0]} {center[1]} {center[2]}\n")

        for anim in animations_out:
            safe_anim_name = anim['name'].replace(" ", "_")
            f.write(f"ANIMATION {safe_anim_name}\n")
            # Calculate duration
            duration = 0.0
            for ch in anim['channels']:
                if ch['keys']:
                    duration = max(duration, ch['keys'][-1]['t'])
            
            f.write(f"Duration {duration}\n")
            
            # Write Speed if available
            if 'root_motion_speed' in anim and anim['root_motion_speed'] > 0.0:
                f.write(f"Speed {anim['root_motion_speed']}\n")


            # Group by bone
            channels_by_bone = {}
            for ch in anim['channels']:
                bid = ch['boneIndex']
                if bid not in channels_by_bone:
                    channels_by_bone[bid] = {'pos': [], 'rot': [], 'scl': []}
                
                if ch['type'] == 'translation':
                    channels_by_bone[bid]['pos'] = ch['keys']
                    # Apply scale to translation keys
                    # If root bone, apply ONLY user scale_factor
                    # If child bone, apply user scale_factor * root_scale_mult
                    
                    # We need to know if this bone is a root or child.
                    # We can check if bones[bid]['parent'] == -1
                    
                    is_root = (bones[bid]['parent'] == -1)
                    current_scale = scale_factor
                    if not is_root:
                        current_scale *= root_scale_mult
                    
                    for k in channels_by_bone[bid]['pos']:
                        k['v'] = [x * current_scale for x in k['v']]
                elif ch['type'] == 'rotation':
                    channels_by_bone[bid]['rot'] = ch['keys']
                elif ch['type'] == 'scale':
                    channels_by_bone[bid]['scl'] = ch['keys']
            
            f.write(f"BoneChannelCount {len(channels_by_bone)}\n")
            for bid, keys in channels_by_bone.items():
                f.write(f"Channel {bid} {len(keys['pos'])} {len(keys['rot'])} {len(keys['scl'])}\n")
                for k in keys['pos']:
                    f.write(f"PosKey {k['t']} {k['v'][0]} {k['v'][1]} {k['v'][2]}\n")
                for k in keys['rot']:
                    f.write(f"RotKey {k['t']} {k['v'][0]} {k['v'][1]} {k['v'][2]} {k['v'][3]}\n")
                for k in keys['scl']:
                    f.write(f"ScaleKey {k['t']} {k['v'][0]} {k['v'][1]} {k['v'][2]}\n")

    print(f"Saved to {output_path}")

def convert_fbx_to_gltf(fbx_path):
    import subprocess
    import shutil
    
    # Check for FBX2glTF
    fbx2gltf = shutil.which("FBX2glTF")
    if fbx2gltf:
        print(f"Found FBX2glTF at {fbx2gltf}")
        output_glb = fbx_path.replace(".fbx", ".glb")
        try:
            subprocess.run([fbx2gltf, "-i", fbx_path, "-o", output_glb, "--binary"], check=True)
            return output_glb
        except subprocess.CalledProcessError as e:
            print(f"FBX2glTF failed: {e}")
            return None

    # Check for Assimp
    assimp = shutil.which("assimp")
    if assimp:
        print(f"Found Assimp at {assimp}")
        output_gltf = fbx_path.replace(".fbx", ".gltf")
        try:
            subprocess.run([assimp, "export", fbx_path, output_gltf], check=True)
            return output_gltf
        except subprocess.CalledProcessError as e:
            print(f"Assimp failed: {e}")
            return None
            
    return None

def read_accessor(gltf, accessor):
    buffer_view = gltf.bufferViews[accessor.bufferView]
    buffer = gltf.buffers[buffer_view.buffer]
    
    # Load binary data
    # In a real script we need to handle URI (embedded or external file)
    # For now assuming embedded or simple file relative path
    data = None
    if buffer.uri:
        # Check if data uri
        if buffer.uri.startswith("data:application/octet-stream;base64,"):
            import base64
            data = base64.b64decode(buffer.uri.split(",")[1])
        else:
            # External file
            # Assuming relative to gltf file
            # This part might need adjustment based on where the script is run
            pass 
            # For this snippet, we assume the user provides a .glb or embedded .gltf for simplicity
            # or we implement file reading.
            # Let's assume we are running this on a .glb or .gltf with external bin
            # If external bin, we need the path.
            pass

    # If we can't load data easily here without more context, 
    # we might want to rely on pygltflib's load_data() if available or just warn.
    # pygltflib doesn't automatically load external buffers by default unless we tell it.
    
    # Actually, let's use a simpler approach: 
    # If the user provides a GLB, the binary chunk is in gltf.binary_blob()
    
    blob = None
    if buffer.uri is None:
        # GLB binary chunk
        blob = gltf.binary_blob()
    elif buffer.uri.startswith("data:"):
        import base64
        blob = base64.b64decode(buffer.uri.split(",")[1])
    else:
        # External file
        # We need the base path of the gltf file
        # This is getting complicated for a simple script.
        # Let's assume GLB for now or embedded.
        print("Warning: External .bin files not fully supported in this simple script. Use .glb or embedded .gltf")
        return []

    if blob is None:
        return []

    # Calculate offset
    start = (buffer_view.byteOffset or 0) + (accessor.byteOffset or 0)
    
    # Determine format
    # componentType: 5126 (FLOAT), 5123 (UNSIGNED_SHORT), etc.
    # type: SCALAR, VEC3, VEC4
    
    count = accessor.count
    fmt_char = ""
    stride = 0
    
    if accessor.componentType == 5126: # FLOAT
        fmt_char = "f"
        stride = 4
    elif accessor.componentType == 5123: # UNSIGNED_SHORT
        fmt_char = "H"
        stride = 2
    elif accessor.componentType == 5121: # UNSIGNED_BYTE
        fmt_char = "B"
        stride = 1
    elif accessor.componentType == 5120: # BYTE
        fmt_char = "b"
        stride = 1
    
    num_components = 1
    if accessor.type == "VEC2": num_components = 2
    elif accessor.type == "VEC3": num_components = 3
    elif accessor.type == "VEC4": num_components = 4
    elif accessor.type == "MAT4": num_components = 16
    
    total_stride = stride * num_components
    
    # Handle bufferView byteStride
    if buffer_view.byteStride and buffer_view.byteStride > total_stride:
        # Interleaved data
        step = buffer_view.byteStride
    else:
        step = total_stride

    results = []
    current = start
    
    for i in range(count):
        chunk = blob[current : current + total_stride]
        unpacked = struct.unpack(f"<{num_components}{fmt_char}", chunk)
        results.append(list(unpacked) if num_components > 1 else unpacked[0])
        current += step
        
    return results

def voxelize_mesh(vertices, faces, joints, weights, node_index_to_bone_index, skin, ibms, scale_factor=1.0, pitch=0.05):
    if trimesh is None:
        return []

    print("Voxelizing mesh...")
    try:
        # Create mesh from vertices and faces
        # Vertices are in Bind Space (unscaled by root, unrotated)
        mesh = trimesh.Trimesh(vertices=vertices, faces=faces)
        
        # Apply scale to mesh
        # This scales it to the target size (including root scale if effective_scale_factor includes it)
        mesh.apply_scale(scale_factor)
        
        # Voxel pitch passed as argument
        print(f"Voxelizing with pitch: {pitch:.4f}")
        
        voxel_grid = mesh.voxelized(pitch=pitch)
        
        # Get filled voxel centers
        # voxel_grid.points are in the scaled mesh space
        
        # Build KD-Tree for skinning
        from scipy.spatial import cKDTree
        # Vertices are unscaled. We need to query with unscaled points.
        tree = cKDTree(vertices)
        
        voxel_shapes = []
        
        # Iterate voxels
        # voxel_grid.points gives (N, 3) array of centers
        centers = voxel_grid.points
        
        # Query nearest vertices
        # Centers are scaled. Unscale them to query the tree.
        unscaled_centers = centers / scale_factor
        dists, indices = tree.query(unscaled_centers)
        
        print(f"Generated {len(centers)} voxels.")
        
        for i, center in enumerate(centers):
            vertex_idx = indices[i]
            
            # Get skinning info for this vertex
            j = joints[vertex_idx]
            w = weights[vertex_idx]
            
            # Find dominant bone
            max_w = 0.0
            joint_idx = -1
            for k in range(4):
                if w[k] > max_w:
                    max_w = w[k]
                    joint_idx = j[k]
            
            target_bone_idx = -1
            if max_w > 0.4 and joint_idx >= 0 and joint_idx < len(skin.joints):
                target_node_idx = skin.joints[joint_idx]
                if target_node_idx in node_index_to_bone_index:
                    target_bone_idx = node_index_to_bone_index[target_node_idx]
            
            if target_bone_idx != -1:
                # Transform voxel center to bone local space
                # P_local = IBM * P_world
                # Note: 'center' is in mesh space. If mesh space == world space (identity transform on mesh node), this works.
                # If mesh has a transform, we need to apply it?
                # The 'vertices' passed to this function are raw accessor data (mesh space).
                # 'centers' are from trimesh, which likely matches mesh space if loaded correctly.
                
                # Use unscaled center for IBM transform
                p_unscaled = list(unscaled_centers[i])
                if joint_idx < len(ibms):
                    p_local_unscaled = transform_point(p_unscaled, ibms[joint_idx])
                    # Scale the local offset
                    p = [x * scale_factor for x in p_local_unscaled]
                else:
                    p = list(center) # Fallback if no IBM?
                
                voxel_shapes.append({
                    "boneId": target_bone_idx,
                    "size": [pitch, pitch, pitch],
                    "center": p # This is now the offset from the bone
                })
                
        return voxel_shapes

    except Exception as e:
        print(f"Voxelization failed: {e}")
        return []

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Extract animation and voxel data from GLTF/FBX")
    parser.add_argument("input_file", help="Input file (GLTF, GLB, FBX)")
    parser.add_argument("output_file", help="Output .anim file")
    parser.add_argument("--scale", type=float, default=1.0, help="Scale factor for the model and animation")
    parser.add_argument("--style", choices=['voxel', 'box'], default='voxel', help="Output style: 'voxel' (default) or 'box' (skeletal boxes only)")
    parser.add_argument("--rotate_x", type=float, default=0.0, help="Rotate model around X axis (degrees)")
    parser.add_argument("--rotate_y", type=float, default=0.0, help="Rotate model around Y axis (degrees)")
    parser.add_argument("--rotate_z", type=float, default=0.0, help="Rotate model around Z axis (degrees)")
    parser.add_argument("--animations", nargs='+', help="List of additional animation files (FBX/GLTF) to merge")
    parser.add_argument("--resolution", choices=['custom', 'cube', 'subcube', 'microcube'], default='custom', help="Voxel resolution mode")
    parser.add_argument("--voxel_size", type=float, default=0.05, help="Voxel size (pitch) when resolution is 'custom'")
    parser.add_argument("--target_height", type=float, help="Target height in world units to normalize scale (optional)")
    
    args = parser.parse_args()
    
    input_file = args.input_file
    output_file = args.output_file
    scale_factor = args.scale
    style = args.style
    rotate_x = args.rotate_x
    rotate_y = args.rotate_y
    rotate_z = args.rotate_z
    extra_animations = args.animations
    resolution = args.resolution
    voxel_size = args.voxel_size
    target_height = args.target_height
    temp_file = None
    
    ext = os.path.splitext(input_file)[1].lower()
    if ext == '.fbx':
        print("Detected FBX input. Checking for converters...")
        converted = convert_fbx_to_gltf(input_file)
        if converted:
            input_file = converted
            temp_file = converted
        else:
            print("Error: FBX file detected but no converter found.")
            print("Please install 'assimp' (and add to PATH) or 'FBX2glTF'.")
            print("Alternatively, convert the file to GLTF/GLB manually.")
            sys.exit(1)
            
    extract_animation_data(input_file, output_file, scale_factor, style, extra_animations, rotate_x, rotate_y, rotate_z, resolution, voxel_size, target_height)
    
    # Cleanup temp file if we created one
    if temp_file and os.path.exists(temp_file):
        try:
            os.remove(temp_file)
            # Assimp might create a .bin file too
            bin_file = temp_file.replace(".gltf", ".bin")
            if os.path.exists(bin_file):
                os.remove(bin_file)
        except:
            pass
