#pragma once
#include <raylib.h>
#include <GLES3/gl3.h>
#include <cstdint>
#include <cstring>
#include <span>
#include "cpp26.hpp"

namespace bf {

// Embed the 8 museum specimen PNGs directly into the executable binary (.rodata)
BF_EMBED_BINARY(species_0_png, "assets/species_0.png")
BF_EMBED_BINARY(species_1_png, "assets/species_1.png")
BF_EMBED_BINARY(species_2_png, "assets/species_2.png")
BF_EMBED_BINARY(species_3_png, "assets/species_3.png")
BF_EMBED_BINARY(species_4_png, "assets/species_4.png")
BF_EMBED_BINARY(species_5_png, "assets/species_5.png")
BF_EMBED_BINARY(species_6_png, "assets/species_6.png")
BF_EMBED_BINARY(species_7_png, "assets/species_7.png")

inline std::span<const uint8_t> embeddedSpeciesPng(int layer) noexcept {
    switch (layer) {
        case 0: return embedded_species_0_png();
        case 1: return embedded_species_1_png();
        case 2: return embedded_species_2_png();
        case 3: return embedded_species_3_png();
        case 4: return embedded_species_4_png();
        case 5: return embedded_species_5_png();
        case 6: return embedded_species_6_png();
        case 7: return embedded_species_7_png();
        default: return {};
    }
}

inline GLuint createSpeciesTextureArray() {
    constexpr int W = 512;
    constexpr int H = 512;
    constexpr int LAYERS = 8;
    constexpr int MIP_LEVELS = 9;

    GLuint texArray = 0;
    glGenTextures(1, &texArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, MIP_LEVELS, GL_RGBA8, W, H, LAYERS);

    for (int layer = 0; layer < LAYERS; ++layer) {
        auto emb = embeddedSpeciesPng(layer);
        Image img = LoadImageFromMemory(".png", emb.data(), static_cast<int>(emb.size()));
        if (img.data != nullptr) {
            if (img.width != W || img.height != H) {
                ImageResize(&img, W, H);
            }
            // Flip vertically so PNG top (head/forewing apex) maps to OpenGL V=1.0 and PNG bottom (tail) maps to V=0.0
            ImageFlipVertical(&img);
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            glTexSubImage3D(
                GL_TEXTURE_2D_ARRAY, 0,
                0, 0, layer,
                W, H, 1,
                GL_RGBA, GL_UNSIGNED_BYTE,
                img.data
            );
            UnloadImage(img);
            std::println("[Textures] Loaded embedded museum specimen for species_{}", layer);
        }
    }

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return texArray;
}

} // namespace bf
