#pragma once
#include "cpp26.hpp"
#include <raylib.h>
#include <raymath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <span>
#include <vector>

struct ButterflyInstance {
    float pos[3];         // Location 3 (vec3): World position (meters)
    float forward[3];     // Location 4 (vec3): Normalized heading vector
    float up[3];          // Location 5 (vec3): Normalized banked up vector
    float wing_angle;     // Location 6 (float): Current main flap angle in radians [-0.9, +1.25]
    float wing_flex;      // Location 7 (float): Wingtip lag/bend factor [-0.6, +0.6]
    float scale;          // Location 8 (float): World wingspan scale (e.g. 0.08f .. 0.15f)
    uint32_t species_id;  // Location 9 (uint):  Layer index [0..7] in GL_TEXTURE_2D_ARRAY
};
static_assert(sizeof(ButterflyInstance) == 52, "ButterflyInstance must be tightly packed to 52 bytes (13 x 4B)");

namespace bf {

inline constexpr int MAX_BUTTERFLIES = 4096;
inline constexpr int GRID_X = 16;
inline constexpr int GRID_Y = 12;
inline constexpr int GRID_Z = 16;
inline constexpr float CELL_SIZE = 0.65f;
inline constexpr float INV_CELL_SIZE = 1.0f / CELL_SIZE;

// Grid origin covering X in [-5.2, +5.2], Y in [-3.9, +3.9], Z in [-7.2, +3.2]
inline constexpr float GRID_MIN_X = -5.2f;
inline constexpr float GRID_MIN_Y = -3.9f;
inline constexpr float GRID_MIN_Z = -7.2f;

// Flight states
inline constexpr int STATE_FLAPPING = 0;
inline constexpr int STATE_GLIDING  = 1;
inline constexpr int STATE_PERCHED  = 2;

// Interactive hand-perching constants in 3D orthographic wall meters
// (where wall height = 4.5m [-2.25..+2.25] for 1080p, so 1m = 240 px)
inline constexpr float PERCH_STEADY_RADIUS_M = 0.135f; // ~32 px drift tolerance while holding hand still
inline constexpr float PERCH_WAKE_DRIFT_M    = 0.200f; // ~48 px movement startles perched butterflies into flight
inline constexpr float PERCH_HOLD_DELAY_SEC  = 0.80f;  // Seconds palm stays steady before butterflies land
inline constexpr int   MAX_PERCHED_PER_HAND  = 5;      // Uncrowded golden-angle cluster of 2x-scale butterflies per hand

struct PerchSlotPolar {
    float base_angle;
    float radius;
};

// Portable consteval square-root (works in both GCC and Clang consteval)
consteval float constevalSqrt(float x) noexcept {
    float g = x;
    for (int iter = 0; iter < 12; ++iter) {
        g = 0.5f * (g + x / g);
    }
    return g;
}

// Consteval golden-angle spiral lookup table computed at compile time
consteval std::array<PerchSlotPolar, MAX_PERCHED_PER_HAND> makeGoldenPerchTable() {
    std::array<PerchSlotPolar, MAX_PERCHED_PER_HAND> table{};
    for (int slot = 0; slot < MAX_PERCHED_PER_HAND; ++slot) {
        table[slot] = {
            .base_angle = float(slot) * 2.39996323f,
            .radius     = 0.095f + 0.135f * constevalSqrt(float(slot) + 0.4f)
        };
    }
    return table;
}
inline constexpr auto kGoldenPerchTable = makeGoldenPerchTable();

struct WorldPalm {
    float x, y;         // 2D orthographic wall position in meters
    bool startled;
    bool is_steady;
};

inline constexpr int MAX_FAIRY_MOTES = 200;

struct FairyMote {
    Vector3 pos;
    Vector3 vel;
    float age;
    float max_age;
    float size_px;
    float phase;
    float r, g, b;
};

// Safe wrapper around raymath Vector3Normalize with custom fallback when length approaches 0
inline Vector3 safeNormalize(Vector3 v, Vector3 fallback = {0.0f, 0.0f, 1.0f}) noexcept {
    if (Vector3LengthSqr(v) < 1e-8f) return fallback;
    return Vector3Normalize(v);
}

// Compute the butterfly's dorsal 'up' vector rolled around its forward heading axis.
// rollToCamRad = 0.0 rad -> upright profile (dorsal back points toward screen top +Y).
// rollToCamRad > 0.0 rad -> banked toward camera (+Z), opening the far wing toward the viewer.
// rollToCamRad < 0.0 rad -> banked away from camera (-Z), opening the near wing toward the viewer.
inline Vector3 computeRolledUp(Vector3 fwd, float rollToCamRad) noexcept {
    constexpr Vector3 kSkyAxis = {0.0f, 1.0f, 0.0f};
    constexpr Vector3 kCamAxis = {0.0f, 0.0f, 1.0f};
    Vector3 skyPerp = safeNormalize(kSkyAxis - fwd * Vector3DotProduct(kSkyAxis, fwd), kSkyAxis);
    Vector3 camPerp = safeNormalize(
        kCamAxis - fwd * Vector3DotProduct(kCamAxis, fwd) - skyPerp * Vector3DotProduct(kCamAxis, skyPerp),
        kCamAxis
    );
    return safeNormalize(
        skyPerp * std::cos(rollToCamRad) + camPerp * std::sin(rollToCamRad),
        {0.0f, 0.70f, 0.71f}
    );
}

constexpr uint32_t pcgHash(uint32_t& state) noexcept {
    uint32_t old = state;
    state = old * 747796405u + 2891336453u;
    uint32_t word = ((old >> ((old >> 28u) + 4u)) ^ old) * 277803737u;
    return (word >> 22u) ^ word;
}

constexpr float rand01(uint32_t& state) noexcept {
    return (pcgHash(state) & 0x00FFFFFFu) / float(0x01000000u);
}

constexpr float randRange(uint32_t& state, float lo, float hi) noexcept {
    return lo + (hi - lo) * rand01(state);
}

// Analytical divergence-free 3D trigonometric curl-noise for gentle independent meandering
inline Vector3 evalCurlTurbulence(Vector3 p, float t, float seed) noexcept {
    float sx = p.x * 0.75f + t * 0.32f + seed;
    float sy = p.y * 0.75f - t * 0.26f + seed * 1.7f;
    float sz = p.z * 0.75f + t * 0.29f - seed * 1.3f;

    float cx = std::sin(sy) * std::cos(sz) + 0.45f * std::sin(1.8f * sy + 1.1f * sz);
    float cy = std::sin(sz) * std::cos(sx) + 0.45f * std::sin(1.8f * sz + 1.1f * sx);
    float cz = std::sin(sx) * std::cos(sy) + 0.45f * std::sin(1.8f * sx + 1.1f * sy);
    return {cx, cy * 0.65f, cz * 0.75f};
}

struct AgentState {
    Vector3 pos;
    Vector3 vel;
    Vector3 forward;
    Vector3 up;
    float phase;
    float flap_freq;
    float wing_angle;
    float wing_flex;
    float state_timer;
    float noise_seed;
    float scale;
    float preferred_orbit_m;
    int orbit_dir;
    int state;              // STATE_FLAPPING, STATE_GLIDING, or STATE_PERCHED
    int perch_hand_idx;
    float perch_offset_x;
    float perch_offset_y;
    uint32_t species_id;
    uint32_t rng;
};

class ButterflySimulation {
public:
    int count = 75;
    float simTime = 0.0f;
    AgentState agents[MAX_BUTTERFLIES];
    AgentState nextAgents[MAX_BUTTERFLIES];
    int cellHead[GRID_Z][GRID_Y][GRID_X];
    int nextAgent[MAX_BUTTERFLIES];
    std::vector<ButterflyInstance> instances;
    FairyMote motes[MAX_FAIRY_MOTES];
    uint32_t moteRng_ = 77777u;

