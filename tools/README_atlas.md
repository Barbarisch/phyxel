# Texture Atlas Builder

This tool combines multiple 18x18 PNG textures into a single atlas texture for efficient GPU rendering.

## Usage

### Basic Usage
```bash
python texture_atlas_builder.py textures/ cube_atlas.png
```

### Advanced Options
```bash
# Custom texture size and padding
python texture_atlas_builder.py textures/ atlas.png --texture-size 18 --padding 2

# Skip metadata generation
python texture_atlas_builder.py textures/ atlas.png --no-metadata
```

## Output Files

1. **Atlas PNG**: The combined texture atlas image
2. **Metadata JSON**: Texture positions and UV coordinates
3. **C++ Header**: Ready-to-use constants for your engine

## Example Directory Structure

```
textures/
├── stone_top.png      (18x18)
├── stone_side.png     (18x18)
├── wood_top.png       (18x18)
├── wood_side.png      (18x18)
├── grass_top.png      (18x18)
└── grass_side.png     (18x18)
```

## Generated Output

### Atlas Layout
- Automatically calculates optimal power-of-2 atlas size
- Adds configurable padding to prevent texture bleeding
- Preserves texture order for predictable indexing

### Metadata JSON Example
```json
{
  "atlas_size": 128,
  "texture_size": 18,
  "padding": 1,
  "textures": {
    "stone_top": {
      "index": 0,
      "grid_pos": [0, 0],
      "pixel_pos": [1, 1],
      "uv_min": [0.007812, 0.007812],
      "uv_max": [0.148438, 0.148438]
    }
  }
}
```

### C++ Integration
```cpp
#include "cube_atlas.h"

// Use texture index in shader
int stoneTopIndex = Phyxel::TextureAtlas::TEX_STONE_TOP;

// Get UV coordinates
auto uv = Phyxel::TextureAtlas::TEXTURE_UVS.at("stone_top");
```

## Shader Integration

### Vertex Shader
```glsl
// Pass texture index to fragment shader
layout(location = 3) in uint inTextureIndex;
layout(location = 0) out flat uint textureIndex;

void main() {
    textureIndex = inTextureIndex;
    // ... rest of vertex shader
}
```

### Fragment Shader
```glsl
layout(binding = 1) uniform sampler2D textureAtlas;
layout(location = 0) in flat uint textureIndex;

// Calculate UV from texture index
vec2 getAtlasUV(uint texIndex, vec2 localUV) {
    uint texPerRow = TEXTURES_PER_ROW;
    uint gridX = texIndex % texPerRow;
    uint gridY = texIndex / texPerRow;
    
    float texSize = float(TEXTURE_SIZE) / float(ATLAS_SIZE);
    float padding = float(PADDING) / float(ATLAS_SIZE);
    
    vec2 atlasPos = vec2(gridX, gridY) * (texSize + 2.0 * padding) + padding;
    return atlasPos + localUV * texSize;
}

void main() {
    vec2 atlasUV = getAtlasUV(textureIndex, gl_PointCoord);
    vec4 color = texture(textureAtlas, atlasUV);
    fragColor = color;
}
```

## Benefits

1. **Performance**: Single texture bind vs multiple switches
2. **Memory**: Optimal GPU memory usage
3. **Batching**: Render all cube types in one draw call
4. **Integration**: Auto-generated headers for easy use
5. **Flexibility**: Configurable sizes and padding
