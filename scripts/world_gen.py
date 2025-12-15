import phyxel
import time

def generate_pyramid(start_x, start_y, start_z, height):
    app = phyxel.get_app()
    cm = app.get_chunk_manager()
    
    phyxel.Logger.info("WorldGen", f"Generating pyramid at ({start_x}, {start_y}, {start_z}) with height {height}")
    
    count = 0
    for y in range(height):
        layer_radius = height - y - 1
        for x in range(-layer_radius, layer_radius + 1):
            for z in range(-layer_radius, layer_radius + 1):
                cm.add_cube(start_x + x, start_y + y, start_z + z)
                count += 1
    return count

def generate_platform(x, y, z, width, depth):
    app = phyxel.get_app()
    cm = app.get_chunk_manager()
    
    phyxel.Logger.info("WorldGen", f"Generating platform at ({x}, {y}, {z}) size {width}x{depth}")
    
    for i in range(width):
        for k in range(depth):
            cm.add_cube(x + i, y, z + k)

def generate_glow_pillars(x, y, z, height):
    app = phyxel.get_app()
    cm = app.get_chunk_manager()
    
    phyxel.Logger.info("WorldGen", f"Generating glow pillar at ({x}, {y}, {z}) height {height}")
    
    for i in range(height):
        cm.add_cube_with_material(x, y + i, z, "glow")

def run_demo():
    phyxel.Logger.info("WorldGen", "Starting World Generation Demo...")
    
    app = phyxel.get_app()
    
    # 1. Teleport Player
    im = app.get_input_manager()
    if im:
        # Move player to a good viewing position (looking down at 32,35,32)
        im.set_camera_position(60.0, 60.0, 60.0)
        # Set orientation to look somewhat down and towards origin
        im.set_yaw_pitch(-135.0, -30.0)
        phyxel.Logger.info("WorldGen", "Teleported player to vantage point")

    # 2. Generate Geometry
    generate_platform(20, 35, 20, 40, 40)
    generate_pyramid(39, 36, 39, 4)
    
    # Generate glow pillars
    generate_glow_pillars(25, 36, 25, 5)
    generate_glow_pillars(55, 36, 25, 5)
    generate_glow_pillars(25, 36, 55, 5)
    generate_glow_pillars(55, 36, 55, 5)
    
    # 3. Spawn Templates
    tm = app.get_object_template_manager()
    if tm:
        # Spawn a static sphere (heavy geometry, but fast to render as static voxels)
        tm.spawn_template("sphere", 33.0, 36.0, 33.0, True)
        phyxel.Logger.info("WorldGen", "Spawned static sphere")
        
        # Spawn a dynamic tree (lighter physics)
        tm.spawn_template("tree", 39.0, 60.0, 39.0, False)
        phyxel.Logger.info("WorldGen", "Spawned dynamic tree above pyramid")

    phyxel.Logger.info("WorldGen", "Demo complete!")