    // Steady-hand anchor tracking (up to 2 simultaneous palms)
    std::vector<std::pair<float, float>> palmAnchors_;
    std::vector<float> palmSteadyTimers_;
    std::vector<WorldPalm> activeWorldPalms_;
    int perchedCounts_[2] = {0, 0};

    static constexpr float kMoteColors[8][3] = {
        {1.00f, 0.68f, 0.22f}, // 0: Monarch (solar gold)
        {0.25f, 0.78f, 1.00f}, // 1: Blue Morpho (celestial cyan-blue)
        {1.00f, 0.88f, 0.32f}, // 2: Tiger Swallowtail (sun-gold)
        {0.88f, 0.45f, 1.00f}, // 3: Question Mark (amber-violet)
        {0.62f, 0.96f, 1.00f}, // 4: Cabbage White (moonlit opal)
        {1.00f, 0.35f, 0.72f}, // 5: Red Admiral (ruby-magenta)
        {0.18f, 0.96f, 0.85f}, // 6: Pipevine Swallowtail (emerald-teal)
        {0.82f, 0.42f, 1.00f}  // 7: Buckeye (amethyst)
    };

    void spawnMoteOnButterfly(int mIdx, int bIdx, bool initialRandomAge) noexcept {
        const AgentState& b = agents[bIdx];
        FairyMote& m = motes[mIdx];
        Vector3 right = safeNormalize(Vector3CrossProduct(b.forward, b.up), {1.0f, 0.0f, 0.0f});
        float side = (mIdx & 1) ? 0.70f : -0.70f;
        m.pos = b.pos + right * (side * b.scale) + Vector3{
            randRange(moteRng_, -0.06f, 0.06f),
            randRange(moteRng_, -0.06f, 0.06f),
            randRange(moteRng_, -0.05f, 0.05f)
        };
        m.vel = b.vel * 0.18f + Vector3{
            randRange(moteRng_, -0.10f, 0.10f),
            randRange(moteRng_, -0.06f, 0.12f),
            randRange(moteRng_, -0.05f, 0.05f)
        };
        m.max_age = randRange(moteRng_, 1.50f, 2.90f);
        m.age = initialRandomAge ? randRange(moteRng_, 0.0f, m.max_age) : 0.0f;
        m.size_px = randRange(moteRng_, 5.5f, 11.5f);
        m.phase = randRange(moteRng_, 0.0f, 6.2831853f);
        uint32_t sp = b.species_id & 7u;
        m.r = kMoteColors[sp][0];
        m.g = kMoteColors[sp][1];
        m.b = kMoteColors[sp][2];
    }

