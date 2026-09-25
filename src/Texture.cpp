#include "Texture.hpp"

namespace Renderer
{

  Texture::Texture(int w, int h, uint16_t *data, bool hasAlpha, uint16_t alphaColor, bool screenSpace, TextureAddressMode addressMode, uint16_t *palette)
      : width(w), height(h), data(data),
        hasAlpha(hasAlpha), alphaColor(alphaColor), screenSpace(screenSpace),
        addressMode(addressMode), palette(palette)
  {
    // Texture data is initialized with default values
  }

uint16_t Texture::getPixel(int u, int v)
{
    if(tiled){
#if BILINEAR_FILTER
        if(getTileFilter()==TileFilter::CachedBilinear)return tiled->sampleHot(u,v);
        if(getTileFilter()==TileFilter::ThreePoint)return tiled->sampleThreePoint(u,v);
        if(getTileFilter()==TileFilter::Bilinear)return tiled->sampleBilinear(u,v);
#endif
        return tiled->sample(u,v);
    }
    switch (addressMode)
    {
    case WRAP:
        // Wrap texture coordinates
        u = ((u % FIXED_POINT_SCALE) + FIXED_POINT_SCALE) % FIXED_POINT_SCALE;
        v = ((v % FIXED_POINT_SCALE) + FIXED_POINT_SCALE) % FIXED_POINT_SCALE;
        break;
    case CLAMP:
        // Clamp texture coordinates
        u = std::min(std::max(u, 0), FIXED_POINT_SCALE - 1);
        v = std::min(std::max(v, 0), FIXED_POINT_SCALE - 1);
        break;
    case ZERO:
        // Return a default color if out of bounds
        if (u < 0 || u >= FIXED_POINT_SCALE || v < 0 || v >= FIXED_POINT_SCALE)
        {
            return 0; // Or use a predefined default color
        }
        break;
    }

    #if BILINEAR_FILTER
    if (bilinear && !palette) {
    // Compute scaled texture coordinates
    uint32_t scaledU = u * (width - 1);
    uint32_t scaledV = v * (height - 1);

    int x0 = scaledU / FIXED_POINT_SCALE;
    int y0 = scaledV / FIXED_POINT_SCALE;

    int x1 = x0 + 1;
    int y1 = y0 + 1;

    // Clamp coordinates to texture dimensions
    if (x1 >= width) x1 = width - 1;
    if (y1 >= height) y1 = height - 1;

    uint32_t u_ratio = scaledU % FIXED_POINT_SCALE;
    uint32_t v_ratio = scaledV % FIXED_POINT_SCALE;
    uint32_t u_opposite = FIXED_POINT_SCALE - u_ratio;
    uint32_t v_opposite = FIXED_POINT_SCALE - v_ratio;

    // Retrieve colors at the four surrounding pixels
    uint16_t c00 = data[y0 * width + x0];
    uint16_t c10 = data[y0 * width + x1];
    uint16_t c01 = data[y1 * width + x0];
    uint16_t c11 = data[y1 * width + x1];

    // Decompose colors into RGB components (5 bits Red, 6 bits Green, 5 bits Blue)
    uint8_t r00 = (c00 >> 11) & 0x1F;
    uint8_t g00 = (c00 >> 5) & 0x3F;
    uint8_t b00 = c00 & 0x1F;

    uint8_t r10 = (c10 >> 11) & 0x1F;
    uint8_t g10 = (c10 >> 5) & 0x3F;
    uint8_t b10 = c10 & 0x1F;

    uint8_t r01 = (c01 >> 11) & 0x1F;
    uint8_t g01 = (c01 >> 5) & 0x3F;
    uint8_t b01 = c01 & 0x1F;

    uint8_t r11 = (c11 >> 11) & 0x1F;
    uint8_t g11 = (c11 >> 5) & 0x3F;
    uint8_t b11 = c11 & 0x1F;

    // Perform bilinear interpolation
    uint32_t r = (r00 * u_opposite * v_opposite +
            r10 * u_ratio * v_opposite +
            r01 * u_opposite * v_ratio +
            r11 * u_ratio * v_ratio) / (FIXED_POINT_SCALE * FIXED_POINT_SCALE);
    uint32_t g = (g00 * u_opposite * v_opposite +
            g10 * u_ratio * v_opposite +
            g01 * u_opposite * v_ratio +
            g11 * u_ratio * v_ratio) / (FIXED_POINT_SCALE * FIXED_POINT_SCALE);
    uint32_t b = (b00 * u_opposite * v_opposite +
            b10 * u_ratio * v_opposite +
            b01 * u_opposite * v_ratio +
            b11 * u_ratio * v_ratio) / (FIXED_POINT_SCALE * FIXED_POINT_SCALE);

    // Recombine RGB components into a 16-bit color
    uint16_t color = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
    return color;
    }
    #endif
    // Scale down the fixed-point UV coordinates to the texture dimensions
    u = (u * width) / FIXED_POINT_SCALE;
    v = (v * height) / FIXED_POINT_SCALE;

    // Retrieve the color from the texture data
    uint16_t color = 0;
    if (palette) {
        // If a palette is provided, use it to get the color.
        // paletteSize>0 means animated: offset the index so the palette
        // appears to scroll without touching the texture data (Sonic trick).
        uint8_t colorIndex = ((uint8_t*)data)[v * width + u];
        if (paletteSize > 0) {
            colorIndex = (colorIndex + paletteOffset) % paletteSize;
        }
        color = palette[colorIndex];
    }
    else {
        color = data[v * width + u];
    }

    return color;
}

} // namespace Renderer
