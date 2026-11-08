"use strict";

// ============================================================================
// 1. Passive Screensaver Dismissal (Any Mouse / Touch / Key input closes window)
// ============================================================================
const startTimestamp = performance.now();
let initialMousePos = null;
let dismissing = false;

function dismissScreensaver() {
  if (dismissing) return;
  dismissing = true;
  if (typeof chrome !== "undefined" && chrome.runtime && chrome.runtime.sendMessage) {
    chrome.runtime.sendMessage({ type: "DISMISS_SCREENSAVER" }, () => {
      window.close();
    });
  } else {
    window.close();
  }
}

window.addEventListener("keydown", () => {
  if (performance.now() - startTimestamp > 350) dismissScreensaver();
});
window.addEventListener("mousedown", () => {
  if (performance.now() - startTimestamp > 350) dismissScreensaver();
});
window.addEventListener("touchstart", () => {
  if (performance.now() - startTimestamp > 350) dismissScreensaver();
}, { passive: true });
window.addEventListener("wheel", () => {
  if (performance.now() - startTimestamp > 350) dismissScreensaver();
}, { passive: true });
window.addEventListener("mousemove", (e) => {
  if (performance.now() - startTimestamp < 600) return;
  if (!initialMousePos) {
    initialMousePos = { x: e.clientX, y: e.clientY };
    return;
  }
  const dx = e.clientX - initialMousePos.x;
  const dy = e.clientY - initialMousePos.y;
  if (dx * dx + dy * dy > 64) {
    dismissScreensaver();
  }
}, { passive: true });

// ============================================================================
// 2. Exact GLSL ES 3.00 Shaders (Butterfly Instanced 3D + Fairy Dust Motes)
// ============================================================================
const BUTTERFLY_VS_SRC = `#version 300 es
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
`;

const BUTTERFLY_FS_SRC = `#version 300 es
precision highp float;
precision highp sampler2DArray;

in vec2 v_uv;
in vec3 v_normal;
flat in uint v_species;

uniform sampler2DArray u_texArray;
uniform vec3 u_lightDir;

out vec4 fragColor;

void main() {
    vec4 tex = texture(u_texArray, vec3(v_uv, float(v_species)));
    if (tex.a < 0.08) discard;

    vec3 dorsalN = normalize(v_normal);
    const vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 L = normalize(-u_lightDir);

    // Smooth dorsal/ventral blend across grazing angles so curved wings never snap at gl_FrontFacing
    float facing = dot(dorsalN, V);
    float dorsalFactor = smoothstep(-0.18, 0.18, facing);
    vec3 N = normalize(dorsalN * (facing >= 0.0 ? 1.0 : -1.0));

    // Ventral (underside) is warmer, slightly softer, and more matte than the dorsal topside
    // so snapping the wings open creates a vivid "Morpho jewel flash" on every downstroke hold
    float texLuma = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
    vec3 ventralAlbedo = mix(vec3(texLuma) * vec3(0.84, 0.78, 0.70), tex.rgb * 0.76, 0.60);
    vec3 baseAlbedo = mix(ventralAlbedo, tex.rgb, dorsalFactor);

    // Per-species bioluminescent accent & iridescent shimmer palette
    vec3 bioTint  = vec3(1.00, 0.50, 0.12); // 0: Monarch (solar gold-amber)
    vec3 iridHueA = vec3(1.00, 0.72, 0.20);
    vec3 iridHueB = vec3(0.75, 0.22, 0.95);
    float iridStrength = 0.34;

    if (v_species == 1u) {        // 1: Blue Morpho (celestial azure -> violet)
        bioTint  = vec3(0.10, 0.65, 1.00);
        iridHueA = vec3(0.08, 0.72, 1.00);
        iridHueB = vec3(0.58, 0.18, 1.00);
        iridStrength = 0.72;
    } else if (v_species == 2u) { // 2: Tiger Swallowtail (sun-gold -> turquoise)
        bioTint  = vec3(1.00, 0.80, 0.18);
        iridHueA = vec3(1.00, 0.88, 0.30);
        iridHueB = vec3(0.12, 0.88, 0.98);
        iridStrength = 0.38;
    } else if (v_species == 3u) { // 3: Question Mark (enchanted amber-violet)
        bioTint  = vec3(0.78, 0.34, 0.96);
        iridHueA = vec3(1.00, 0.58, 0.22);
        iridHueB = vec3(0.72, 0.24, 1.00);
        iridStrength = 0.52;
    } else if (v_species == 4u) { // 4: Cabbage White (moonlit cyan-opal -> rose pearl)
        bioTint  = vec3(0.55, 0.92, 1.00);
        iridHueA = vec3(0.48, 0.95, 1.00);
        iridHueB = vec3(0.96, 0.62, 1.00);
        iridStrength = 0.46;
    } else if (v_species == 5u) { // 5: Red Admiral (ruby-magenta -> royal violet)
        bioTint  = vec3(0.88, 0.18, 0.68);
        iridHueA = vec3(1.00, 0.28, 0.55);
        iridHueB = vec3(0.52, 0.20, 1.00);
        iridStrength = 0.56;
    } else if (v_species == 6u) { // 6: Pipevine Swallowtail (emerald-teal -> sapphire)
        bioTint  = vec3(0.06, 0.88, 0.82);
        iridHueA = vec3(0.08, 0.95, 0.78);
        iridHueB = vec3(0.28, 0.35, 1.00);
        iridStrength = 0.64;
    } else if (v_species == 7u) { // 7: Buckeye (amethyst-violet -> warm gold)
        bioTint  = vec3(0.72, 0.28, 0.96);
        iridHueA = vec3(0.82, 0.36, 1.00);
        iridHueB = vec3(1.00, 0.76, 0.24);
        iridStrength = 0.54;
    }

    float luma = dot(baseAlbedo, vec3(0.299, 0.587, 0.114));
    float darkPigment = 1.0 - smoothstep(0.05, 0.42, luma);
    float wingSpan = clamp(abs(v_uv.x - 0.5) * 2.2, 0.0, 1.0);
    baseAlbedo = mix(baseAlbedo, baseAlbedo * 0.55 + bioTint * (0.28 + 0.18 * wingSpan), darkPigment * 0.68);

    // Velvety solid floor so dark wing veins and body remain solid against #000000
    vec3 velvetBlack = mix(vec3(0.09, 0.09, 0.11), bioTint * 0.22, 0.35);
    baseAlbedo = max(baseAlbedo, velvetBlack);

    // Boost jewel-tone vibrance on dorsal topside
    float newLuma = dot(baseAlbedo, vec3(0.299, 0.587, 0.114));
    baseAlbedo = clamp(mix(vec3(newLuma), baseAlbedo, mix(1.12, 1.42, dorsalFactor)), 0.0, 1.0);

    // 1. Stained-glass two-sided lighting
    float ndotl = max(dot(N, L), 0.0) + 0.58 * max(dot(-N, L), 0.0);
    float hemi  = 0.56 + 0.16 * dorsalN.y;
    vec3 diffuse = baseAlbedo * (hemi + 0.78 * ndotl);

    // 2. Bioluminescent Subsurface Scattering
    float backAlign = clamp(dot(V, -L), 0.0, 1.0);
    float wrapBack  = clamp(dot(-N, L), 0.0, 1.0);
    float sssFactor = pow(backAlign, 2.0) * 0.55 + wrapBack * 0.45 + 0.22;
    float membrane  = smoothstep(0.06, 0.55, newLuma);
    vec3 sss = mix(baseAlbedo, bioTint, 0.35) * sssFactor * (0.28 + 0.65 * membrane);

    // 3. All-Species Structural Iridescence + Wingtip Rim Luminescence
    float dorsalShimmer = mix(0.24, 1.00, dorsalFactor);
    float ndotv = clamp(abs(dot(N, V)), 0.0, 1.0);
    float fresnel = pow(1.0 - ndotv, 1.75);
    float edgeGlow = smoothstep(0.28, 0.88, wingSpan);
    vec3 iridTint = mix(iridHueA, iridHueB, clamp(fresnel * 0.75 + edgeGlow * 0.35, 0.0, 1.0));
    vec3 iridescence = iridTint * (iridStrength * dorsalShimmer) * (0.45 + 0.55 * ndotl) * (0.28 + 0.72 * max(fresnel, edgeGlow * 0.65));

    // 4. Microscopic Wing-Scale Shimmer
    vec2 scaleGrid = v_uv * 115.0 + dorsalN.xy * 9.5;
    float sparkleWave = sin(scaleGrid.x) * sin(scaleGrid.y) * cos((scaleGrid.x - scaleGrid.y) * 0.7);
    float sparkle = pow(clamp(sparkleWave, 0.0, 1.0), 5.0) * (0.35 + 0.65 * edgeGlow);
    vec3 scaleShimmer = mix(iridHueA, vec3(1.0, 0.97, 0.88), 0.55) * sparkle * (0.58 * dorsalShimmer);

    // 5. Subtle specular highlight on dark chitin body and wing ribs
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 16.0) * 0.25;

    vec3 color = diffuse + sss + iridescence + scaleShimmer + vec3(spec);
    color = color / (vec3(1.0) + color * 0.16);

    fragColor = vec4(color, 1.0);
}
`;