    void init(int numButterflies) {
        count = std::clamp(numButterflies, 10, MAX_BUTTERFLIES);
        instances.resize(count);
        simTime = 0.0f;

        // Species 2x hero scale, languid soaring vs. smaller flitting flap frequencies, and hand-orbit radius tables:
        // 0:Monarch, 1:Morpho, 2:TigerSwallowtail, 3:QuestionMark, 4:CabbageWhite, 5:RedAdmiral, 6:Pipevine, 7:Buckeye
        static constexpr float kSpeciesScale[8] = {
            0.320f, 0.350f, 0.340f, 0.250f, 0.230f, 0.260f, 0.310f, 0.250f
        };
        static constexpr float kSpeciesFlapHz[8] = {
            2.5f,   2.2f,   2.4f,   3.6f,   4.0f,   3.7f,   2.7f,   3.8f
        };
        static constexpr float kOrbitMinM[8] = {
            0.42f,  0.48f,  0.44f,  0.28f,  0.24f,  0.32f,  0.40f,  0.28f
        };
        static constexpr float kOrbitMaxM[8] = {
            0.82f,  0.92f,  0.86f,  0.58f,  0.50f,  0.64f,  0.78f,  0.58f
        };

        constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
        for (int i = 0; i < count; ++i) {
            uint32_t rng = 1337u + uint32_t(i) * 9781u;
            AgentState& a = agents[i];
            a.rng = rng;
            a.species_id = uint32_t(i % 8);

            a.pos = {
                randRange(a.rng, -3.5f, 3.5f),
                randRange(a.rng, -1.9f, 1.9f),
                randRange(a.rng, -4.2f, 1.0f)
            };

            float headingAng = randRange(a.rng, 0.0f, kTwoPi);
            Vector3 dir = {std::cos(headingAng), randRange(a.rng, -0.35f, 0.35f), std::sin(headingAng) * 0.35f};
            a.vel = safeNormalize(dir) * randRange(a.rng, 0.32f, 0.58f);
            a.forward = safeNormalize(a.vel);
            a.up = computeRolledUp(a.forward, randRange(a.rng, -0.45f, 0.75f));

            a.phase = randRange(a.rng, 0.0f, kTwoPi);
            a.flap_freq = kSpeciesFlapHz[a.species_id] * randRange(a.rng, 0.92f, 1.08f);
            a.wing_angle = 0.56f; // Start in a pleasant ~32-deg upward V (never flat)
            a.wing_flex = 0.0f;
            a.state = (rand01(a.rng) < 0.45f) ? STATE_GLIDING : STATE_FLAPPING;
            a.state_timer = randRange(a.rng, 0.6f, 2.4f);
            a.noise_seed = randRange(a.rng, 0.0f, 100.0f);
            a.scale = kSpeciesScale[a.species_id] * randRange(a.rng, 0.92f, 1.08f);
            a.preferred_orbit_m = randRange(a.rng, kOrbitMinM[a.species_id], kOrbitMaxM[a.species_id]);
            a.orbit_dir = (i & 1) ? 1 : -1;
            a.perch_hand_idx = -1;
            a.perch_offset_x = 0.0f;
            a.perch_offset_y = 0.0f;

            writeInstance(i, a);
        }

        moteRng_ = 77777u;
        for (int m = 0; m < MAX_FAIRY_MOTES; ++m) {
            spawnMoteOnButterfly(m, m % count, true);
        }
    }

    inline void writeInstance(int idx, const AgentState& a) noexcept {
        ButterflyInstance& inst = instances[idx];
        inst.pos[0] = a.pos.x;
        inst.pos[1] = a.pos.y;
        inst.pos[2] = a.pos.z;

        inst.forward[0] = a.forward.x;
        inst.forward[1] = a.forward.y;
        inst.forward[2] = a.forward.z;

        inst.up[0] = a.up.x;
        inst.up[1] = a.up.y;
        inst.up[2] = a.up.z;

        inst.wing_angle = a.wing_angle;
        inst.wing_flex  = a.wing_flex;
        inst.scale      = a.scale;
        inst.species_id = a.species_id;
    }

