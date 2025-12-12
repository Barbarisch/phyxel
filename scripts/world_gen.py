import phyxel
import time

def generate_pyramid(start_x, start_y, start_z, height):
    app = phyxel.get_app()
    if not app:
        phyxel.Logger.error("WorldGen", "Failed to get application instance")
        return

    cm = app.get_chunk_manager()
    if not cm:
        phyxel.Logger.error("WorldGen", "Failed to get chunk manager")
        return

    phyxel.Logger.info("WorldGen", f"Generating pyramid at ({start_x}, {start_y}, {start_z}) with height {height}")

    count = 0
    for y in range(height):
        # Size of the layer (decreases as we go up)
        # Base layer (y=0) has size 'height'
        # Top layer (y=height-1) has size 1
        layer_radius = height - y - 1
        
        for x in range(-layer_radius, layer_radius + 1):
            for z in range(-layer_radius, layer_radius + 1):
                world_x = start_x + x
                world_y = start_y + y
                world_z = start_z + z
                
                cm.add_cube(world_x, world_y, world_z)
                count += 1
    
    phyxel.Logger.info("WorldGen", f"Pyramid generation complete. Placed {count} cubes.")

def generate_platform(x, y, z, width, depth):
    app = phyxel.get_app()
    cm = app.get_chunk_manager()
    
    phyxel.Logger.info("WorldGen", f"Generating platform at ({x}, {y}, {z}) size {width}x{depth}")
    
    for i in range(width):
        for k in range(depth):
            cm.add_cube(x + i, y, z + k)

def run_demo():
    phyxel.Logger.info("WorldGen", "Starting World Generation Demo...")
    
    # Generate a platform
    generate_platform(32, 35, 32, 10, 10)
    
    # Generate a pyramid on top of it
    generate_pyramid(37, 36, 37, 4)
    
    phyxel.Logger.info("WorldGen", "Demo complete!")

if __name__ == "__main__":
    run_demo()