const MOTE_VS_SRC = `#version 300 es
precision highp float;
layout(location = 0) in vec2 a_posPx;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_color;
uniform vec2 u_resolution;
out vec2 v_uv;
out vec3 v_color;
void main() {
    vec2 ndc = (a_posPx / u_resolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
`;

const MOTE_FS_SRC = `#version 300 es
precision highp float;
in vec2 v_uv;
in vec3 v_color;
out vec4 fragColor;
void main() {
    float r2 = dot(v_uv, v_uv);
    if (r2 > 1.0) discard;
    float core = exp(-r2 * 4.5);
    float rays = exp(-abs(v_uv.x) * 7.5) * exp(-abs(v_uv.y) * 1.8)
               + exp(-abs(v_uv.y) * 7.5) * exp(-abs(v_uv.x) * 1.8);
    float falloff = clamp((core + rays * 0.38) * (1.0 - r2), 0.0, 1.0);
    if (falloff < 0.02) discard;
    vec3 col = mix(v_color * falloff, vec3(1.0, 0.98, 0.92), core * core * 0.65);
    fragColor = vec4(col, 1.0);
}
`;

// ============================================================================
// 3. 26-Vertex, 20-Triangle 4-Wing Articulated Butterfly Mesh (from mesh.hpp)
// ============================================================================
// Interleaved [posX, posY, posZ, uvU, uvV, side] (6 floats = 24 bytes per vertex)
const BUTTERFLY_VERTS = new Float32Array([
  // Left Forewing (side = -1.0)
  -0.76,  0.000,  0.62,  0.010, 0.97, -1.0,
  -0.78,  0.000, -0.02,  0.000, 0.49, -1.0,
  -0.38,  0.000,  0.54,  0.260, 0.91, -1.0,
  -0.38,  0.000, -0.02,  0.260, 0.49, -1.0,
  -0.025, 0.000,  0.46,  0.484, 0.85, -1.0,
  -0.025, 0.000, -0.02,  0.484, 0.49, -1.0,

  // Left Hindwing (side = -2.0)
  -0.72, -0.004,  0.02,  0.040, 0.50, -2.0,
  -0.56, -0.004, -0.62,  0.140, 0.03, -2.0,
  -0.38, -0.004,  0.02,  0.260, 0.50, -2.0,
  -0.38, -0.004, -0.62,  0.260, 0.03, -2.0,
  -0.025,-0.004,  0.02,  0.484, 0.50, -2.0,
  -0.025,-0.004, -0.54,  0.484, 0.08, -2.0,

  // Right Forewing (side = +1.0)
   0.025, 0.000,  0.46,  0.516, 0.85,  1.0,
   0.025, 0.000, -0.02,  0.516, 0.49,  1.0,
   0.38,  0.000,  0.54,  0.740, 0.91,  1.0,
   0.38,  0.000, -0.02,  0.740, 0.49,  1.0,
   0.76,  0.000,  0.62,  0.990, 0.97,  1.0,
   0.78,  0.000, -0.02,  1.000, 0.49,  1.0,

  // Right Hindwing (side = +2.0)
   0.025,-0.004,  0.02,  0.516, 0.50,  2.0,
   0.025,-0.004, -0.54,  0.516, 0.08,  2.0,
   0.38, -0.004,  0.02,  0.740, 0.50,  2.0,
   0.38, -0.004, -0.62,  0.740, 0.03,  2.0,
   0.72, -0.004,  0.02,  0.960, 0.50,  2.0,
   0.56, -0.004, -0.62,  0.860, 0.03,  2.0,

  // Center Body Strip (side = 0.0)
  -0.028, 0.020,  0.46,  0.484, 0.85,  0.0,
   0.028, 0.020, -0.52,  0.516, 0.10,  0.0
]);

