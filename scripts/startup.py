import phyxel
import sys
import audio_demo

# Global reference to the character for console access
anim_char = None

def map_animation_states_spider(anim_char):
    # Map generic states to specific animation names found in the file
    # Use 'tools/list_anims.py resources/character_spider.anim' to see all names
    anim_char.set_animation_mapping("Idle", "Spider_Armature|warte_pose")
    anim_char.set_animation_mapping("Walk", "Spider_Armature|walk_ani_vor")
    anim_char.set_animation_mapping("Run", "Spider_Armature|run_ani_vor")
    anim_char.set_animation_mapping("Jump", "Spider_Armature|Jump")
    anim_char.set_animation_mapping("Attack", "Spider_Armature|Attack")
    anim_char.set_animation_mapping("StrafeLeft", "Spider_Armature|walk_left")
    anim_char.set_animation_mapping("StrafeRight", "Spider_Armature|walk_right")
    
    # Map missing transition states to their main counterparts
    anim_char.set_animation_mapping("StartWalk", "Spider_Armature|walk_ani_vor")
    anim_char.set_animation_mapping("Fall", "Spider_Armature|fall")
    anim_char.set_animation_mapping("Land", "Spider_Armature|warte_pose") # Use idle for landing
    
    # Default animation is handled in C++ loadModel, but we can force it here too
    anim_char.play_animation("Spider_Armature|warte_pose")

def map_animation_states_female2(anim_char):
    # Map generic states to specific animation names found in the file
    # Use 'tools/list_anims.py resources/character_female2.anim' to see all names
    anim_char.set_animation_mapping("Idle", "Idle")
    anim_char.set_animation_mapping("Walk", "Walk")
    anim_char.set_animation_mapping("Run", "Run")
    anim_char.set_animation_mapping("Jump", "Jump")
    anim_char.set_animation_mapping("Attack", "Attack")
    anim_char.set_animation_mapping("StrafeLeft", "Left_Strafe_Walk")
    anim_char.set_animation_mapping("StrafeRight", "Right_Strafe_Walk")
    
    # Map missing transition states to their main counterparts
    anim_char.set_animation_mapping("StartWalk", "Walk")
    anim_char.set_animation_mapping("Fall", "Fall")
    anim_char.set_animation_mapping("Land", "Idle") # Use idle for landing
    
    # Default animation is handled in C++ loadModel, but we can force it here too
    anim_char.play_animation("Idle")

def map_animation_states_wolf(anim_char):
    # Map generic states to specific animation names found in the file
    # Use 'tools/list_anims.py resources/character_wolf.anim' to see all names
    anim_char.set_animation_mapping("Idle", "Wolf_with_Animations_04_Idle")
    anim_char.set_animation_mapping("Walk", "Wolf_with_Animations_02_walk")
    anim_char.set_animation_mapping("Run", "Wolf_with_Animations_01_Run")
    #anim_char.set_animation_mapping("Jump", "Jump")
    #anim_char.set_animation_mapping("Attack", "Attack")
    #anim_char.set_animation_mapping("StrafeLeft", "Strafe_Left")
    #anim_char.set_animation_mapping("StrafeRight", "Strafe_Right")
    
    # Map missing transition states to their main counterparts
    #anim_char.set_animation_mapping("StartWalk", "Walk")
    #anim_char.set_animation_mapping("Fall", "Fall")
    #anim_char.set_animation_mapping("Land", "Idle") # Use idle for landing
    
    # Default animation is handled in C++ loadModel, but we can force it here too
    anim_char.play_animation("Idle")

def map_animation_states_dragon(anim_char):
    # Map generic states to specific animation names found in the file
    # Use 'tools/list_anims.py resources/character_dragon.anim' to see all names
    #anim_char.set_animation_mapping("Idle", "Dragon_Baked_Actions_fbx_7.4_binary_Armature|Idel_New")
    #anim_char.set_animation_mapping("Walk", "Dragon_Baked_Actions_fbx_7.4_binary_Armature|Walk_New")
    #anim_char.set_animation_mapping("Run", "Dragon_Baked_Actions_fbx_7.4_binary_Armature|Run_New")
    # anim_char.set_animation_mapping("Jump", "Dragon_Armature|Jump")
    # anim_char.set_animation_mapping("Attack", "Dragon_Armature|Attack")
    # anim_char.set_animation_mapping("StrafeLeft", "Dragon_Armature|Strafe_Left")
    # anim_char.set_animation_mapping("StrafeRight", "Dragon_Armature|Strafe_Right")
    
    # Map missing transition states to their main counterparts
    #anim_char.set_animation_mapping("StartWalk", "Dragon_Baked_Actions_fbx_7.4_binary_Armature|Walk_New")
    #anim_char.set_animation_mapping("Fall", "Dragon_Armature|Fall")
    # anim_char.set_animation_mapping("Land", "Dragon_Baked_Actions_fbx_7.4_binary_Armature|Idel_New") # Use idle for landing
    
    # Default animation is handled in C++ loadModel, but we can force it here too
    #anim_char.play_animation("Dragon_Baked_Actions_fbx_7.4_binary_Armature|Idel_New")
    pass

def spawn_characters():
    global anim_char
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
    #anim_char = app.create_animated_character(40, 50, 40, "resources\\character_female3.anim")
    #anim_char = app.create_animated_character(40, 50, 40, "resources\\character_spider.anim")
    #anim_char = app.create_animated_character(40, 50, 40, "resources\\character_spider3.anim")
    #anim_char = app.create_animated_character(40, 50, 40, "resources\\character_dragon.anim")
    anim_char = app.create_animated_character(40, 50, 40, "resources\\character_wolf.anim")
    if anim_char:
        #map_animation_states_spider(anim_char)
        #map_animation_states_female2(anim_char)
        #map_animation_states_dragon(anim_char)
        #map_animation_states_wolf(anim_char)
        pass

    # Default to controlling the PhysicsCharacter
    #app.set_control_target("physics")
    app.set_control_target("animated")
    
    phyxel.Logger.info("Script", "Characters spawned successfully!")
    
    # TEST: Derez the character immediately to test debris system
    # phyxel.Logger.info("Script", "Triggering derez for testing...")
    # app.derez_character(0.0) # 0.0 for "fall in place"

def derez(strength=1.0):
    """Helper function to derez the character with a specific explosion strength."""
    app = phyxel.get_app()
    if app:
        app.derez_character(strength)
        phyxel.Logger.info("Script", f"Derezzing character with strength {strength}")

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
