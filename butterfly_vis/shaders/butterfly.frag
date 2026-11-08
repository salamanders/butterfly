#version 300 es
precision highp float;
precision highp sampler2DArray;

in vec2 v_uv;
in vec3 v_normal;
flat in uint v_species;

uniform sampler2DArray u_texArray;
uniform vec3 u_lightDir; // Normalized vector pointing FROM light TO scene, e.g. normalize(vec3(-0.35, -0.80, -0.48))

out vec4 fragColor;

void main() {
    vec4 tex = texture(u_texArray, vec3(v_uv, float(v_species)));
    if (tex.a < 0.08) discard; // Preserves full inner wing boundaries and body contours

    vec3 dorsalN = normalize(v_normal);
    const vec3 V = vec3(0.0, 0.0, 1.0); // Constant orthographic view vector (saves per-pixel normalize & v_worldPos varying!)
    vec3 L = normalize(-u_lightDir); // Surface-to-light vector

    // Smooth dorsal/ventral blend across grazing angles so curved wings never snap at gl_FrontFacing
    float facing = dot(dorsalN, V);
    float dorsalFactor = smoothstep(-0.15, 0.15, facing);
    vec3 N = normalize(dorsalN * (facing >= 0.0 ? 1.0 : -1.0));

    // Ventral (underside) of butterfly wings is slightly softer than dorsal topside,
    // but kept luminous so wings never look dull or muddy in shadow
    vec3 ventralAlbedo = tex.rgb * vec3(0.92, 0.90, 0.94);
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

    // Infuse dark/earthy pigment regions with per-species bioluminescent undertone
    // while keeping veins and markings distinct
    float luma = dot(baseAlbedo, vec3(0.299, 0.587, 0.114));
    float darkPigment = 1.0 - smoothstep(0.05, 0.42, luma);
    float wingSpan = clamp(abs(v_uv.x - 0.5) * 2.2, 0.0, 1.0);
    baseAlbedo = mix(baseAlbedo, baseAlbedo * 0.55 + bioTint * (0.28 + 0.18 * wingSpan), darkPigment * 0.68);

    // Velvety solid floor: Ensure dark wing scales never drop to pitch black (#000000)
    // so black veins, body, and inner wing bases remain solid and visible against the black wall
    vec3 velvetBlack = mix(vec3(0.09, 0.09, 0.11), bioTint * 0.22, 0.35);
    baseAlbedo = max(baseAlbedo, velvetBlack);

    // Boost jewel-tone vibrance so colors gleam against the #000000 background
    float newLuma = dot(baseAlbedo, vec3(0.299, 0.587, 0.114));
    baseAlbedo = clamp(mix(vec3(newLuma), baseAlbedo, 1.36), 0.0, 1.0);

    // 1. Stained-glass two-sided lighting with high self-luminous floor (never dark in shadow)
    float ndotl = max(dot(N, L), 0.0) + 0.58 * max(dot(-N, L), 0.0);
    float hemi  = 0.56 + 0.16 * dorsalN.y;
    vec3 diffuse = baseAlbedo * (hemi + 0.78 * ndotl);

    // 2. Bioluminescent Subsurface Scattering (stained-glass inner wing glow)
    float backAlign = clamp(dot(V, -L), 0.0, 1.0);
    float wrapBack  = clamp(dot(-N, L), 0.0, 1.0);
    float sssFactor = pow(backAlign, 2.0) * 0.55 + wrapBack * 0.45 + 0.22;
    float membrane  = smoothstep(0.06, 0.55, newLuma);
    vec3 sss = mix(baseAlbedo, bioTint, 0.35) * sssFactor * (0.28 + 0.65 * membrane);

    // 3. All-Species Structural Iridescence + Wingtip Rim Luminescence
    float ndotv = clamp(abs(dot(N, V)), 0.0, 1.0);
    float fresnel = pow(1.0 - ndotv, 1.75);
    float edgeGlow = smoothstep(0.28, 0.88, wingSpan);
    vec3 iridTint = mix(iridHueA, iridHueB, clamp(fresnel * 0.75 + edgeGlow * 0.35, 0.0, 1.0));
    vec3 iridescence = iridTint * iridStrength * (0.45 + 0.55 * ndotl) * (0.28 + 0.72 * max(fresnel, edgeGlow * 0.65));

    // 4. Microscopic Wing-Scale Shimmer (twinkles along outer wing scales as wings beat)
    vec2 scaleGrid = v_uv * 115.0 + dorsalN.xy * 9.5;
    float sparkleWave = sin(scaleGrid.x) * sin(scaleGrid.y) * cos((scaleGrid.x - scaleGrid.y) * 0.7);
    float sparkle = pow( clamp(sparkleWave, 0.0, 1.0), 5.0 ) * (0.35 + 0.65 * edgeGlow);
    vec3 scaleShimmer = mix(iridHueA, vec3(1.0, 0.97, 0.88), 0.55) * sparkle * 0.55;

    // 5. Subtle specular highlight on dark chitin body and wing ribs
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 16.0) * 0.25;

    vec3 color = diffuse + sss + iridescence + scaleShimmer + vec3(spec);
    // Soft highlight roll-off so vivid jewel tones never clip harshly on the projector
    color = color / (vec3(1.0) + color * 0.16);

    fragColor = vec4(color, 1.0);
}