const BUTTERFLY_INDICES = new Uint16Array([
  0, 2, 1,   1, 2, 3,   2, 4, 3,   3, 4, 5,
  6, 8, 7,   7, 8, 9,   8, 10, 9,  9, 10, 11,
  12, 14, 13,  13, 14, 15,  14, 16, 15,  15, 16, 17,
  18, 20, 19,  19, 20, 21,  20, 22, 21,  21, 22, 23,
  4, 12, 11,   11, 12, 19,  24, 12, 11,  12, 25, 11
]);

// ============================================================================
// 4. Biomechanical + Cel-Animation Flight Simulation (Port of src/sim.hpp)
// ============================================================================
const TWO_PI = Math.PI * 2.0;
const PHI = 1.61803398875;

const STATE_FLAPPING = 0;
const STATE_GLIDING = 1;

const GRID_X = 16;
const GRID_Y = 12;
const GRID_Z = 16;
const NUM_CELLS = GRID_X * GRID_Y * GRID_Z;
const CELL_SIZE = 0.65;
const INV_CELL_SIZE = 1.0 / CELL_SIZE;
const GRID_MIN_X = -5.2;
const GRID_MIN_Y = -3.9;
const GRID_MIN_Z = -7.2;

const SPECIES_SCALE = [0.320, 0.350, 0.340, 0.250, 0.230, 0.260, 0.310, 0.250];
const SPECIES_FLAP_HZ = [2.5, 2.2, 2.4, 3.6, 4.0, 3.7, 2.7, 3.8];
const SPECIES_GLIDE_RAD = [0.82, 0.78, 0.84, 0.98, 1.04, 0.96, 0.86, 1.00];

const MOTE_COLORS = [
  [1.00, 0.68, 0.22], // 0: Monarch
  [0.25, 0.78, 1.00], // 1: Blue Morpho
  [1.00, 0.88, 0.32], // 2: Tiger Swallowtail
  [0.88, 0.45, 1.00], // 3: Question Mark
  [0.62, 0.96, 1.00], // 4: Cabbage White
  [1.00, 0.35, 0.72], // 5: Red Admiral
  [0.18, 0.96, 0.85], // 6: Pipevine Swallowtail
  [0.82, 0.42, 1.00]  // 7: Buckeye
];

const MAX_FAIRY_MOTES = 200;

function makeRng(seed) {
  let state = seed >>> 0;
  return {
    next01() {
      const old = state;
      state = (Math.imul(old, 747796405) + 2891336453) >>> 0;
      const shift = ((old >>> 28) + 4) >>> 0;
      const word = Math.imul(((old >>> shift) ^ old) >>> 0, 277803737) >>> 0;
      const res = ((word >>> 22) ^ word) >>> 0;
      return (res & 0x00ffffff) / 0x01000000;
    },
    range(lo, hi) {
      return lo + (hi - lo) * this.next01();
    }
  };
}

