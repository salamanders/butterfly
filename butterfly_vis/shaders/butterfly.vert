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
    // Orthonormal basis from instance forward (+Z) and banked up (+Y)
    vec3 fwd = normalize(i_forward);
    vec3 right = normalize(cross(fwd, i_up));
    vec3 up = cross(right, fwd);

    vec3 local = a_pos;
    vec3 localNormal = vec3(0.0, 1.0, 0.0);

    if (abs(a_side) > 0.5) {
        float wingSign   = sign(a_side);                 // -1.0 for left wings, +1.0 for right wings
        float span       = clamp(abs(local.x) / 0.78, 0.0, 1.0);
        float outerTip   = clamp((span - 0.45) / 0.55, 0.0, 1.0); // Outer wingtip beyond |x|=0.38 hinge
        float trail      = clamp((0.58 - local.z) / 1.20, 0.0, 1.0);
        // Distance behind the forewing/hindwing cleft (0.0 at cleft z=0.0, 1.0 at hindwing tail z=-0.62)
        // Guarantees C0 continuity across the z=0 seam so forewing and hindwing never tear apart!
        float hindTail   = clamp((0.02 - local.z) / 0.64, 0.0, 1.0);

        // 1. Clap-and-Peel dorsal cupping: at the top of the upstroke, leading edges peel apart first
        // while hindwings and trailing membranes stay momentarily cupped near the dorsal peak
        float dorsalClap = clamp((i_wing_angle - 0.80) / 0.60, 0.0, 1.0);
        float peelCup    = dorsalClap * max(i_wing_flex, 0.0) * (0.20 + 0.80 * trail) * 0.32;

        // 2. Continuous C-Curve inner-to-outer hinge whip, forewing-to-hindwing wave & bank steering asymmetry
        float hindWave   = hindTail * (-0.04 + i_wing_flex * 0.55);
        float cCurveWhip = i_wing_flex * (span * 0.20 + outerTip * outerTip * 0.34);
        float chordLag   = trail * trail * (0.30 + 0.70 * span) * 0.48;
        // Differential wing amplitude when banking: the upper/outer wing sweeps ~8 deg wider than the inner wing
        float bankAsym   = -wingSign * right.y * (0.10 + 0.06 * span);
        float angle      = clamp(i_wing_angle + bankAsym + hindWave + peelCup + cCurveWhip + i_wing_flex * chordLag, 0.14, 1.48);

        // 3. Continuous 3D Pronation/Supination twist around the z=0.02 cleft axis & Figure-8 sweep
        float cleftDist  = local.z - 0.02;
        float feather    = i_wing_flex * (0.22 + 0.26 * span);
        float fc         = cos(feather);
        float fs         = sin(feather);

        float billow     = i_wing_flex * trail * span * 0.09;
        float sweepZ     = span * (i_wing_flex * 0.11 + sin(i_wing_angle * 2.0) * 0.035);
        float wy         = local.y + billow - cleftDist * fs;
        float wz         = 0.02 + cleftDist * fc + sweepZ;

        // Rotate left (-wingSign) and right (+wingSign) wings symmetrically around Z axis
        float rot = wingSign * angle;
        float c = cos(rot);
        float s = sin(rot);

        local = vec3(local.x * c - wy * s,
                     local.x * s + wy * c,
                     wz);

        // Normal tilts both spanwise (-s, c) and chordwise (3D pronation/supination twist)
        float chordTilt = i_wing_flex * (0.30 + 0.45 * trail) * (0.35 + 0.65 * span);
        localNormal = normalize(vec3(-s, c, chordTilt));
    }

    // Abdomen counter-swing damps thorax pitching moments (inertial counterweight)
    float abdomenCounter = (abs(a_side) < 0.5 && local.z < 0.0) ? (-i_wing_flex * local.z * 0.24) : 0.0;

    // Breaststroke thorax pitch: nose pitches forward/down on power snap (i_wing_flex > 0) and rises on recovery upstroke
    float strokePitch = i_wing_flex * 0.44;
    float pc = cos(strokePitch);
    float ps = sin(strokePitch);
    local = vec3(local.x,
                 local.y * pc + local.z * ps + abdomenCounter,
                 -local.y * ps + local.z * pc);
    localNormal = normalize(vec3(localNormal.x,
                                 localNormal.y * pc + localNormal.z * ps,
                                 -localNormal.y * ps + localNormal.z * pc));

    // Phase-delayed vertical body counter-bounce coupled to wing snap & follow-through
    local.y += (0.58 - i_wing_angle) * 0.19 + i_wing_flex * 0.08;

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
