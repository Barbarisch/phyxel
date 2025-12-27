import phyxel
import sys
import audio_demo

def spawn_characters():
    app = phyxel.get_app()
    if not app:
        phyxel.Logger.error("Script", "Failed to get application instance")
        return

    phyxel.Logger.info("Script", "Spawning characters from Python...")

    # Create Physics Character
    #app.create_physics_character(30, 50, 30)
    
    # Create Spider Character
    #app.create_spider_character(35, 55, 35)

    # Create Animated Voxel Character
    # Note: We use the generated animation file
    #anim_char = app.create_animated_character(40, 50, 40, "character_complete.anim")
    anim_char = app.create_animated_character(40, 50, 40, "resources\\character_female.anim")
    if anim_char:
        # Default animation is handled in C++ loadModel, but we can force it here too
        anim_char.play_animation("idle")

    # Default to controlling the PhysicsCharacter
    app.set_control_target("physics")
    
    phyxel.Logger.info("Script", "Characters spawned successfully!")

def on_start():
    phyxel.Logger.info("Script", "Startup script executed!")
    
    try:
        import world_gen
        world_gen.run_demo()
    except Exception as e:
        phyxel.Logger.error("Script", f"Failed to run world_gen: {e}")

    spawn_characters()

    phyxel.Logger.info("Script", "Audio Demo loaded. Type 'audio_demo.help()' for commands.")

if __name__ == "__main__":
    on_start()