function clamp(v, lo, hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

// Compute the butterfly's dorsal 'up' vector rolled around its forward heading axis
function computeRolledUp(fx, fy, fz, rollToCamRad, out) {
  // Sky axis = (0, 1, 0), Cam axis = (0, 0, 1)
  // skyPerp = normalize((0,1,0) - fwd * fy)
  let sx = -fx * fy;
  let sy = 1.0 - fy * fy;
  let sz = -fz * fy;
  let slen = Math.hypot(sx, sy, sz);
  if (slen < 1e-4) {
    sx = 0.0; sy = 1.0; sz = 0.0;
  } else {
    sx /= slen; sy /= slen; sz /= slen;
  }

  // camPerp = normalize((0,0,1) - fwd * fz - skyPerp * sz)
  const dotCamFwd = fz;
  const dotCamSky = sz;
  let cx = -fx * dotCamFwd - sx * dotCamSky;
  let cy = -fy * dotCamFwd - sy * dotCamSky;
  let cz = 1.0 - fz * dotCamFwd - sz * dotCamSky;
  let clen = Math.hypot(cx, cy, cz);
  if (clen < 1e-4) {
    cx = 0.0; cy = 0.0; cz = 1.0;
  } else {
    cx /= clen; cy /= clen; cz /= clen;
  }

  const c = Math.cos(rollToCamRad);
  const s = Math.sin(rollToCamRad);
  let ux = sx * c + cx * s;
  let uy = sy * c + cy * s;
  let uz = sz * c + cz * s;
  let ulen = Math.hypot(ux, uy, uz);
  if (ulen < 1e-4) {
    out[0] = 0.0; out[1] = 0.70; out[2] = 0.71;
  } else {
    out[0] = ux / ulen; out[1] = uy / ulen; out[2] = uz / ulen;
  }
}

function evalCurlTurbulence(px, py, pz, t, seed, out) {
  const sx = px * 0.75 + t * 0.32 + seed;
  const sy = py * 0.75 - t * 0.26 + seed * 1.7;
  const sz = pz * 0.75 + t * 0.29 - seed * 1.3;

  out[0] = Math.sin(sy) * Math.cos(sz) + 0.45 * Math.sin(1.8 * sy + 1.1 * sz);
  out[1] = (Math.sin(sz) * Math.cos(sx) + 0.45 * Math.sin(1.8 * sz + 1.1 * sx)) * 0.65;
  out[2] = (Math.sin(sx) * Math.cos(sy) + 0.45 * Math.sin(1.8 * sx + 1.1 * sy)) * 0.75;
}

class ButterflySimulation {
  constructor(count) {
    this.count = clamp(count, 10, 400);
    this.simTime = 0.0;
    this.agents = [];
    this.cellHead = new Int32Array(NUM_CELLS);
    this.nextAgent = new Int32Array(this.count);

    // 52-byte interleaved instance buffer (13 x 4-byte words per butterfly)
    this.instanceBuffer = new ArrayBuffer(this.count * 52);
    this.instanceFloatView = new Float32Array(this.instanceBuffer);
    this.instanceUintView = new Uint32Array(this.instanceBuffer);

    this.motes = [];
    this.moteRng = makeRng(77777);
    this.moteVerts = new Float32Array(MAX_FAIRY_MOTES * 6 * 7); // 6 verts/mote * 7 floats (x,y,u,v,r,g,b)

    for (let i = 0; i < this.count; ++i) {
      const rng = makeRng(1337 + i * 9781);
      const sp = i % 8;
      const headingAng = rng.range(0.0, TWO_PI);
      let dx = Math.cos(headingAng);
      let dy = rng.range(-0.35, 0.35);
      let dz = Math.sin(headingAng) * 0.35;
      const dlen = Math.hypot(dx, dy, dz) || 1.0;
      dx /= dlen; dy /= dlen; dz /= dlen;

      const spd = rng.range(0.32, 0.58);
      const up = [0, 1, 0];
      computeRolledUp(dx, dy, dz, rng.range(-0.45, 0.75), up);

      const agent = {
        px: rng.range(-3.70, 3.70),
        py: rng.range(-2.05, 2.05),
        pz: rng.range(-4.20, 1.00),
        vx: dx * spd,
        vy: dy * spd,
        vz: dz * spd,
        fx: dx,
        fy: dy,
        fz: dz,
        ux: up[0],
        uy: up[1],
        uz: up[2],
        phase: rng.range(0.0, TWO_PI),
        flap_freq: SPECIES_FLAP_HZ[sp] * rng.range(0.92, 1.08),
        wing_angle: 0.56,
        wing_flex: 0.0,
        state: rng.next01() < 0.45 ? STATE_GLIDING : STATE_FLAPPING,
        state_timer: rng.range(0.6, 2.4),
        noise_seed: rng.range(0.0, 100.0),
        scale: SPECIES_SCALE[sp] * rng.range(0.92, 1.08),
        species_id: sp,
        rng
      };
      this.agents.push(agent);
      this.writeInstance(i, agent);
    }

    for (let m = 0; m < MAX_FAIRY_MOTES; ++m) {
      this.motes.push({
        px: 0, py: 0, pz: 0,
        vx: 0, vy: 0, vz: 0,
        age: 0, max_age: 2.0,
        size_px: 8.0, phase: 0,
        r: 1, g: 1, b: 1
      });
      this.spawnMoteOnButterfly(m, m % this.count, true);
    }
  }

  writeInstance(idx, a) {
    const base = idx * 13;
    const fv = this.instanceFloatView;
    fv[base + 0] = a.px;
    fv[base + 1] = a.py;
    fv[base + 2] = a.pz;
    fv[base + 3] = a.fx;
    fv[base + 4] = a.fy;
    fv[base + 5] = a.fz;
    fv[base + 6] = a.ux;
    fv[base + 7] = a.uy;
    fv[base + 8] = a.uz;
    fv[base + 9] = a.wing_angle;
    fv[base + 10] = a.wing_flex;
    fv[base + 11] = a.scale;
    this.instanceUintView[base + 12] = a.species_id;
  }

  spawnMoteOnButterfly(mIdx, bIdx, initialRandomAge) {
    const b = this.agents[bIdx];
    const m = this.motes[mIdx];
    // right = normalize(cross(forward, up))
    let rx = b.fy * b.uz - b.fz * b.uy;
    let ry = b.fz * b.ux - b.fx * b.uz;
    let rz = b.fx * b.uy - b.fy * b.ux;
    const rlen = Math.hypot(rx, ry, rz) || 1.0;
    rx /= rlen; ry /= rlen; rz /= rlen;

    const side = (mIdx & 1) ? 0.70 : -0.70;
    const rng = this.moteRng;
    m.px = b.px + rx * (side * b.scale) + rng.range(-0.06, 0.06);
    m.py = b.py + ry * (side * b.scale) + rng.range(-0.06, 0.06);
    m.pz = b.pz + rz * (side * b.scale) + rng.range(-0.05, 0.05);

    m.vx = b.vx * 0.18 + rng.range(-0.10, 0.10);
    m.vy = b.vy * 0.18 + rng.range(-0.06, 0.12);
    m.vz = b.vz * 0.18 + rng.range(-0.05, 0.05);

    m.max_age = rng.range(1.50, 2.90);
    m.age = initialRandomAge ? rng.range(0.0, m.max_age) : 0.0;
    m.size_px = rng.range(5.5, 11.5);
    m.phase = rng.range(0.0, TWO_PI);

    const col = MOTE_COLORS[b.species_id & 7];
    m.r = col[0];
    m.g = col[1];
    m.b = col[2];
  }

  step(rawDt, orthoHalfW, orthoHalfH) {
    const dt = clamp(rawDt, 0.001, 0.033);
    this.simTime += dt;
    if (this.simTime > 86400.0) this.simTime -= 86400.0;

    // 1. Rebuild flat O(N) spatial hash grid
    this.cellHead.fill(-1);
    for (let i = 0; i < this.count; ++i) {
      const a = this.agents[i];
      const cx = clamp(((a.px - GRID_MIN_X) * INV_CELL_SIZE) | 0, 0, GRID_X - 1);
      const cy = clamp(((a.py - GRID_MIN_Y) * INV_CELL_SIZE) | 0, 0, GRID_Y - 1);
      const cz = clamp(((a.pz - GRID_MIN_Z) * INV_CELL_SIZE) | 0, 0, GRID_Z - 1);
      const cellIdx = (cz * GRID_Y + cy) * GRID_X + cx;
      this.nextAgent[i] = this.cellHead[cellIdx];
      this.cellHead[cellIdx] = i;
    }

    const turb = [0, 0, 0];
    const targetUp = [0, 1, 0];
    const sepRadius = 0.62;
    const sepRadiusSq = sepRadius * sepRadius;

    // Adapt horizontal bounds smoothly to screen aspect ratio (e.g. 16:9, 16:10, or 3:2 Chromebooks)
    const boundMaxX = Math.max(2.6, orthoHalfW - 0.20);
    const meadowAmpX = Math.max(2.2, orthoHalfW - 0.60);
    const boundMaxY = orthoHalfH - 0.17;
    const boundMinY = -(orthoHalfH - 0.15);
    const meadowAmpY = orthoHalfH - 0.29;

    for (let i = 0; i < this.count; ++i) {
      const a = this.agents[i];

      const cx = clamp(((a.px - GRID_MIN_X) * INV_CELL_SIZE) | 0, 0, GRID_X - 1);
      const cy = clamp(((a.py - GRID_MIN_Y) * INV_CELL_SIZE) | 0, 0, GRID_Y - 1);
      const cz = clamp(((a.pz - GRID_MIN_Z) * INV_CELL_SIZE) | 0, 0, GRID_Z - 1);

      let sepX = 0.0, sepY = 0.0, sepZ = 0.0;
      const zMin = Math.max(0, cz - 1), zMax = Math.min(GRID_Z - 1, cz + 1);
      const yMin = Math.max(0, cy - 1), yMax = Math.min(GRID_Y - 1, cy + 1);
      const xMin = Math.max(0, cx - 1), xMax = Math.min(GRID_X - 1, cx + 1);

      let neighborsChecked = 0;
      for (let nz = zMin; nz <= zMax; ++nz) {
        for (let ny = yMin; ny <= yMax; ++ny) {
          for (let nx = xMin; nx <= xMax; ++nx) {
            let j = this.cellHead[(nz * GRID_Y + ny) * GRID_X + nx];
            while (j !== -1 && neighborsChecked < 16) {
              if (j !== i) {
                const b = this.agents[j];
                const dx = a.px - b.px;
                const dy = a.py - b.py;
                const dz = a.pz - b.pz;
                const d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 1e-6 && d2 < sepRadiusSq) {
                  const invD = 1.0 / Math.sqrt(d2);
                  const push = (sepRadius - (1.0 / invD)) / sepRadius;
                  const w = invD * push;
                  sepX += dx * w;
                  sepY += dy * w;
                  sepZ += dz * w;
                }
                ++neighborsChecked;
              }
              j = this.nextAgent[j];
            }
          }
        }
      }

      // Soft boundary return force keeping butterflies inside full screen
      let boundX = 0.0, boundY = 0.0, boundZ = 0.0;
      if (a.px >  boundMaxX) boundX -= (a.px - boundMaxX) * 1.8;
      if (a.px < -boundMaxX) boundX -= (a.px + boundMaxX) * 1.8;
      if (a.py >  boundMaxY) boundY -= (a.py - boundMaxY) * 2.0;
      if (a.py <  boundMinY) boundY -= (a.py - boundMinY) * 2.0;
      if (a.pz >  1.20)      boundZ -= (a.pz - 1.20) * 1.8;
      if (a.pz < -4.40)      boundZ -= (a.pz + 4.40) * 1.4;

      // Wandering full-screen meadow home
      const fi = i;
      const homeX = meadowAmpX * Math.sin(this.simTime * 0.21 + a.noise_seed + fi * 2.39996);
      const homeY = meadowAmpY * Math.cos(this.simTime * 0.27 + a.noise_seed * 1.3 + fi * PHI);
      const homeZ = -1.60 + 1.40 * Math.sin(this.simTime * 0.19 + a.noise_seed * 0.7 + fi);

      const meadowX = (homeX - a.px) * 0.45;
      const meadowY = (homeY - a.py) * 0.45;
      const meadowZ = (homeZ - a.pz) * 0.45;

      evalCurlTurbulence(a.px, a.py, a.pz, this.simTime, a.noise_seed, turb);
      const soaringSpecies = (a.species_id === 0 || a.species_id === 1 || a.species_id === 2 || a.species_id === 6);
      const turbMul = soaringSpecies ? 0.72 : 1.05;

      let accelX = sepX * 1.15 + meadowX + boundX + turb[0] * turbMul;
      let accelY = sepY * 1.15 + meadowY + boundY + turb[1] * turbMul;
      let accelZ = sepZ * 1.15 + meadowZ + boundZ + turb[2] * turbMul;

      const boundLenSq = boundX * boundX + boundY * boundY + boundZ * boundZ;
      const sepLenSq = sepX * sepX + sepY * sepY + sepZ * sepZ;

      if (a.state === STATE_GLIDING && (boundLenSq > 0.01 || sepLenSq > 0.06)) {
        a.state = STATE_FLAPPING;
        a.phase = 2.38;
        a.state_timer = a.rng.range(0.90, 1.60);
      }

      a.state_timer -= dt;
      if (a.state === STATE_GLIDING) {
        if (a.state_timer <= 0.0) {
          a.state = STATE_FLAPPING;
          a.phase = 2.38;
          a.state_timer = soaringSpecies ? a.rng.range(1.00, 1.75) : a.rng.range(0.65, 1.25);
        }
      } else {
        if (a.state_timer <= 0.0 && Math.sin(a.phase) > 0.35 && boundLenSq <= 0.01) {
          const glideProb = soaringSpecies ? 0.85 : 0.60;
          if (a.rng.next01() < glideProb) {
            a.state = STATE_GLIDING;
            a.state_timer = soaringSpecies ? a.rng.range(1.60, 3.30) : a.rng.range(0.55, 1.20);
            const horizSpd = Math.hypot(a.vx, a.vz);
            const targetHoriz = clamp(horizSpd, 0.24, soaringSpecies ? 0.44 : 0.52);
            const hx = horizSpd > 1e-4 ? (a.vx / horizSpd) : (a.fx >= 0.0 ? 1.0 : -1.0);
            const hz = horizSpd > 1e-4 ? (a.vz / horizSpd) : 0.0;
            a.vx = hx * targetHoriz;
            a.vz = hz * targetHoriz;
            a.vy = soaringSpecies ? -0.17 : -0.20;
          } else {
            a.state_timer = a.rng.range(0.80, 1.50);
          }
        }
      }

      let surge = 1.0;
      if (a.state === STATE_FLAPPING) {
        a.phase += a.flap_freq * TWO_PI * dt;
        if (a.phase > 100.0 * TWO_PI) a.phase -= 100.0 * TWO_PI;

        // Asymmetric "Snap-and-Hold" stroke curve
        const skewPhase = a.phase - 0.32 * Math.sin(a.phase);
        const raw01 = 0.5 + 0.5 * Math.sin(skewPhase);
        const s01 = raw01 * Math.sqrt(raw01);
        const targetAngle = 0.20 + s01 * 1.20;

        const downstrokePhase = -Math.cos(skewPhase) * (1.0 - 0.22 * Math.cos(a.phase));
        const targetFlex = downstrokePhase * 0.44;
        const blend = Math.min(1.0, dt * 18.0);
        a.wing_angle = lerp(a.wing_angle, targetAngle, blend);
        a.wing_flex = lerp(a.wing_flex, targetFlex, blend);

        // Phase-delayed (~35 deg) body counter-bounce & forward surge
        const delayedBounce = -Math.cos(a.phase - 0.58);
        const climbBias = soaringSpecies ? 0.34 : 0.16;
        accelY += climbBias + delayedBounce * (soaringSpecies ? 0.56 : 0.74);
        surge = 1.0 + (soaringSpecies ? 0.36 : 0.46) * delayedBounce;

        a.vx += accelX * dt;
        a.vy += accelY * dt;
        a.vz += accelZ * dt;
        a.vy = clamp(a.vy, soaringSpecies ? -0.14 : -0.24, 0.64);

        const spd = Math.hypot(a.vx, a.vy, a.vz);
        const minSpd = soaringSpecies ? 0.28 : 0.36;
        const maxSpd = soaringSpecies ? 0.58 : 0.68;
        if (spd > 1e-5) {
          if (spd < minSpd) {
            const s = minSpd / spd;
            a.vx *= s; a.vy *= s; a.vz *= s;
          } else if (spd > maxSpd) {
            const s = maxSpd / spd;
            a.vx *= s; a.vy *= s; a.vz *= s;
          }
        }

        // Align forward heading
        let dfx = a.vx, dfy = a.vy * 0.85, dfz = a.vz * 0.45;
        let dflen = Math.hypot(dfx, dfy, dfz);
        if (dflen > 1e-5) {
          dfx /= dflen; dfy /= dflen; dfz /= dflen;
        } else {
          dfx = a.fx; dfy = a.fy; dfz = a.fz;
        }
        const fBlend = Math.min(1.0, dt * (soaringSpecies ? 6.5 : 9.0));
        a.fx = lerp(a.fx, dfx, fBlend);
        a.fy = lerp(a.fy, dfy, fBlend);
        a.fz = lerp(a.fz, dfz, fBlend);
        const flen = Math.hypot(a.fx, a.fy, a.fz) || 1.0;
        a.fx /= flen; a.fy /= flen; a.fz /= flen;

        // 3D roll around forward axis
        const baseRoll = 0.16 + 0.48 * Math.sin(this.simTime * 0.72 + a.noise_seed * 1.9);
        const turnRoll = clamp(accelZ * 0.55 + accelX * 0.25, -0.48, 0.48);
        const flapRock = 0.24 * Math.sin(a.phase + a.noise_seed);
        const flapRoll = clamp(baseRoll + turnRoll + flapRock, -0.62, 0.85);

        computeRolledUp(a.fx, a.fy, a.fz, flapRoll, targetUp);
        const upBlend = Math.min(1.0, dt * 6.5);
        a.ux = lerp(a.ux, targetUp[0], upBlend);
        a.uy = lerp(a.uy, targetUp[1], upBlend);
        a.uz = lerp(a.uz, targetUp[2], upBlend);
        const ulen = Math.hypot(a.ux, a.uy, a.uz) || 1.0;
        a.ux /= ulen; a.uy /= ulen; a.uz /= ulen;
      } else {
        const damp = Math.min(1.0, dt * 8.0);
        const targetGlideAngle = SPECIES_GLIDE_RAD[a.species_id & 7] + 0.04 * Math.sin(a.noise_seed);
        a.wing_angle = lerp(a.wing_angle, targetGlideAngle, damp);
        a.wing_flex = lerp(a.wing_flex, -0.04, damp);
        surge = 0.95;

        if (a.vy > -0.14) {
          a.vy = soaringSpecies ? -0.17 : -0.20;
        }
        const spd = Math.hypot(a.vx, a.vy, a.vz);
        const minGlide = soaringSpecies ? 0.26 : 0.32;
        const maxGlide = soaringSpecies ? 0.50 : 0.58;
        const glideSpd = clamp(spd * (1.0 - 0.12 * dt), minGlide, maxGlide);
        if (spd > 1e-5) {
          const s = glideSpd / spd;
          a.vx *= s; a.vy *= s; a.vz *= s;
        }

        let gfx = a.vx, gfy = a.vy * 0.85, gfz = a.vz * 0.45;
        let gflen = Math.hypot(gfx, gfy, gfz);
        if (gflen > 1e-5) {
          gfx /= gflen; gfy /= gflen; gfz /= gflen;
        } else {
          gfx = a.fx; gfy = a.fy; gfz = a.fz;
        }
        const gBlend = Math.min(1.0, dt * 8.0);
        a.fx = lerp(a.fx, gfx, gBlend);
        a.fy = lerp(a.fy, gfy, gBlend);
        a.fz = lerp(a.fz, gfz, gBlend);
        const flen = Math.hypot(a.fx, a.fy, a.fz) || 1.0;
        a.fx /= flen; a.fy /= flen; a.fz /= flen;

        const depthBank = a.vz * 0.85;
        const thermalDrift = 0.15
          + 0.54 * Math.sin(this.simTime * 1.05 + a.noise_seed * 2.3)
          + 0.26 * Math.cos(this.simTime * 1.75 + a.noise_seed * 0.9);
        const glideRoll = clamp(depthBank + thermalDrift, -0.58, 0.85);

        computeRolledUp(a.fx, a.fy, a.fz, glideRoll, targetUp);
        const upBlend = Math.min(1.0, dt * 5.0);
        a.ux = lerp(a.ux, targetUp[0], upBlend);
        a.uy = lerp(a.uy, targetUp[1], upBlend);
        a.uz = lerp(a.uz, targetUp[2], upBlend);
        const ulen = Math.hypot(a.ux, a.uy, a.uz) || 1.0;
        a.ux /= ulen; a.uy /= ulen; a.uz /= ulen;
      }

      a.px += a.vx * (dt * surge);
      a.py += a.vy * (dt * surge);
      a.pz += a.vz * (dt * surge);

      this.writeInstance(i, a);
    }

    // Step wingtip fairy-dust motes
    for (let m = 0; m < MAX_FAIRY_MOTES; ++m) {
      const mote = this.motes[m];
      mote.age += dt;
      if (mote.age >= mote.max_age) {
        const bIdx = (m * 7 + ((this.simTime * 17.0) | 0)) % this.count;
        this.spawnMoteOnButterfly(m, bIdx, false);
      } else {
        const swirlX = 0.08 * Math.sin(this.simTime * 1.4 + mote.phase);
        const swirlY = 0.06 * Math.cos(this.simTime * 1.1 + mote.phase * 1.3);
        mote.px += (mote.vx + swirlX) * dt;
        mote.py += (mote.vy + swirlY) * dt;
        mote.pz += mote.vz * dt;
        const drag = 1.0 - 0.65 * dt;
        mote.vx *= drag;
        mote.vy *= drag;
        mote.vz *= drag;
      }
    }
  }

  buildFairyMoteVertices(orthoHalfW, orthoHalfH, screenW, screenH) {
    let vFloats = 0;
    const out = this.moteVerts;

    for (let i = 0; i < MAX_FAIRY_MOTES; ++i) {
      const m = this.motes[i];
      const life01 = clamp(m.age / m.max_age, 0.0, 1.0);
      const env = Math.sin(life01 * Math.PI);
      const twinkle = 0.62 + 0.38 * Math.sin(this.simTime * 5.2 + m.phase);
      const brightness = env * twinkle;
      if (brightness < 0.05) continue;

      const px = ((m.px / orthoHalfW) + 1.0) * 0.5 * screenW;
      const py = (1.0 - (m.py / orthoHalfH)) * 0.5 * screenH;
      if (px < -16.0 || px > screenW + 16.0 || py < -16.0 || py > screenH + 16.0) continue;

      const depthScale = clamp(1.0 + (m.pz + 1.8) * 0.14, 0.58, 1.38);
      const rad = m.size_px * depthScale * (0.55 + 0.45 * brightness);
      const cr = m.r * brightness;
      const cg = m.g * brightness;
      const cb = m.b * brightness;

      const x0 = px - rad, x1 = px + rad;
      const y0 = py - rad, y1 = py + rad;

      // Tri 1
      out[vFloats++] = x0; out[vFloats++] = y0; out[vFloats++] = -1; out[vFloats++] = -1; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb;
      out[vFloats++] = x1; out[vFloats++] = y0; out[vFloats++] =  1; out[vFloats++] = -1; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb;
      out[vFloats++] = x1; out[vFloats++] = y1; out[vFloats++] =  1; out[vFloats++] =  1; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb;
      // Tri 2
      out[vFloats++] = x0; out[vFloats++] = y0; out[vFloats++] = -1; out[vFloats++] = -1; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb;
      out[vFloats++] = x1; out[vFloats++] = y1; out[vFloats++] =  1; out[vFloats++] =  1; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb;
      out[vFloats++] = x0; out[vFloats++] = y1; out[vFloats++] = -1; out[vFloats++] =  1; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb;
    }

    return vFloats / 7;
  }
}

