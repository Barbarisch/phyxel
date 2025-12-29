import sys
import os

def list_animations(file_path):
    if not os.path.exists(file_path):
        print(f"Error: File '{file_path}' not found.")
        return

    print(f"Scanning '{file_path}' for animations...")
    print("-" * 40)
    
    count = 0
    try:
        with open(file_path, 'r') as f:
            for line in f:
                if line.startswith("ANIMATION"):
                    parts = line.strip().split()
                    if len(parts) > 1:
                        anim_name = parts[1]
                        print(f"- {anim_name}")
                        count += 1
    except Exception as e:
        print(f"Error reading file: {e}")
        return

    print("-" * 40)
    print(f"Found {count} animations.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python list_anims.py <path_to_anim_file>")
        sys.exit(1)
    
    file_path = sys.argv[1]
    list_animations(file_path)
