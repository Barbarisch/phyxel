import json
import struct
import sys
import os
import math

# Try to import pygltflib, if not present, ask user to install
try:
    from pygltflib import GLTF2
except ImportError:
    print("Error: pygltflib is required. Please install it using: pip install pygltflib")
    sys.exit(1)

def extract_animation_data(gltf_path, output_path):
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
        
    # Iterate meshes
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
                    
                    # If no skinning, check if this node IS a bone or attached to one
                    current_bone_idx = -1
                    if not has_skinning:
                        if node_idx in node_index_to_bone_index:
                            current_bone_idx = node_index_to_bone_index[node_idx]
                        else:
                            # Check parent? We don't have parent map easily here unless we built it.
                            # But we built 'bones' list which has 'parent'.
                            # node_index_to_bone_index maps gltf node to bone index.
                            pass

                    for i in range(len(positions)):
                        p = positions[i]
                        
                        target_bone_idx = -1
                        
                        if has_skinning and skin:
                            j = joints[i]    # [j0, j1, j2, j3]
                            w = weights[i]   # [w0, w1, w2, w3]
                            
                            # Find dominant bone
                            max_w = 0.0
                            joint_idx = -1
                            for k in range(4):
                                if w[k] > max_w:
                                    max_w = w[k]
                                    joint_idx = j[k]
                            
                            if max_w > 0.4 and joint_idx >= 0 and joint_idx < len(skin.joints):
                                target_node_idx = skin.joints[joint_idx]
                                if target_node_idx in node_index_to_bone_index:
                                    target_bone_idx = node_index_to_bone_index[target_node_idx]
                                    
                                    # Transform point to bone local space using IBM
                                    # P_local = IBM * P_mesh
                                    if joint_idx < len(ibms):
                                        p = transform_point(p, ibms[joint_idx])
                        
                        elif current_bone_idx != -1:
                            # Rigid binding to current node (which is a bone)
                            target_bone_idx = current_bone_idx
                            # Point is already in node/bone local space (mesh data)
                            # Apply node scale if present, because physics shapes don't inherit scale from transform
                            if node.scale:
                                p = [p[0] * node.scale[0], p[1] * node.scale[1], p[2] * node.scale[2]]
                        
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
                            
                            if has_skinning:
                                j = joints[i]    # [j0, j1, j2, j3]
                                w = weights[i]   # [w0, w1, w2, w3]
                                
                                # Find dominant bone
                                max_w = 0.0
                                joint_idx = -1
                                for k in range(4):
                                    if w[k] > max_w:
                                        max_w = w[k]
                                        joint_idx = j[k]
                                
                                if max_w > 0.4 and joint_idx >= 0 and joint_idx < len(skin.joints):
                                    target_node_idx = skin.joints[joint_idx]
                                    if target_node_idx in node_index_to_bone_index:
                                        target_bone_idx = node_index_to_bone_index[target_node_idx]
                                        
                                        # Transform point to bone local space using IBM
                                        if joint_idx < len(ibms):
                                            p = transform_point(p, ibms[joint_idx])
                            
                            elif current_bone_idx != -1:
                                # Rigid binding to current node (which is a bone)
                                target_bone_idx = current_bone_idx
                                # Point is already in node/bone local space (mesh data)
                                # No transform needed if mesh is directly on the bone node
                                pass
                            
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
        
        # Write Model Data (Bounding Boxes)
        f.write("MODEL\n")
        f.write(f"BoxCount {len(bone_boxes)}\n")
        for bid, bbox in bone_boxes.items():
            # Calculate center (offset) and size
            min_pt = bbox["min"]
            max_pt = bbox["max"]
            
            size = [max_pt[0] - min_pt[0], max_pt[1] - min_pt[1], max_pt[2] - min_pt[2]]
            center = [(max_pt[0] + min_pt[0])/2, (max_pt[1] + min_pt[1])/2, (max_pt[2] + min_pt[2])/2]
            
            # Ensure minimum size to avoid physics errors
            size = [max(0.05, s) for s in size]
            
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

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python extract_animation.py <input.gltf/glb> <output.anim>")
    else:
        extract_animation_data(sys.argv[1], sys.argv[2])
