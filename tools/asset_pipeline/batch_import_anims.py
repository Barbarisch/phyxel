import os
import sys
import argparse
import subprocess
import shutil
from pathlib import Path

# Since this script is now in the same directory as extract_animation.py,
# we can import it directly.
try:
    from extract_animation import extract_animation_data
except ImportError:
    # Fallback if run from a different working directory
    current_dir = Path(__file__).parent.absolute()
    sys.path.append(str(current_dir))
    try:
        from extract_animation import extract_animation_data
    except ImportError:
        print("Error: Could not import extract_animation_data. Make sure it is in the same directory.")
        sys.exit(1)

def convert_fbx_to_gltf(fbx_path, output_dir, converter_path):
    """
    Converts an FBX file to GLB using FBX2glTF.
    Returns the path to the converted file, or None if failed.
    """
    # Check if converter exists (either full path or in PATH)
    if not shutil.which(converter_path) and not os.path.exists(converter_path):
        print(f"Error: FBX2glTF executable not found at '{converter_path}'")
        return None

    filename = os.path.basename(fbx_path)
    name_without_ext = os.path.splitext(filename)[0]
    output_path = os.path.join(output_dir, name_without_ext + ".glb")

    # If already exists and newer, skip
    if os.path.exists(output_path):
        if os.path.getmtime(output_path) > os.path.getmtime(fbx_path):
            print(f"Skipping {filename} (already converted)")
            return output_path

    print(f"Converting {filename} to GLB...")
    
    # Command: FBX2glTF --binary --input <input> --output <output>
    cmd = [converter_path, "--binary", "--input", fbx_path, "--output", output_path]
    
    try:
        # Capture output to avoid cluttering terminal unless error
        subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return output_path
    except subprocess.CalledProcessError as e:
        print(f"Failed to convert {filename}:")
        print(e.stderr.decode())
        return None

def main():
    parser = argparse.ArgumentParser(description="Batch import animations from a directory, converting FBX if needed.")
    parser.add_argument("input_dir", help="Directory containing animation files (FBX/GLTF/GLB)")
    parser.add_argument("--skin", help="Filename of the skin/mesh file (e.g. skin.fbx). If not provided, tries to find 'skin', 'mesh', or 't-pose'.")
    parser.add_argument("--out", required=True, help="Output .anim file path")
    parser.add_argument("--fbx2gltf", default="FBX2glTF", help="Path to FBX2glTF executable (default: assumes in PATH)")
    parser.add_argument("--scale", type=float, default=1.0, help="Scale factor for the model")

    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    if not input_dir.exists():
        print(f"Error: Input directory '{input_dir}' does not exist.")
        return

    # Create a temp directory for conversions if needed
    temp_dir = input_dir / "converted_gltf"
    
    # Gather files
    files = list(input_dir.glob("*"))
    
    skin_file_path = None
    anim_files = []

    # 1. Identify Skin File
    potential_skins = []
    if args.skin:
        potential_skins = [input_dir / args.skin]
    else:
        # Heuristics
        for f in files:
            if f.is_dir(): continue
            lower_name = f.name.lower()
            if "skin" in lower_name or "mesh" in lower_name or "t-pose" in lower_name:
                potential_skins.append(f)
    
    if not potential_skins:
        print("Error: Could not identify a skin file. Please specify one with --skin")
        print("Available files:")
        for f in files:
            if not f.is_dir(): print(f" - {f.name}")
        return
    
    # Pick the first one found or specified
    raw_skin_file = potential_skins[0]
    if not raw_skin_file.exists():
         print(f"Error: Skin file '{raw_skin_file}' not found.")
         return

    print(f"Using skin file: {raw_skin_file.name}")

    # Ensure temp dir exists if we are going to use it
    if any(f.suffix.lower() == ".fbx" for f in files):
        temp_dir.mkdir(exist_ok=True)

    # Convert skin if FBX
    final_skin_path = str(raw_skin_file)
    if raw_skin_file.suffix.lower() == ".fbx":
        final_skin_path = convert_fbx_to_gltf(str(raw_skin_file), str(temp_dir), args.fbx2gltf)
        if not final_skin_path:
            print("Aborting due to skin conversion failure.")
            return

    # 2. Process Animations
    for f in files:
        # Skip the skin file itself (original)
        if f.resolve() == raw_skin_file.resolve():
            continue
        
        if f.is_dir():
            continue

        if f.suffix.lower() not in ['.fbx', '.gltf', '.glb']:
            continue

        # Convert if FBX
        final_anim_path = str(f)
        if f.suffix.lower() == ".fbx":
            final_anim_path = convert_fbx_to_gltf(str(f), str(temp_dir), args.fbx2gltf)
        
        if final_anim_path:
            anim_files.append(final_anim_path)

    if not anim_files:
        print("No animation files found.")
        return

    print(f"Found {len(anim_files)} animations.")

    # 3. Extract and Combine
    print(f"Generating {args.out}...")
    try:
        extract_animation_data(
            gltf_path=final_skin_path,
            output_path=args.out,
            scale_factor=args.scale,
            style='voxel', # Defaulting to voxel style as per context
            extra_animations=anim_files
        )
        print(f"Successfully created {args.out}")
    except Exception as e:
        print(f"Error during extraction: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()