    // Update palm steadiness timers & anchors in 3D wall coordinates
    void updatePalmSteadiness(float dt, std::span<const std::pair<float, float>> rawWorldPalms) {
        size_t n = std::min<size_t>(rawWorldPalms.size(), 2);
        std::vector<std::pair<float, float>> newAnchors(n);
        std::vector<float> newTimers(n, 0.0f);
        activeWorldPalms_.resize(n);

        for (size_t idx = 0; idx < n; ++idx) {
            float px = rawWorldPalms[idx].first;
            float py = rawWorldPalms[idx].second;
            bool startled = false;

            if (idx < palmAnchors_.size()) {
                float ax = palmAnchors_[idx].first;
                float ay = palmAnchors_[idx].second;
                float drift = std::hypot(px - ax, py - ay);
                float prevT = (idx < palmSteadyTimers_.size()) ? palmSteadyTimers_[idx] : 0.0f;

                if (drift <= PERCH_STEADY_RADIUS_M) {
                    newAnchors[idx] = {std::lerp(ax, px, dt * 1.5f), std::lerp(ay, py, dt * 1.5f)};
                    newTimers[idx] = prevT + dt;
                } else if (drift >= PERCH_WAKE_DRIFT_M) {
                    startled = true;
                    newAnchors[idx] = {px, py};
                    newTimers[idx] = 0.0f;
                } else {
                    newAnchors[idx] = {std::lerp(ax, px, 0.35f), std::lerp(ay, py, 0.35f)};
                    newTimers[idx] = std::max(0.0f, prevT - dt * 0.5f);
                }
            } else {
                newAnchors[idx] = {px, py};
                newTimers[idx] = 0.0f;
            }

            activeWorldPalms_[idx] = {
                .x = px,
                .y = py,
                .startled = startled,
                .is_steady = (newTimers[idx] >= PERCH_HOLD_DELAY_SEC)
            };
        }

        palmAnchors_ = std::move(newAnchors);
        palmSteadyTimers_ = std::move(newTimers);
    }

