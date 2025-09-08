#!/usr/bin/env python3
"""
Texture Atlas Builder for Phyxel Engine
Combines multiple 18x18 PNG textures into a single atlas texture.
Generates offset mappings for GPU shader access.
"""

import os
import json
import math
from PIL import Image
import argparse
from pathlib import Path

class TextureAtlasBuilder:
    def __init__(self, texture_size=18, padding=1):
        self.texture_size = texture_size
        self.padding = padding
        self.effective_size = texture_size + (2 * padding)  # Include padding
        
    def find_optimal_atlas_size(self, num_textures):
        """Find optimal atlas dimensions that are power-of-2"""
        # Calculate minimum area needed
        total_area = num_textures * (self.effective_size ** 2)
        min_dimension = math.ceil(math.sqrt(total_area))
        
        # Round up to next power of 2
        atlas_size = 2 ** math.ceil(math.log2(min_dimension))
        
        # Calculate how many textures fit per row/column
        textures_per_row = atlas_size // self.effective_size
        textures_per_col = atlas_size // self.effective_size
        
        # If we can't fit all textures, increase atlas size
        while (textures_per_row * textures_per_col) < num_textures:
            atlas_size *= 2
            textures_per_row = atlas_size // self.effective_size
            textures_per_col = atlas_size // self.effective_size
            
        return atlas_size, textures_per_row, textures_per_col
    
    def load_textures(self, input_dir):
        """Load all PNG textures from input directory"""
        textures = {}
        input_path = Path(input_dir)
        
        if not input_path.exists():
            raise FileNotFoundError(f"Input directory not found: {input_dir}")
        
        png_files = sorted(input_path.glob("*.png"))
        
        for png_file in png_files:
            try:
                img = Image.open(png_file)
                
                # Validate size
                if img.size != (self.texture_size, self.texture_size):
                    print(f"Warning: {png_file.name} is {img.size}, expected {self.texture_size}x{self.texture_size}")
                    img = img.resize((self.texture_size, self.texture_size), Image.NEAREST)
                
                # Convert to RGBA
                if img.mode != 'RGBA':
                    img = img.convert('RGBA')
                
                # Use filename without extension as texture name
                texture_name = png_file.stem
                textures[texture_name] = img
                
            except Exception as e:
                print(f"Error loading {png_file}: {e}")
        
        return textures
    
    def build_atlas(self, textures, output_path, generate_metadata=True):
        """Build texture atlas from loaded textures"""
        num_textures = len(textures)
        if num_textures == 0:
            raise ValueError("No textures to process")
        
        # Calculate optimal atlas dimensions
        atlas_size, textures_per_row, textures_per_col = self.find_optimal_atlas_size(num_textures)
        
        print(f"Building atlas:")
        print(f"  Texture size: {self.texture_size}x{self.texture_size}")
        print(f"  Padding: {self.padding}px")
        print(f"  Effective size: {self.effective_size}x{self.effective_size}")
        print(f"  Atlas size: {atlas_size}x{atlas_size}")
        print(f"  Grid: {textures_per_row}x{textures_per_col}")
        print(f"  Total textures: {num_textures}")
        
        # Create atlas image
        atlas = Image.new('RGBA', (atlas_size, atlas_size), (0, 0, 0, 0))
        
        # Metadata for shader access
        texture_metadata = {
            "atlas_size": atlas_size,
            "texture_size": self.texture_size,
            "padding": self.padding,
            "effective_size": self.effective_size,
            "textures_per_row": textures_per_row,
            "textures": {}
        }
        
        # Place textures in atlas
        texture_index = 0
        for texture_name, texture_img in textures.items():
            # Calculate grid position
            grid_x = texture_index % textures_per_row
            grid_y = texture_index // textures_per_row
            
            # Calculate pixel position (including padding)
            pixel_x = grid_x * self.effective_size + self.padding
            pixel_y = grid_y * self.effective_size + self.padding
            
            # Place texture in atlas
            atlas.paste(texture_img, (pixel_x, pixel_y))
            
            # Calculate UV coordinates for shaders
            u_min = pixel_x / atlas_size
            v_min = pixel_y / atlas_size
            u_max = (pixel_x + self.texture_size) / atlas_size
            v_max = (pixel_y + self.texture_size) / atlas_size
            
            # Store metadata
            texture_metadata["textures"][texture_name] = {
                "index": texture_index,
                "grid_pos": [grid_x, grid_y],
                "pixel_pos": [pixel_x, pixel_y],
                "uv_min": [u_min, v_min],
                "uv_max": [u_max, v_max],
                "uv_size": [u_max - u_min, v_max - v_min]
            }
            
            texture_index += 1
        
        # Save atlas image
        atlas.save(output_path)
        print(f"Atlas saved: {output_path}")
        
        # Save metadata
        if generate_metadata:
            metadata_path = output_path.with_suffix('.json')
            with open(metadata_path, 'w') as f:
                json.dump(texture_metadata, f, indent=2)
            print(f"Metadata saved: {metadata_path}")
            
            # Generate C++ header for easy integration
            self.generate_cpp_header(texture_metadata, output_path.with_suffix('.h'))
        
        return texture_metadata
    
    def generate_cpp_header(self, metadata, header_path):
        """Generate C++ header with texture atlas constants"""
        header_content = f"""#pragma once
// Auto-generated texture atlas header for Phyxel Engine
// Atlas size: {metadata['atlas_size']}x{metadata['atlas_size']}
// Texture count: {len(metadata['textures'])}

#include <unordered_map>
#include <string>

namespace Phyxel {{
namespace TextureAtlas {{

// Atlas constants
constexpr int ATLAS_SIZE = {metadata['atlas_size']};
constexpr int TEXTURE_SIZE = {metadata['texture_size']};
constexpr int PADDING = {metadata['padding']};
constexpr int TEXTURES_PER_ROW = {metadata['textures_per_row']};

// Texture UV coordinates
struct TextureUV {{
    float u_min, v_min, u_max, v_max;
}};

// Texture indices (for shader uniforms)
enum TextureIndex {{
"""
        
        # Add texture indices
        for i, (name, data) in enumerate(metadata['textures'].items()):
            enum_name = name.upper().replace('-', '_').replace(' ', '_')
            header_content += f"    TEX_{enum_name} = {data['index']},\n"
        
        header_content += f"""    TEXTURE_COUNT = {len(metadata['textures'])}
}};

// UV lookup table
const std::unordered_map<std::string, TextureUV> TEXTURE_UVS = {{
"""
        
        # Add UV coordinates
        for name, data in metadata['textures'].items():
            uv = data['uv_min'] + data['uv_max']
            header_content += f"""    {{"{name}", {{{uv[0]:.6f}f, {uv[1]:.6f}f, {uv[2]:.6f}f, {uv[3]:.6f}f}}}},\n"""
        
        header_content += """};

// Get texture index by name
inline int getTextureIndex(const std::string& name) {
    static const std::unordered_map<std::string, int> indices = {
"""
        
        for name, data in metadata['textures'].items():
            header_content += f"""        {{"{name}", {data['index']}}},\n"""
        
        header_content += """    };
    auto it = indices.find(name);
    return (it != indices.end()) ? it->second : -1;
}

}} // namespace TextureAtlas
}} // namespace Phyxel
"""
        
        with open(header_path, 'w') as f:
            f.write(header_content)
        print(f"C++ header saved: {header_path}")

def main():
    parser = argparse.ArgumentParser(description='Build texture atlas from 18x18 PNG files')
    parser.add_argument('input_dir', help='Directory containing PNG texture files')
    parser.add_argument('output', help='Output atlas PNG file path')
    parser.add_argument('--texture-size', type=int, default=18, 
                       help='Individual texture size (default: 18)')
    parser.add_argument('--padding', type=int, default=1,
                       help='Padding between textures (default: 1)')
    parser.add_argument('--no-metadata', action='store_true',
                       help='Skip generating metadata files')
    
    args = parser.parse_args()
    
    try:
        builder = TextureAtlasBuilder(args.texture_size, args.padding)
        textures = builder.load_textures(args.input_dir)
        
        if not textures:
            print("No PNG files found in input directory!")
            return 1
        
        output_path = Path(args.output)
        metadata = builder.build_atlas(textures, output_path, not args.no_metadata)
        
        print(f"\nAtlas build complete!")
        print(f"Textures processed: {len(textures)}")
        print(f"Atlas dimensions: {metadata['atlas_size']}x{metadata['atlas_size']}")
        
        return 0
        
    except Exception as e:
        print(f"Error: {e}")
        return 1

if __name__ == "__main__":
    exit(main())