// ============================================================================
// 5. WebGL2 Initialization, Texture Array Loading & Render Loop
// ============================================================================
function compileShader(gl, type, src) {
  const sh = gl.createShader(type);
  gl.shaderSource(sh, src);
  gl.compileShader(sh);
  if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
    throw new Error("Shader compile error: " + gl.getShaderInfoLog(sh));
  }
  return sh;
}

function createProgram(gl, vsSrc, fsSrc) {
  const vs = compileShader(gl, gl.VERTEX_SHADER, vsSrc);
  const fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSrc);
  const prog = gl.createProgram();
  gl.attachShader(prog, vs);
  gl.attachShader(prog, fs);
  gl.linkProgram(prog);
  gl.deleteShader(vs);
  gl.deleteShader(fs);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
    throw new Error("Program link error: " + gl.getProgramInfoLog(prog));
  }
  return prog;
}

function loadImage(url) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = (err) => reject(err);
    img.src = url;
  });
}

async function createSpeciesTextureArray(gl) {
  const urls = [];
  for (let i = 0; i < 8; ++i) {
    urls.push(`assets/species_${i}.png`);
  }
  const images = await Promise.all(urls.map(loadImage));

  const tex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D_ARRAY, tex);
  gl.texStorage3D(gl.TEXTURE_2D_ARRAY, 9, gl.RGBA8, 512, 512, 8);

  // Flip vertically to match OpenGL UV convention (V=1.0 is head/apex, V=0.0 is tail)
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
  for (let layer = 0; layer < 8; ++layer) {
    gl.texSubImage3D(
      gl.TEXTURE_2D_ARRAY,
      0,
      0, 0, layer,
      512, 512, 1,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      images[layer]
    );
  }
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);

  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.generateMipmap(gl.TEXTURE_2D_ARRAY);
  gl.bindTexture(gl.TEXTURE_2D_ARRAY, null);
  return tex;
}

