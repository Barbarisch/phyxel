import phyxel
import sys
import audio_demo

def on_start():
    phyxel.Logger.info("Script", "Startup script executed!")
    
    try:
        import world_gen
        world_gen.run_demo()
    except Exception as e:
        phyxel.Logger.error("Script", f"Failed to run world_gen: {e}")

    phyxel.Logger.info("Script", "Audio Demo loaded. Type 'audio_demo.help()' for commands.")

if __name__ == "__main__":
    on_start()
