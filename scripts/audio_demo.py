import phyxel
import math

def play_beacon(x, y, z):
    """Plays a click sound at the given location."""
    app = phyxel.get_app()
    audio = app.get_audio_system()
    # Play click at location
    audio.play_sound_3d("resources/sounds/click.wav", x, y, z, phyxel.AudioChannel.SFX, 1.0, 0.0, 0.0, 0.0)
    phyxel.Logger.info("AudioDemo", f"Beacon ping at ({x}, {y}, {z})")

def test_doppler():
    """Simulates a fast object flying past the player."""
    app = phyxel.get_app()
    audio = app.get_audio_system()
    input_mgr = app.get_input_manager()
    
    # Get player pos
    px, py, pz = input_mgr.get_camera_position()
    
    # Source starts 20 units to the left
    sx = px - 20
    sy = py
    sz = pz
    
    # Velocity: Moving right at 50 units/sec (approx 180 km/h)
    vx = 50.0
    vy = 0.0
    vz = 0.0
    
    audio.play_sound_3d("resources/sounds/whoosh.wav", sx, sy, sz, phyxel.AudioChannel.SFX, 1.0, vx, vy, vz)
    phyxel.Logger.info("AudioDemo", "Doppler test: Whoosh flying right!")

def play_near_me(offset_x=0, offset_y=0, offset_z=0):
    """Plays a click sound relative to the player's current position."""
    app = phyxel.get_app()
    audio = app.get_audio_system()
    input_mgr = app.get_input_manager()
    
    px, py, pz = input_mgr.get_camera_position()
    tx, ty, tz = px + offset_x, py + offset_y, pz + offset_z
    
    audio.play_sound_3d("resources/sounds/click.wav", tx, ty, tz, phyxel.AudioChannel.SFX, 1.0, 0.0, 0.0, 0.0)
    phyxel.Logger.info("AudioDemo", f"Played click at ({tx:.1f}, {ty:.1f}, {tz:.1f})")

def help():
    phyxel.Logger.info("AudioDemo", "Available commands:")
    phyxel.Logger.info("AudioDemo", "  audio_demo.play_beacon(x, y, z) - Play a 3D click at world coords")
    phyxel.Logger.info("AudioDemo", "  audio_demo.play_near_me(x, y, z)- Play relative to player")
    phyxel.Logger.info("AudioDemo", "  audio_demo.test_doppler()       - Simulate a flyby")