// Compute column-major orthographic view-projection matrix matching main.cpp:
// camPos = (0, 0, 8.6) looking at (0, 0, -1), Ortho(-orthoHalfW, orthoHalfW, -orthoHalfH, orthoHalfH, 0.5, 35.0)
function computeOrthoViewProj(orthoHalfW, orthoHalfH) {
  const near = 0.5;
  const far = 35.0;
  const camZ = 8.6;
  return new Float32Array([
    1.0 / orthoHalfW, 0.0, 0.0, 0.0,
    0.0, 1.0 / orthoHalfH, 0.0, 0.0,
    0.0, 0.0, -2.0 / (far - near), 0.0,
    0.0, 0.0, -(far + near + 2.0 * camZ) / (far - near), 1.0
  ]);
}

async function main() {
  const params = new URLSearchParams(window.location.search);
  const butterflyCount = clamp(Number(params.get("count")) || 75, 10, 300);

  const canvas = document.getElementById("glcanvas");
  const gl = canvas.getContext("webgl2", {
    alpha: false,
    depth: true,
    stencil: false,
    antialias: false, // Match Pi 4B crisp alpha-cutout without MSAA blur
    powerPreference: "high-performance"
  });

  if (!gl) {
    console.error("WebGL2 is required for A Kaleidoscope of Butterflies.");
    return;
  }

  const butterflyProg = createProgram(gl, BUTTERFLY_VS_SRC, BUTTERFLY_FS_SRC);
  const locViewProj = gl.getUniformLocation(butterflyProg, "u_viewProj");
  const locTexArray = gl.getUniformLocation(butterflyProg, "u_texArray");
  const locLightDir = gl.getUniformLocation(butterflyProg, "u_lightDir");

  const moteProg = createProgram(gl, MOTE_VS_SRC, MOTE_FS_SRC);
  const locMoteRes = gl.getUniformLocation(moteProg, "u_resolution");

  const speciesTexArray = await createSpeciesTextureArray(gl);
  const sim = new ButterflySimulation(butterflyCount);

  // Setup Butterfly Instanced VAO
  const butterflyVAO = gl.createVertexArray();
  const meshVBO = gl.createBuffer();
  const meshEBO = gl.createBuffer();
  const instanceVBO = gl.createBuffer();

  gl.bindVertexArray(butterflyVAO);

  gl.bindBuffer(gl.ARRAY_BUFFER, meshVBO);
  gl.bufferData(gl.ARRAY_BUFFER, BUTTERFLY_VERTS, gl.STATIC_DRAW);

  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, meshEBO);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, BUTTERFLY_INDICES, gl.STATIC_DRAW);

  // MeshVertex stride = 24 bytes (6 floats)
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
  gl.vertexAttribDivisor(0, 0);

  gl.enableVertexAttribArray(1);
  gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 24, 12);
  gl.vertexAttribDivisor(1, 0);

  gl.enableVertexAttribArray(2);
  gl.vertexAttribPointer(2, 1, gl.FLOAT, false, 24, 20);
  gl.vertexAttribDivisor(2, 0);

  // ButterflyInstance stride = 52 bytes
  gl.bindBuffer(gl.ARRAY_BUFFER, instanceVBO);
  gl.bufferData(gl.ARRAY_BUFFER, sim.instanceBuffer.byteLength, gl.STREAM_DRAW);

  gl.enableVertexAttribArray(3);
  gl.vertexAttribPointer(3, 3, gl.FLOAT, false, 52, 0);
  gl.vertexAttribDivisor(3, 1);

  gl.enableVertexAttribArray(4);
  gl.vertexAttribPointer(4, 3, gl.FLOAT, false, 52, 12);
  gl.vertexAttribDivisor(4, 1);

  gl.enableVertexAttribArray(5);
  gl.vertexAttribPointer(5, 3, gl.FLOAT, false, 52, 24);
  gl.vertexAttribDivisor(5, 1);

  gl.enableVertexAttribArray(6);
  gl.vertexAttribPointer(6, 1, gl.FLOAT, false, 52, 36);
  gl.vertexAttribDivisor(6, 1);

  gl.enableVertexAttribArray(7);
  gl.vertexAttribPointer(7, 1, gl.FLOAT, false, 52, 40);
  gl.vertexAttribDivisor(7, 1);

  gl.enableVertexAttribArray(8);
  gl.vertexAttribPointer(8, 1, gl.FLOAT, false, 52, 44);
  gl.vertexAttribDivisor(8, 1);

  // Attribute 9 (species_id) is uint32 -> MUST use vertexAttribIPointer
  gl.enableVertexAttribArray(9);
  gl.vertexAttribIPointer(9, 1, gl.UNSIGNED_INT, 52, 48);
  gl.vertexAttribDivisor(9, 1);

  gl.bindVertexArray(null);

  // Setup Fairy-Dust Mote VAO
  const moteVAO = gl.createVertexArray();
  const moteVBO = gl.createBuffer();
  gl.bindVertexArray(moteVAO);
  gl.bindBuffer(gl.ARRAY_BUFFER, moteVBO);
  gl.bufferData(gl.ARRAY_BUFFER, sim.moteVerts.byteLength, gl.STREAM_DRAW);

  // MoteVertex stride = 28 bytes (7 floats: x, y, u, v, r, g, b)
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 28, 0);
  gl.enableVertexAttribArray(1);
  gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 28, 8);
  gl.enableVertexAttribArray(2);
  gl.vertexAttribPointer(2, 3, gl.FLOAT, false, 28, 16);
  gl.bindVertexArray(null);

  // Normalized key light direction (-0.35, -0.80, -0.48)
  const lLen = Math.hypot(-0.35, -0.80, -0.48);
  const lightDir = new Float32Array([-0.35 / lLen, -0.80 / lLen, -0.48 / lLen]);

  let lastTime = performance.now();

  function frame(now) {
    const dt = Math.min(0.033, Math.max(0.001, (now - lastTime) * 0.001));
    lastTime = now;

    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const displayW = Math.max(1, Math.floor(window.innerWidth * dpr));
    const displayH = Math.max(1, Math.floor(window.innerHeight * dpr));
    if (canvas.width !== displayW || canvas.height !== displayH) {
      canvas.width = displayW;
      canvas.height = displayH;
    }

    const aspect = displayW / displayH;
    const orthoHalfH = 2.25;
    const orthoHalfW = orthoHalfH * aspect;

    sim.step(dt, orthoHalfW, orthoHalfH);

    gl.viewport(0, 0, displayW, displayH);
    gl.clearColor(0.0, 0.0, 0.0, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    // 1. Fairy-dust motes pass
    const numMoteVerts = sim.buildFairyMoteVertices(orthoHalfW, orthoHalfH, displayW, displayH);
    if (numMoteVerts > 0) {
      gl.useProgram(moteProg);
      gl.uniform2f(locMoteRes, displayW, displayH);
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.CULL_FACE);
      gl.disable(gl.BLEND);

      gl.bindVertexArray(moteVAO);
      gl.bindBuffer(gl.ARRAY_BUFFER, moteVBO);
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, sim.moteVerts.subarray(0, numMoteVerts * 7));
      gl.drawArrays(gl.TRIANGLES, 0, numMoteVerts);
    }

    // 2. Instanced 3D Butterflies pass
    const viewProjMat = computeOrthoViewProj(orthoHalfW, orthoHalfH);
    gl.useProgram(butterflyProg);
    gl.uniformMatrix4fv(locViewProj, false, viewProjMat);
    gl.uniform1i(locTexArray, 0);
    gl.uniform3fv(locLightDir, lightDir);

    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LESS);
    gl.depthMask(true);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, speciesTexArray);

    gl.bindVertexArray(butterflyVAO);
    gl.bindBuffer(gl.ARRAY_BUFFER, instanceVBO);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, sim.instanceFloatView);
    gl.drawElementsInstanced(gl.TRIANGLES, BUTTERFLY_INDICES.length, gl.UNSIGNED_SHORT, 0, sim.count);

    gl.bindVertexArray(null);
    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
}

main().catch((err) => console.error("[Butterfly Screensaver Error]", err));