    void step(
        float rawDt,
        std::span<const std::pair<float, float>> worldPalms = {})
    {
        constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
        float dt = std::clamp(rawDt, 0.001f, 0.033f);
        simTime += dt;
        if (simTime > 86400.0f) simTime -= 86400.0f; // Prevent 32-bit float precision loss over multi-day uptime

        updatePalmSteadiness(dt, worldPalms);
        int numPalms = int(activeWorldPalms_.size());

        // Count currently perched butterflies per hand and wake startled ones
        for (int& perchedCount : perchedCounts_) perchedCount = 0;
        for (int i = 0; i < count; ++i) {
            AgentState& a = agents[i];
            if (a.state == STATE_PERCHED) {
                int hIdx = a.perch_hand_idx;
                if (hIdx < 0 || hIdx >= numPalms || activeWorldPalms_[hIdx].startled) {
                    // Startle back into gentle upward 3D flutter!
                    a.state = STATE_FLAPPING;
                    a.state_timer = randRange(a.rng, 1.0f, 2.2f);
                    a.perch_hand_idx = -1;
                    float burstAng = randRange(a.rng, 0.45f, 2.69f); // Upward fan (+Y in world space)
                    a.vel = {
                        std::cos(burstAng) * 0.95f,
                        std::sin(burstAng) * 0.95f,
                        randRange(a.rng, -0.4f, 0.2f)
                    };
                } else {
                    perchedCounts_[hIdx]++;
                }
            }
        }

        // Invite nearby orbiting butterflies onto steady hands (golden-angle spiral slots)
        for (int p = 0; p < numPalms; ++p) {
            if (!activeWorldPalms_[p].is_steady || perchedCounts_[p] >= MAX_PERCHED_PER_HAND) continue;
            float px = activeWorldPalms_[p].x;
            float py = activeWorldPalms_[p].y;
            for (int i = 0; i < count && perchedCounts_[p] < MAX_PERCHED_PER_HAND; ++i) {
                AgentState& a = agents[i];
                if (a.state == STATE_PERCHED) continue;
                float d2D = std::hypot(a.pos.x - px, a.pos.y - py);
                if (d2D < 0.90f && rand01(a.rng) < dt * 3.5f) {
                    int slot = perchedCounts_[p]++;
                    a.state = STATE_PERCHED;
                    a.state_timer = randRange(a.rng, 0.0f, 3.0f);
                    a.perch_hand_idx = p;
                    // Golden-angle spiral around palm/fingers so perched butterflies never stack!
                    const auto& polar = kGoldenPerchTable[slot];
                    float goldenAng = polar.base_angle + a.noise_seed * 0.2f;
                    a.perch_offset_x = std::cos(goldenAng) * polar.radius;
                    a.perch_offset_y = std::sin(goldenAng) * polar.radius * 0.88f + 0.015f;
                }
            }
        }

        // 1. Rebuild flat O(N) spatial hash grid on CPU (zero heap allocations)
        std::memset(cellHead, -1, sizeof(cellHead));
        for (int i = 0; i < count; ++i) {
            const Vector3& p = agents[i].pos;
            int cx = std::clamp(int((p.x - GRID_MIN_X) * INV_CELL_SIZE), 0, GRID_X - 1);
            int cy = std::clamp(int((p.y - GRID_MIN_Y) * INV_CELL_SIZE), 0, GRID_Y - 1);
            int cz = std::clamp(int((p.z - GRID_MIN_Z) * INV_CELL_SIZE), 0, GRID_Z - 1);
            nextAgent[i] = cellHead[cz][cy][cx];
            cellHead[cz][cy][cx] = i;
        }

        // 2. Multi-core OpenMP simulation update on 3 cores (leaving 4th Cortex-A72 core free for async TFLite MoveNet)
        #pragma omp parallel for schedule(static) num_threads(3)
        for (int i = 0; i < count; ++i) {
            AgentState a = agents[i];

            // =================================================================
            // STATE_PERCHED: Flutter onto outstretched hand & slowly breathe wings
            // =================================================================
            if (a.state == STATE_PERCHED && a.perch_hand_idx >= 0 && a.perch_hand_idx < numPalms) {
                const WorldPalm& wp = activeWorldPalms_[a.perch_hand_idx];
                Vector3 targetPos = {
                    wp.x + a.perch_offset_x,
                    wp.y + a.perch_offset_y,
                    0.85f // Foreground depth so basking butterflies look lush & detailed
                };
                Vector3 rem = targetPos - a.pos;
                float remDist = Vector3Length(rem);

                if (remDist > 0.028f) {
                    float appSpd = std::min(remDist * 1.8f + 0.15f, 0.90f);
                    a.vel = rem * (appSpd / remDist);
                    a.pos += a.vel * dt;
                    a.phase += a.flap_freq * kTwoPi * dt;
                    float s01 = 0.5f + 0.5f * std::sin(a.phase);
                    a.wing_angle = 0.36f + s01 * 1.04f;
                    a.wing_flex  = -std::cos(a.phase) * 0.25f;

                    Vector3 desFwd = safeNormalize({rem.x, rem.y, rem.z * 0.3f}, {0.0f, 1.0f, 0.0f});
                    float bBlend = std::min(1.0f, dt * 6.0f);
                    a.forward = safeNormalize(Vector3Lerp(a.forward, desFwd, bBlend));
                    a.up      = safeNormalize(Vector3Lerp(a.up, {0.0f, 0.0f, 1.0f}, bBlend));
                } else {
                    // Settled on palm: completely still position, basking upright facing viewer!
                    a.pos = targetPos;
                    a.vel = Vector3Zero();
                    float tilt = 0.35f * std::sin(a.noise_seed * 1.7f);
                    Vector3 baskFwd = safeNormalize({std::sin(tilt), std::cos(tilt), 0.12f});
                    Vector3 baskUp  = {0.0f, 0.0f, 1.0f}; // Dorsal wings open toward projector wall viewer
                    float bBlend = std::min(1.0f, dt * 6.0f);
                    a.forward = safeNormalize(Vector3Lerp(a.forward, baskFwd, bBlend));
                    a.up      = safeNormalize(Vector3Lerp(a.up,      baskUp,  bBlend));

                    // Basking breath: rest in a ~20-deg not-quite-flat V, slow graceful fold to ~75-deg every ~4s
                    a.state_timer += dt;
                    float breath = std::sin(a.state_timer * 1.5f + a.noise_seed);
                    float targetWingAngle = (breath > -0.35f)
                        ? 0.35f
                        : (0.35f + ((-breath - 0.35f) / 0.65f) * 0.95f);
                    float wBlend = std::min(1.0f, dt * 3.5f);
                    a.wing_angle = std::lerp(a.wing_angle, targetWingAngle, wBlend);
                    a.wing_flex  = std::lerp(a.wing_flex,  -0.04f,          wBlend);
                }
            } else {
                // =============================================================
                // STATE_FLAPPING & STATE_GLIDING: Independent Meandering + Gentle Hand Orbit
                // =============================================================
                if (a.state == STATE_PERCHED) {
                    a.state = STATE_FLAPPING;
                    a.perch_hand_idx = -1;
                }

                int cx = std::clamp(int((a.pos.x - GRID_MIN_X) * INV_CELL_SIZE), 0, GRID_X - 1);
                int cy = std::clamp(int((a.pos.y - GRID_MIN_Y) * INV_CELL_SIZE), 0, GRID_Y - 1);
                int cz = std::clamp(int((a.pos.z - GRID_MIN_Z) * INV_CELL_SIZE), 0, GRID_Z - 1);

                Vector3 sepForce = Vector3Zero();
                constexpr float sepRadius = 0.62f;
                constexpr float sepRadiusSq = sepRadius * sepRadius;

                int zMin = std::max(0, cz - 1), zMax = std::min(GRID_Z - 1, cz + 1);
                int yMin = std::max(0, cy - 1), yMax = std::min(GRID_Y - 1, cy + 1);
                int xMin = std::max(0, cx - 1), xMax = std::min(GRID_X - 1, cx + 1);

                int neighborsChecked = 0;
                for (int nz = zMin; nz <= zMax; ++nz) {
                    for (int ny = yMin; ny <= yMax; ++ny) {
                        for (int nx = xMin; nx <= xMax; ++nx) {
                            int j = cellHead[nz][ny][nx];
                            while (j != -1 && neighborsChecked < 16) {
                                if (j != i && agents[j].state != STATE_PERCHED) {
                                    Vector3 diff = a.pos - agents[j].pos;
                                    float d2 = Vector3LengthSqr(diff);
                                    if (d2 > 1e-6f && d2 < sepRadiusSq) {
                                        float invD = 1.0f / std::sqrt(d2);
                                        float push = (sepRadius - (1.0f / invD)) / sepRadius;
                                        sepForce += diff * (invD * push);
                                    }
                                    ++neighborsChecked;
                                }
                                j = nextAgent[j];
                            }
                        }
                    }
                }

                Vector3 currentForce = Vector3Zero();
                float excitement = 1.0f;

                // Engage gentle hand orbit for nearby butterflies or ~20% cohort
                int engagedPalm = -1;
                if (numPalms > 0) {
                    int cohortPalm = (i / 4) % numPalms;
                    float bestDist = 999.0f;
                    int nearestPalm = cohortPalm;
                    for (int p = 0; p < numPalms; ++p) {
                        float d = std::hypot(a.pos.x - activeWorldPalms_[p].x, a.pos.y - activeWorldPalms_[p].y);
                        if (d < bestDist) {
                            bestDist = d;
                            nearestPalm = p;
                        }
                    }
                    if (bestDist < 1.25f || ((i / 4) % 10) < 2) {
                        engagedPalm = (bestDist < 1.60f) ? nearestPalm : cohortPalm;
                    }
                }

                if (engagedPalm >= 0) {
                    const WorldPalm& wp = activeWorldPalms_[engagedPalm];

                    if (rand01(a.rng) < dt * 0.30f) {
                        a.orbit_dir = -a.orbit_dir;
                    }

                    float offX = 0.24f * std::cos(simTime * 0.5f + a.noise_seed * 2.7f);
                    float offY = 0.20f * std::sin(simTime * 0.65f + a.noise_seed * 1.9f);
                    float cdx = a.pos.x - (wp.x + offX);
                    float cdy = a.pos.y - (wp.y + offY);
                    float cDist = std::max(0.02f, std::hypot(cdx, cdy));
                    float nx = cdx / cDist;
                    float ny = cdy / cDist;
                    float tx = -ny * float(a.orbit_dir);
                    float ty =  nx * float(a.orbit_dir);

                    float baseR = wp.is_steady ? (a.preferred_orbit_m + 0.45f) : (a.preferred_orbit_m + 0.20f);
                    float waveR = 0.22f * std::sin(simTime * 0.8f + a.noise_seed)
                                + 0.14f * std::cos(simTime * 1.3f + a.noise_seed * 2.1f);
                    float targetOrbit = std::max(wp.is_steady ? 0.55f : 0.32f, baseR + waveR);

                    float radialPull = (targetOrbit - cDist) * 0.65f;
                    float rawDist = std::hypot(a.pos.x - wp.x, a.pos.y - wp.y);
                    if (wp.is_steady && rawDist < 0.52f) {
                        radialPull += (0.52f - rawDist) * 1.8f;
                    }

                    float swirlSpd = wp.is_steady ? 0.52f : 0.72f;
                    float targetZ = -0.4f + 0.7f * std::sin(simTime * 0.5f + a.noise_seed);
                    float zPull = (targetZ - a.pos.z) * 0.7f;

                    Vector3 desVel = {
                        tx * swirlSpd + nx * radialPull,
                        ty * swirlSpd + ny * radialPull,
                        zPull
                    };
                    currentForce = (desVel - a.vel) * 1.10f;
                    excitement = wp.is_steady ? 0.92f : 1.05f;
                }

                // Soft boundary return force keeping butterflies inside the 1080p orthographic wall
                Vector3 boundForce = Vector3Zero();
                if (a.pos.x >  3.50f) boundForce.x -= (a.pos.x - 3.50f) * 1.8f;
                if (a.pos.x < -3.50f) boundForce.x -= (a.pos.x + 3.50f) * 1.8f;
                if (a.pos.y >  1.85f) boundForce.y -= (a.pos.y - 1.85f) * 2.0f;
                if (a.pos.y < -1.85f) boundForce.y -= (a.pos.y + 1.85f) * 2.0f;
                if (a.pos.z >  1.20f) boundForce.z -= (a.pos.z - 1.20f) * 1.8f;
                if (a.pos.z < -4.40f) boundForce.z -= (a.pos.z + 4.40f) * 1.4f;

                // Per-butterfly slowly wandering personal meadow spot across the interior of the screen
                // so mutual separation never pushes the population out to the perimeter walls
                Vector3 meadowPull = Vector3Zero();
                if (engagedPalm < 0) {
                    auto fi = float(i);
                    Vector3 meadowHome = {
                        3.05f * std::sin(simTime * 0.21f + a.noise_seed + fi * 2.39996f),
                        1.58f * std::cos(simTime * 0.27f + a.noise_seed * 1.3f + fi * std::numbers::phi_v<float>),
                        -1.60f + 1.40f * std::sin(simTime * 0.19f + a.noise_seed * 0.7f + fi)
                    };
                    meadowPull = (meadowHome - a.pos) * 0.45f;
                }

                Vector3 turb = evalCurlTurbulence(a.pos, simTime, a.noise_seed);
                Vector3 accel = sepForce * 1.15f + currentForce + meadowPull + boundForce + turb * 0.75f;

                // Flap-and-Glide State Machine
                bool soaringSpecies = (a.species_id == 0 || a.species_id == 1 || a.species_id == 2 || a.species_id == 6);

                // If a gliding butterfly reaches the screen boundary and needs to turn back,
                // it must resume flapping first (turning is only possible while flapping).
                if (a.state == STATE_GLIDING && Vector3LengthSqr(boundForce) > 0.01f) {
                    a.state = STATE_FLAPPING;
                    a.phase = 0.36f;
                    a.state_timer = randRange(a.rng, 0.90f, 1.60f);
                }

                a.state_timer -= dt;
                if (a.state == STATE_GLIDING) {
                    if (a.state_timer <= 0.0f) {
                        a.state = STATE_FLAPPING;
                        // Start stroke smoothly from the ~54-deg (0.94 rad) folded glide angle so wings never snap
                        a.phase = 0.36f;
                        a.state_timer = soaringSpecies
                            ? randRange(a.rng, 0.95f, 1.65f)  // 2-4 slow, languid wingbeats
                            : randRange(a.rng, 0.85f, 1.55f); // 3-6 livelier flitting beats
                    }
                } else {
                    // Transition into a straight-line downward glide seamlessly as wings pass the ~54-deg folded V angle
                    if (a.state_timer <= 0.0f && std::sin(a.phase) > 0.15f && Vector3LengthSqr(boundForce) <= 0.01f) {
                        float glideProb = soaringSpecies ? 0.85f : 0.55f;
                        if (rand01(a.rng) < glideProb) {
                            a.state = STATE_GLIDING;
                            a.state_timer = soaringSpecies
                                ? randRange(a.rng, 1.50f, 3.20f)  // Long, dreamy float-and-glide
                                : randRange(a.rng, 0.65f, 1.40f); // Shorter glide for smaller flitting species

                            // Lock into a straight-line downward-drifting glide vector toward bottom of screen
                            float horizSpd = std::hypot(a.vel.x, a.vel.z);
                            float targetHoriz = std::clamp(horizSpd, 0.24f, soaringSpecies ? 0.44f : 0.52f);
                            float hx = (horizSpd > 1e-4f) ? (a.vel.x / horizSpd) : ((a.forward.x >= 0.0f) ? 1.0f : -1.0f);
                            float hz = (horizSpd > 1e-4f) ? (a.vel.z / horizSpd) : 0.0f;
                            a.vel.x = hx * targetHoriz;
                            a.vel.z = hz * targetHoriz;
                            a.vel.y = soaringSpecies ? -0.17f : -0.20f;
                        } else {
                            a.state_timer = randRange(a.rng, 0.80f, 1.50f);
                        }
                    }
                }

                float surge = 1.0f;
                if (a.state == STATE_FLAPPING) {
                    a.phase += a.flap_freq * excitement * kTwoPi * dt;
                    if (a.phase > 100.0f * kTwoPi) a.phase -= 100.0f * kTwoPi;

                    // Lingering open-V stroke curve: wings spend slightly longer near the open +21 deg V
                    // and fold up softly to +80 deg above the back
                    float raw01 = 0.5f + 0.5f * std::sin(a.phase);
                    float s01 = raw01 * std::sqrt(raw01); // pow(raw01, 1.5) without std::pow overhead
                    float targetAngle = 0.36f + s01 * 1.04f;
                    // Strong 90-deg quadrature wave (-cos) drives forewing-to-hindwing trailing cloth lag & breaststroke pitch
                    float downstrokePhase = -std::cos(a.phase); // +1 on power downstroke, -1 on recovery upstroke
                    float targetFlex  = downstrokePhase * 0.48f;
                    float blend = std::min(1.0f, dt * 16.0f);
                    a.wing_angle = std::lerp(a.wing_angle, targetAngle, blend);
                    a.wing_flex  = std::lerp(a.wing_flex,  targetFlex,  blend);

                    // Upward climb bias while flapping (duty-cycle balanced against downward glide sink)
                    // plus breaststroke lift & forward surge on power downstroke
                    float climbBias = soaringSpecies ? 0.52f : 0.24f;
                    accel.y += climbBias + downstrokePhase * 0.42f;
                    surge = 1.0f + 0.36f * downstrokePhase;

                    // Turning and speed changes only occur while actively flapping
                    a.vel += accel * dt;
                    a.vel.y = std::clamp(a.vel.y, soaringSpecies ? -0.10f : -0.20f, 0.52f);
                    float spdSq = Vector3LengthSqr(a.vel);
                    float minSpd = soaringSpecies ? 0.28f : 0.36f;
                    float maxSpd = soaringSpecies ? 0.58f : 0.68f;
                    if (spdSq < minSpd * minSpd) {
                        a.vel = safeNormalize(a.vel) * minSpd;
                    } else if (spdSq > maxSpd * maxSpd) {
                        a.vel = safeNormalize(a.vel) * maxSpd;
                    }

                    // Smoothly align body heading and bank angle while flapping
                    Vector3 desFwd = safeNormalize({a.vel.x, a.vel.y * 0.85f, a.vel.z * 0.45f}, a.forward);
                    float fBlend = std::min(1.0f, dt * (soaringSpecies ? 6.5f : 9.0f));
                    a.forward = safeNormalize(Vector3Lerp(a.forward, desFwd, fBlend));

                    // True 3D roll around the forward flight axis during flaps:
                    // - baseRoll wanders per butterfly across negative (near wing wide) and positive (far wing wide) angles
                    // - turnRoll banks into depth/horizontal turns
                    // - flapRock adds wingbeat roll oscillation so the body rocks gently with each stroke
                    float baseRoll = 0.16f + 0.48f * std::sin(simTime * 0.72f + a.noise_seed * 1.9f);
                    float turnRoll = std::clamp(accel.z * 0.55f + accel.x * 0.25f, -0.48f, 0.48f);
                    float flapRock = 0.24f * std::sin(a.phase + a.noise_seed);
                    float flapRoll = std::clamp(baseRoll + turnRoll + flapRock, -0.62f, 0.85f);

                    Vector3 targetUp = computeRolledUp(a.forward, flapRoll);
                    float upBlend = std::min(1.0f, dt * 6.5f);
                    a.up = safeNormalize(Vector3Lerp(a.up, targetUp, upBlend), targetUp);
                } else {
                    float damp = std::min(1.0f, dt * 8.0f);
                    // Glide in a more folded ~54-deg upward V-dihedral (0.94 rad) so gliding butterflies read as
                    // sleek 3D V-profiles traveling forward rather than wide-open kites sliding sideways
                    a.wing_angle = std::lerp(a.wing_angle, 0.94f,  damp);
                    a.wing_flex  = std::lerp(a.wing_flex, -0.04f, damp);
                    surge = 0.95f;

                    // Straight-line downward glide: keep constant straight-line velocity vector with downward sink
                    if (a.vel.y > -0.14f) {
                        a.vel.y = soaringSpecies ? -0.17f : -0.20f;
                    }
                    float spd = std::sqrt(Vector3LengthSqr(a.vel));
                    float minGlide = soaringSpecies ? 0.26f : 0.32f;
                    float maxGlide = soaringSpecies ? 0.50f : 0.58f;
                    float glideSpd = std::clamp(spd * (1.0f - 0.12f * dt), minGlide, maxGlide);
                    a.vel = safeNormalize(a.vel, a.forward) * glideSpd;

                    // Point body along the constant straight-line downward glide direction
                    Vector3 glideFwd = safeNormalize({a.vel.x, a.vel.y * 0.85f, a.vel.z * 0.45f}, a.forward);
                    a.forward = safeNormalize(Vector3Lerp(a.forward, glideFwd, std::min(1.0f, dt * 8.0f)));

                    // True 3D drift-roll around the forward axis during glides:
                    // Sweeps across [-0.58, +0.85] rad (-33 deg to +49 deg) so gliding butterflies dynamically
                    // roll between banking toward the viewer (far wing wide), balanced 3/4 V-glides (both wings open),
                    // and banking away (near wing wide), rather than sitting locked at one camera angle.
                    float depthBank = a.vel.z * 0.85f;
                    float thermalDrift = 0.15f
                        + 0.54f * std::sin(simTime * 1.05f + a.noise_seed * 2.3f)
                        + 0.26f * std::cos(simTime * 1.75f + a.noise_seed * 0.9f);
                    float glideRoll = std::clamp(depthBank + thermalDrift, -0.58f, 0.85f);

                    Vector3 glideUp = computeRolledUp(a.forward, glideRoll);
                    float upBlend = std::min(1.0f, dt * 5.0f);
                    a.up = safeNormalize(Vector3Lerp(a.up, glideUp, upBlend), glideUp);
                }

                a.pos += a.vel * (dt * surge);
            }

            nextAgents[i] = a;
            writeInstance(i, a);
        }

        std::memcpy(agents, nextAgents, count * sizeof(AgentState));

        // Step wingtip fairy-dust / firefly motes
        for (int m = 0; m < MAX_FAIRY_MOTES; ++m) {
            FairyMote& mote = motes[m];
            mote.age += dt;
            if (mote.age >= mote.max_age) {
                int bIdx = (m * 7 + int(simTime * 17.0f)) % count;
                spawnMoteOnButterfly(m, bIdx, false);
            } else {
                float swirlX = 0.08f * std::sin(simTime * 1.4f + mote.phase);
                float swirlY = 0.06f * std::cos(simTime * 1.1f + mote.phase * 1.3f);
                mote.pos.x += (mote.vel.x + swirlX) * dt;
                mote.pos.y += (mote.vel.y + swirlY) * dt;
                mote.pos.z += mote.vel.z * dt;
                mote.vel *= (1.0f - 0.65f * dt);
            }
        }
    }
};

} // namespace bf
