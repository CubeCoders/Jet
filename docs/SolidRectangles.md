# Solid-Color Rectangle Extension for 2D Sprite System

## Overview

Extension to the Jet engine's 2D sprite system that provides convenient helpers for drawing arbitrarily-sized solid-color rectangles with alpha blending. Optimized for common use cases like full-screen fades, letterbox bars, and visual effect overlays.

## Key Features

1. **Arbitrary Sizing**: Draw rectangles of any size at any position
2. **Alpha Blending**: Full 0-255 alpha support with per-pixel blending
3. **Automatic Optimizations**:
   - 0% alpha (0): sprite.enabled = false → rendering skipped entirely
   - 100% alpha (255): Fast opaque blit → ~3x faster than blended rendering
   - 1-254 alpha: Per-pixel alpha blend

## API Reference

### Core Functions

#### `makeSolidRect(x, y, width, height, material)`
Creates a Sprite2D configured as a solid-color rectangle.

**Parameters:**
- `x`, `y`: Position in screen pixels (0,0 = top-left)
- `width`, `height`: Dimensions in pixels
- `material`: Pointer to Material with RGB565 color and alpha (caller owns)

**Returns:** Configured Sprite2D (caller owns)

#### `setSolidRectAlpha(sprite, alpha)`
Updates a sprite's alpha with automatic enable/disable optimization.

**Parameters:**
- `sprite`: Reference to Sprite2D to update
- `alpha`: New alpha value (0=transparent, 255=opaque)

### Convenience Functions

#### `makeFullScreenFade(screenWidth, screenHeight, color, material)`
Creates a full-screen overlay (typically for fade-to-black/white effects).

**Common colors:**
- `0x0000`: Black
- `0xFFFF`: White

#### `makeLetterboxBar(screenWidth, barHeight, color, topBar, material)`
Creates a letterbox bar for cinematic mode.

**Parameters:**
- `topBar`: true for top bar, false for bottom bar

## Usage Examples

### Full-Screen Fade

```cpp
// Setup (once)
static Renderer::Material fadeMat(0x0000, 0);  // Black, transparent
static Renderer::Sprite2D fadeSprite = 
    Renderer::makeFullScreenFade(480, 320, 0x0000, &fadeMat);
scene->addSprite(&fadeSprite);

// During gameplay - fade in over time
float fadeProgress = calculateFadeProgress();  // 0.0-1.0
fadeMat.alpha = (uint8_t)(fadeProgress * 255);
```

### Letterbox Bars

```cpp
// Setup
static Renderer::Material topMat(0x0000, 255);  // Black, opaque
static Renderer::Material botMat(0x0000, 255);
static Renderer::Sprite2D topBar = 
    Renderer::makeLetterboxBar(480, 40, 0x0000, true, &topMat);
static Renderer::Sprite2D botBar = 
    Renderer::makeSolidRect(0, 280, 480, 40, &botMat);

scene->addSprite(&topBar);
scene->addSprite(&botBar);

// Toggle visibility
topBar.enabled = wantCinematicMode;
botBar.enabled = wantCinematicMode;
```

### Damage Flash Overlay

```cpp
// Setup
static Renderer::Material flashMat(0xF800, 0);  // Red, transparent
static Renderer::Sprite2D flashSprite = 
    Renderer::makeSolidRect(0, 0, 480, 320, &flashMat);
scene->addSprite(&flashSprite);

// When taking damage
void onDamage() {
    flashMat.alpha = 128;  // 50% red overlay
}

// Each frame - fade the flash
void update(float dt) {
    if (flashMat.alpha > 0) {
        flashMat.alpha = max(0, flashMat.alpha - (int)(255 * dt * 2.0));
    }
}
```

## Performance Characteristics

| Alpha Value | Behavior | Performance |
|-------------|----------|-------------|
| 0 | Rendering skipped (enabled=false) | Zero cost |
| 255 | Fast opaque blit (no blending) | ~3x faster |
| 1-254 | Per-pixel alpha blend | Moderate cost |

## RGB565 Color Reference

Common colors in RGB565 format:
- Black: `0x0000`
- White: `0xFFFF`
- Red: `0xF800`
- Green: `0x07E0`
- Blue: `0x001F`
- Yellow: `0xFFE0`
- Magenta: `0xF81F`
- Cyan: `0x07FF`

## Implementation Details

### Files Modified
- `components/Jet/src/Sprite2D.hpp`: Added helper functions and documentation

### Files Created
- `components/Jet/examples/SolidRectExample.cpp`: Comprehensive usage examples

### Rendering Pipeline
The solid-color rectangle rendering uses the existing Sprite2D infrastructure:
1. Scene::drawSprites() is called at the end of each render pass
2. Sprites are sorted by zOrder (lower = drawn first)
3. For solid rectangles (material->diffuseMap == nullptr):
   - Fully transparent (alpha=0): Skip entirely via enabled=false
   - Fully opaque (alpha=255): Fast span fill with material->color
   - Partial alpha (1-254): Per-pixel RGB565 lerp blend

### Memory Management
- Caller owns both the Sprite2D and Material objects
- Both must remain valid while registered with Scene
- Use static or member variables, not stack temporaries

## Common Use Cases

1. **Scene Transitions**: Fade to black between levels
2. **Cinematic Mode**: Letterbox bars for cutscenes
3. **Visual Feedback**: Damage/healing flash overlays
4. **UI Overlays**: Semi-transparent colored panels
5. **Debug Visualization**: Quick colored rectangles for testing

## Design Decisions

### Why not use BLEND_ADD mode by default?
BLEND_REPLACE (default) provides proper alpha blending where the overlay obscures what's behind it. BLEND_ADD is available via `sprite.blendMode = BlendMode::BLEND_ADD` for special effects like glows.

### Why return Sprite2D by value?
Sprites are small structs (32 bytes) and returning by value ensures clear ownership semantics. Caller can store as stack, static, or heap variables.

### Why require caller to manage Material lifetime?
Materials are often shared between multiple sprites or modified dynamically (alpha changes for fades). Caller ownership provides maximum flexibility.

## Future Enhancements

Possible additions (not currently implemented):
- Animated fades with easing functions
- Pattern fills (diagonal stripes, checkerboard)
- Border/outline rendering
- Rounded corners for rectangles
- Gradient fills (vertical/horizontal)
