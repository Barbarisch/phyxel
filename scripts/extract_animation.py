import json
import struct
import sys
import os
import math
import numpy as np
import argparse

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
except ImportError:
    print("Warning: trimesh/scipy not found. Voxelization will be skipped.")
    trimesh = None

def extract_animation_data(gltf_path, output_path, scale_factor=1.0):
    print(f"Loading {gltf_path}...")
    gltf = GLTF2().load(gltf_path)

    # 1. Extract Skeleton (Nodes)
    # We need to find the root node of the skeleton. 
    # Often scenes have a default scene.
    
    bones = []
    bone_map = {} # name -> index in our bones list
    node_index_to_bone_index = {} # gltf node index -> our bone index

    # Helper to get node hierarchy
    def process_node(node_idx, parent_bone_idx):
        node = gltf.nodes[node_idx]
        
        # Extract transform
        pos = node.translation if node.translation else [0.0, 0.0, 0.0]
        rot = node.rotation if node.rotation else [0.0, 0.0, 0.0, 1.0] # x, y, z, w
        scale = node.scale if node.scale else [1.0, 1.0, 1.0]

        # Apply global scale to position (translation)
        pos = [p * scale_factor for p in pos]

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

    # 2. Extract Animations
    animations_out = []

    if gltf.animations:
        for anim in gltf.animations:
            print(f"Processing animation: {anim.name}")
            channels_out = []
            
            for channel in anim.channels:
                target_node_idx = channel.target.node
                if target_node_idx not in node_index_to_bone_index:
                    continue # Animation targets a node not in our skeleton
                
                bone_idx = node_index_to_bone_index[target_node_idx]
                path = channel.target.path # translation, rotation, scale
                
                sampler = anim.samplers[channel.sampler]
                input_accessor = gltf.accessors[sampler.input]
                output_accessor = gltf.accessors[sampler.output]
                
                # Read Input (Time)
                times = read_accessor(gltf, input_accessor)
                
                # Read Output (Values)
                values = read_accessor(gltf, output_accessor)
                
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
                "name": anim.name if anim.name else "anim",
                "channels": channels_out
            })

    # 1.5 Extract Bone Bounding Boxes (The "Model")
    # We need to process meshes and find which vertices belong to which bone.
    bone_boxes = {} # bone_index -> {min: [inf,inf,inf], max: [-inf,-inf,-inf]}
    
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
    
    # Iterate meshes to collect data
    print(f"Scanning {len(gltf.nodes)} nodes for meshes...")
    for node_idx, node in enumerate(gltf.nodes):
        if node.mesh is not None:
            print(f"Found mesh on node {node_idx} ({node.name})")
            # Check if this node uses the skin (or if the skin is applied globally)
            # Usually the node with the mesh has the 'skin' property
            if node.skin is not None or True: # Assuming all meshes are part of the character for now
                mesh = gltf.meshes[node.mesh]
                for prim in mesh.primitives:
                    if prim.attributes.POSITION is None:
                        continue

                    pos_accessor = gltf.accessors[prim.attributes.POSITION]
                    positions = read_accessor(gltf, pos_accessor)
                    
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
                        joints = [[node_idx, 0, 0, 0]] * len(positions)
                        weights = [[1.0, 0, 0, 0]] * len(positions)
                        
                        # Apply node scale to positions to match World Space/Trimesh behavior
                        if node.scale:
                            s = node.scale
                            positions = [[p[0]*s[0], p[1]*s[1], p[2]*s[2]] for p in positions]
                    
                    all_positions.extend(positions)
                    all_joints.extend(joints)
                    all_weights.extend(weights)

    voxel_shapes = []
    if trimesh is not None:
        voxel_shapes = voxelize_mesh(gltf_path, all_positions, all_joints, all_weights, node_index_to_bone_index, skin, ibms, scale_factor)

    # Fallback or if voxelization produced nothing (e.g. no skinning found or trimesh failed)
    if not voxel_shapes:
        print("Falling back to bounding box generation (or using it for physics)...")
        
        bone_boxes = {}
        
        for i in range(len(all_positions)):
            p = all_positions[i]
            # Apply scale to vertex position for bounding box calculation
            p = [x * scale_factor for x in p]
            
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
                    
                    p_unscaled = [x / scale_factor for x in p]
                    if skin and joint_idx < len(ibms):
                        p_local = transform_point(p_unscaled, ibms[joint_idx])
                        p = [x * scale_factor for x in p_local]
            
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


            # Group by bone
            channels_by_bone = {}
            for ch in anim['channels']:
                bid = ch['boneIndex']
                if bid not in channels_by_bone:
                    channels_by_bone[bid] = {'pos': [], 'rot': [], 'scl': []}
                
                if ch['type'] == 'translation':
                    channels_by_bone[bid]['pos'] = ch['keys']
                    # Apply scale to translation keys
                    for k in channels_by_bone[bid]['pos']:
                        k['v'] = [x * scale_factor for x in k['v']]
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

def voxelize_mesh(gltf_path, vertices, joints, weights, node_index_to_bone_index, skin, ibms, scale_factor=1.0):
    if trimesh is None:
        return []

    print("Voxelizing mesh...")
    try:
        # Load mesh
        mesh = trimesh.load(gltf_path, force='mesh')
        
        # Apply scale to mesh
        mesh.apply_scale(scale_factor)
        
        # Scale to a reasonable size if needed, but we want to match the skeleton scale.
        # The skeleton extraction uses the raw GLTF units.
        # So we should voxelize in the same units.
        
        # Voxel pitch: 0.05 seems to be the "pixel" size in the engine (based on previous heuristic)
        # Or maybe 0.1. The user said "match the game engine voxels".
        # In obj_to_template.py, pitch is 1.0/9.0 (~0.11) or 1.0.
        # Let's try 0.05 for high detail, or 0.1.
        pitch = 0.05 
        
        voxel_grid = mesh.voxelized(pitch=pitch)
        
        # Get filled voxel centers
        # voxel_grid.points are in the mesh's local space (which should match GLTF world space if no transforms on mesh node)
        # Wait, trimesh loads the geometry. If the GLTF has a node transform, trimesh might or might not apply it.
        # Usually trimesh.load(glb) loads the scene. force='mesh' merges it.
        # If we use the vertices from the GLTF accessor directly for KD-Tree, they are in the mesh primitive space.
        # We need to ensure the voxel grid is also in that space.
        
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
    
    args = parser.parse_args()
    
    input_file = args.input_file
    output_file = args.output_file
    scale_factor = args.scale
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
            
    extract_animation_data(input_file, output_file, scale_factor)
    
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
