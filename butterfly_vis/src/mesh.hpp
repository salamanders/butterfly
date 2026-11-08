#pragma once
#include <cstdint>

struct MeshVertex {
    float pos[3];   // Local X, Y, Z (wings lie in XZ plane at rest; +Z = head, -Z = tail)
    float uv[2];    // U in [0,1] across full wingspan (-X..+X -> 0..1 U), V in [0,1] tail-to-head
    float side;     // -1.0f for left wing, 0.0f for body, +1.0f for right wing
};

// 26-vertex, 20-triangle 4-wing articulated butterfly mesh:
// Splits each side at the natural V=0.50 forewing/hindwing cleft into independent
// Forewings (side = -1.0f / +1.0f, leading the stroke) and Hindwings (side = -2.0f / +2.0f,
// trailing behind in phase like silk cloth), each with inner and outer spanwise hinge segments.
inline constexpr MeshVertex kButterflyVerts[26] = {
    // --- Left Forewing (side = -1.0f, V = 0.49 .. 0.97, z = -0.02 .. +0.62) ---
    {.pos = {-0.76f,  0.000f,  0.62f}, .uv = {0.010f, 0.97f}, .side = -1.0f}, // 0: L forewing apex
    {.pos = {-0.78f,  0.000f, -0.02f}, .uv = {0.000f, 0.49f}, .side = -1.0f}, // 1: L forewing outer cleft
    {.pos = {-0.38f,  0.000f,  0.54f}, .uv = {0.260f, 0.91f}, .side = -1.0f}, // 2: L forewing mid leading
    {.pos = {-0.38f,  0.000f, -0.02f}, .uv = {0.260f, 0.49f}, .side = -1.0f}, // 3: L forewing mid cleft
    {.pos = {-0.025f, 0.000f,  0.46f}, .uv = {0.484f, 0.85f}, .side = -1.0f}, // 4: L forewing root front
    {.pos = {-0.025f, 0.000f, -0.02f}, .uv = {0.484f, 0.49f}, .side = -1.0f}, // 5: L forewing root cleft

    // --- Left Hindwing (side = -2.0f, V = 0.03 .. 0.50, z = -0.62 .. +0.02) ---
    {.pos = {-0.72f, -0.004f,  0.02f}, .uv = {0.040f, 0.50f}, .side = -2.0f}, // 6: L hindwing outer cleft
    {.pos = {-0.56f, -0.004f, -0.62f}, .uv = {0.140f, 0.03f}, .side = -2.0f}, // 7: L hindwing tail tip
    {.pos = {-0.38f, -0.004f,  0.02f}, .uv = {0.260f, 0.50f}, .side = -2.0f}, // 8: L hindwing mid cleft
    {.pos = {-0.38f, -0.004f, -0.62f}, .uv = {0.260f, 0.03f}, .side = -2.0f}, // 9: L hindwing mid tail
    {.pos = {-0.025f,-0.004f,  0.02f}, .uv = {0.484f, 0.50f}, .side = -2.0f}, // 10: L hindwing root cleft
    {.pos = {-0.025f,-0.004f, -0.54f}, .uv = {0.484f, 0.08f}, .side = -2.0f}, // 11: L hindwing root rear

    // --- Right Forewing (side = +1.0f, V = 0.49 .. 0.97, z = -0.02 .. +0.62) ---
    {.pos = { 0.025f, 0.000f,  0.46f}, .uv = {0.516f, 0.85f}, .side =  1.0f}, // 12: R forewing root front
    {.pos = { 0.025f, 0.000f, -0.02f}, .uv = {0.516f, 0.49f}, .side =  1.0f}, // 13: R forewing root cleft
    {.pos = { 0.38f,  0.000f,  0.54f}, .uv = {0.740f, 0.91f}, .side =  1.0f}, // 14: R forewing mid leading
    {.pos = { 0.38f,  0.000f, -0.02f}, .uv = {0.740f, 0.49f}, .side =  1.0f}, // 15: R forewing mid cleft
    {.pos = { 0.76f,  0.000f,  0.62f}, .uv = {0.990f, 0.97f}, .side =  1.0f}, // 16: R forewing apex
    {.pos = { 0.78f,  0.000f, -0.02f}, .uv = {1.000f, 0.49f}, .side =  1.0f}, // 17: R forewing outer cleft

    // --- Right Hindwing (side = +2.0f, V = 0.03 .. 0.50, z = -0.62 .. +0.02) ---
    {.pos = { 0.025f,-0.004f,  0.02f}, .uv = {0.516f, 0.50f}, .side =  2.0f}, // 18: R hindwing root cleft
    {.pos = { 0.025f,-0.004f, -0.54f}, .uv = {0.516f, 0.08f}, .side =  2.0f}, // 19: R hindwing root rear
    {.pos = { 0.38f, -0.004f,  0.02f}, .uv = {0.740f, 0.50f}, .side =  2.0f}, // 20: R hindwing mid cleft
    {.pos = { 0.38f, -0.004f, -0.62f}, .uv = {0.740f, 0.03f}, .side =  2.0f}, // 21: R hindwing mid tail
    {.pos = { 0.72f, -0.004f,  0.02f}, .uv = {0.960f, 0.50f}, .side =  2.0f}, // 22: R hindwing outer cleft
    {.pos = { 0.56f, -0.004f, -0.62f}, .uv = {0.860f, 0.03f}, .side =  2.0f}, // 23: R hindwing tail tip

    // --- Center Body Strip (side = 0.0f so thorax/abdomen never folds) ---
    {.pos = {-0.028f, 0.020f,  0.46f}, .uv = {0.484f, 0.85f}, .side =  0.0f}, // 24: Body front-L (dorsal ridge)
    {.pos = { 0.028f, 0.020f, -0.52f}, .uv = {0.516f, 0.10f}, .side =  0.0f}, // 25: Body rear-R
};

// Counter-clockwise (CCW) winding when viewed from +Y (dorsal top):
inline constexpr uint16_t kButterflyIndices[60] = {
    // Left Forewing (4 tris: outer 2 + inner 2)
    0, 2, 1,   1, 2, 3,   2, 4, 3,   3, 4, 5,
    // Left Hindwing (4 tris: outer 2 + inner 2)
    6, 8, 7,   7, 8, 9,   8, 10, 9,  9, 10, 11,
    // Right Forewing (4 tris: inner 2 + outer 2)
    12, 14, 13,  13, 14, 15,  14, 16, 15,  15, 16, 17,
    // Right Hindwing (4 tris: inner 2 + outer 2)
    18, 20, 19,  19, 20, 21,  20, 22, 21,  21, 22, 23,
    // Thorax / Abdomen bridge + dorsal ridge (4 tris)
    4, 12, 11,   11, 12, 19,  24, 12, 11,  12, 25, 11
};
