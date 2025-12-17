import wave
import math
import struct
import random
import os

def generate_click(filename, duration=0.1, frequency=1000, sample_rate=44100):
    print(f"Generating {filename}...")
    num_samples = int(duration * sample_rate)
    
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)  # Mono
        wav_file.setsampwidth(2)  # 2 bytes per sample (16-bit PCM)
        wav_file.setframerate(sample_rate)
        
        for i in range(num_samples):
            t = i / sample_rate
            # Simple sine wave with exponential decay
            envelope = math.exp(-t * 20) 
            value = int(32767.0 * envelope * math.sin(2.0 * math.pi * frequency * t))
            data = struct.pack('<h', value)
            wav_file.writeframes(data)
    print(f"Saved {filename}")

def generate_whoosh(filename, duration=1.0, sample_rate=44100):
    print(f"Generating {filename}...")
    num_samples = int(duration * sample_rate)
    
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)  # Mono
        wav_file.setsampwidth(2)  # 2 bytes per sample (16-bit PCM)
        wav_file.setframerate(sample_rate)
        
        for i in range(num_samples):
            t = i / sample_rate
            # White noise with sine window for "whoosh" effect (fade in, fade out)
            envelope = math.sin(math.pi * t / duration) 
            noise = random.uniform(-1.0, 1.0)
            
            # Add some low frequency oscillation to make it sound more like air
            lfo = math.sin(2.0 * math.pi * 5.0 * t) * 0.2
            
            value = int(32767.0 * envelope * (noise * 0.8 + lfo))
            # Clamp
            value = max(-32768, min(32767, value))
            
            data = struct.pack('<h', value)
            wav_file.writeframes(data)
    print(f"Saved {filename}")

if __name__ == "__main__":
    output_dir = "resources/sounds"
    os.makedirs(output_dir, exist_ok=True)
    
    generate_click(os.path.join(output_dir, "click.wav"))
    generate_whoosh(os.path.join(output_dir, "whoosh.wav"))
