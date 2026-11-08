#version 300 es
precision highp float;

// Per-vertex (mesh)
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in float a_side;

// Per-instance
layout(location = 3) in vec3 i_pos;
layout(location = 4) in vec3 i_forward;
layout(location = 5) in vec3 i_up;
layout(location = 6) in float i_wing_angle;
layout(location = 7) in float i_wing_flex;
layout(location = 8) in float i_scale;
layout(location = 9) in uint i_species;

uniform mat4 u_viewProj;

out vec2 v_uv;
out vec3 v_normal;
flat out uint v_species;

void main() {
    vec3 local = a_pos;
    vec3 localNormal = vec3(0.0, 1.0, 0.0);

    if (abs(a_side) > 0.5) {
        float wingSign   = sign(a_side);                 // -1.0 for left wings, +1.0 for right wings
        bool  isHindwing = (abs(a_side) > 1.5);          // false = forewing (leads), true = hindwing (trails)
        float span  = clamp(abs(local.x) / 0.78, 0.0, 1.0);
        float trail = clamp((0.58 - local.z) / 1.20, 0.0, 1.0);

        // Forewing (+Z) leads the stroke; Hindwing (-Z) lags behind in phase like trailing silk cloth
        float hindOffset = isHindwing ? (-0.06 + i_wing_flex * 0.65) : 0.0;
        float spanLag    = span * span * (isHindwing ? 0.52 : 0.36);
        float chordLag   = trail * trail * (0.35 + 0.65 * span) * (isHindwing ? 0.95 : 0.32);
        float angle      = clamp(i_wing_angle + hindOffset + i_wing_flex * (spanLag + chordLag), 0.16, 1.54);

        // Rotate left (-wingSign) and right (+wingSign) wings symmetrically upward (+Y) into a V around Z axis
        float rot = wingSign * angle;
        float c = cos(rot);
        float s = sin(rot);

        // Subtle trailing-cloth billow along the hindwing membrane during strokes
        float billow = i_wing_flex * trail * span * (isHindwing ? 0.14 : 0.05);
        float wy = local.y + billow;

        local = vec3(local.x * c - wy * s,
                     local.x * s + wy * c,
                     local.z);

        // Normal tilts both spanwise (-s, c) and chordwise (trailing wave pitch) so iridescence rolls head-to-tail
        float chordTilt = i_wing_flex * (isHindwing ? 0.65 : 0.28) * (0.35 + 0.65 * span);
        localNormal = normalize(vec3(-s, c, chordTilt));
    }

    // Breaststroke pitch & heave: nose pitches up on power downstroke (i_wing_flex > 0) and dips on recovery upstroke
    float strokePitch = i_wing_flex * 0.42;
    float pc = cos(strokePitch);
    float ps = sin(strokePitch);
    local = vec3(local.x,
                 local.y * pc + local.z * ps,
                 -local.y * ps + local.z * pc);
    localNormal = normalize(vec3(localNormal.x,
                                 localNormal.y * pc + localNormal.z * ps,
                                 -localNormal.y * ps + localNormal.z * pc));

    // Vertical breaststroke body bob coupled to wing stroke
    local.y += (0.78 - i_wing_angle) * 0.14;

    // Orthonormal basis from instance forward (+Z) and banked up (+Y)
    vec3 fwd = normalize(i_forward);
    vec3 right = normalize(cross(fwd, i_up));
    vec3 up = cross(right, fwd);

    // Subtle Z-depth scale in orthographic projection so far butterflies look smaller with zero lens distortion
    float depthScale = clamp(1.0 + (i_pos.z + 1.8) * 0.14, 0.58, 1.38);
    vec3 worldOffset = (right * local.x + up * local.y + fwd * local.z) * (i_scale * depthScale);
    vec3 worldPos = i_pos + worldOffset;
    vec3 worldNormal = normalize(right * localNormal.x + up * localNormal.y + fwd * localNormal.z);

    v_uv = a_uv;
    v_normal = worldNormal;
    v_species = i_species;

    gl_Position = u_viewProj * vec4(worldPos, 1.0);
}
