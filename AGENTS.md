# Implementation Spec: 1080p60 3D Butterfly Kaleidoscope on Raspberry Pi 4B

## Role & Mission for Implementing Agent
You are maintaining a standalone **C++26 / OpenGL ES 3.0** real-time 3D visualization ("A Kaleidoscope of Butterflies") on a **Raspberry Pi 4B** driving a **1920x1080 @ 60Hz HD projector** with a **pure black background (`#000000`)**.

Follow these architectural rules strictly. Do **not** invent alternative frameworks, do **not** use X11/Wayland/Python, and **never** re-introduce any of the explicitly prohibited legacy features listed below.

---

## Explicitly Prohibited Legacy Features (Do NOT Re-Introduce!)
1. **NO manual steady-hand / bullseye corner calibration (Tier 2):** Startup calibration must **never** wait for a human to hold their hand on corners or draw bullseye progress rings.
2. **NO 30-second spinning-circle camera wait at boot:** If no USB webcam is open within 2.5 seconds, proceed immediately with the default `1920x1080` homography.
3. **NO flashing white-dot corner calibration:** Always use the **4 standard `tag36h11` AprilTag fiducial tiles (`IDs 0, 1, 2, 3`)** via `libapriltag`.
4. **NO `calibration.json` file save/load:** Calibrate fresh at startup; never read or write calibration files on disk.
5. **NO procedural wing texture generator or runtime disk asset loading:** All 8 museum butterfly PNGs, both GLSL ES 3.00 shaders, and the quantized MoveNet `.tflite` model are embedded directly into `.rodata` at compile time via `BF_EMBED_BINARY`.
6. **NO physics head repulsion or hand-inside-head suppression:** Eye safety is handled **solely** by drawing a solid `#000000` black circle over the detected head last before frame swap.
7. **NO head holdover timer:** When MoveNet no longer detects a person, clear `has_head` and `palms` immediately.
8. **NO keyboard `'F'` HUD toggle or hand progress rings:** Never draw FPS text or hand steadiness rings onto the projector.
9. **NO `--perspective`, `--ortho`, or `--msaa` CLI flags:** Always render in 1080p orthographic projection without MSAA.

---

## Pre-Answered FAQ (Read Before Modifying Any Code)

1. **Q: What language standard, windowing backend, and graphics API are used?**
   * **Answer:** **C++26 (`-std=c++26`) + Raylib compiled with `PLATFORM=PLATFORM_DRM` and `GRAPHICS=GRAPHICS_API_OPENGL_ES3`**, calling raw **OpenGL ES 3.0 (`<GLES3/gl3.h>`)** directly for the instanced butterfly pass and 2D overlay pass.
   * **Why:** Raylib `PLATFORM_DRM` handles KMS/DRM/GBM/EGL context setup and hardware VSync page-flipping cleanly without X11/Wayland compositor overhead, while raw OpenGL ES 3.0 lets us issue a single instanced draw call (`glDrawElementsInstanced`).

2. **Q: Should I increase `gpu_mem` in `/boot/firmware/config.txt`?**
   * **Answer:** **NO.** Leave `gpu_mem` untouched (default 64MB/76MB) or unset.
   * **Why:** On the Pi 4B, `gpu_mem` only allocates memory to legacy VideoCore firmware. The modern `v3d` 3D driver allocates textures and buffers from Linux **CMA (Contiguous Memory Allocator)** in system RAM.

3. **Q: Where do butterfly wing textures, shaders, and the MoveNet model come from?**
   * **Answer:** They are embedded directly into the executable binary's `.rodata` section at compile time via `BF_EMBED_BINARY` (`src/cpp26.hpp`):
     * `assets/species_0.png` .. `assets/species_7.png` (`512x512` RGBA8 real museum specimens)
     * `shaders/butterfly.vert` & `shaders/butterfly.frag` (`#version 300 es`)
     * `assets/movenet_lightning_int8.tflite` (`192x192` `kTfLiteUInt8` MoveNet SinglePose Lightning)
   * **Why:** Guarantees a self-contained single binary with zero runtime disk I/O or missing-file failures.

4. **Q: Should I use `GL_BLEND` (alpha blending) or MSAA for the butterflies?**
   * **Answer:** **NO.** Never enable `GL_BLEND` or `FLAG_MSAA_4X_HINT` for the butterflies. Use **alpha cutout (`if (tex.a < 0.08) discard;`)** combined with a velvety solid black floor (`velvetBlack`) so dark inner wing margins and veins remain opaque against the `#000000` background.
   * **Why:** VideoCore VI is a **Tile-Based Deferred Renderer (TBDR)** with hardware Hidden Surface Removal (HSR). `GL_BLEND` disables HSR and forces CPU back-to-front sorting, while MSAA wastes tile resolve bandwidth.

5. **Q: Should wing flapping be computed in the Vertex Shader or on the CPU?**
   * **Answer:** Compute `wing_angle` and `wing_flex` **on the CPU** inside the per-butterfly OpenMP update loop, and pass those two floats in the per-instance VBO.
   * **Why:** Real butterflies alternate between **burst flapping**, **gliding** (holding wings in a shallow 'V' dihedral), and **perching** on steady hands. Integrating phase (`phase += flap_freq * 2π * dt`) on the CPU prevents phase-jump snapping when transitioning between states.

6. **Q: How many butterflies run by default?**
   * **Answer:** Default to **`75` butterflies** at **2x hero scale** (`--count 75` CLI flag, overrideable from `10` to `3000`). At `52` bytes per `ButterflyInstance`, 75 butterflies is only **3.9 KB/frame** uploaded via `glBufferSubData`.

---

## Project Directory Layout

Located in `/home/benjamin/butterfly/butterfly_vis/`:

```text
/home/benjamin/butterfly/butterfly_vis/
├── Makefile                 # Builds butterfly_vis (-std=c++26 -O3 -mcpu=cortex-a72 -fopenmp)
├── butterfly.service        # Systemd unit (--count 75 --wait-hardware)
├── assets/                  # Source assets embedded into .rodata at compile time
│   ├── movenet_lightning_int8.tflite
│   └── species_0.png .. species_7.png
├── shaders/                 # GLSL ES 3.00 shaders embedded into .rodata at compile time
│   ├── butterfly.vert       # #version 300 es instanced orthographic vertex shader
│   └── butterfly.frag       # #version 300 es two-sided SSS + iridescence fragment shader
└── src/
    ├── main.cpp             # DRM/GBM init, CLI flags, render loop, head blackout, telemetry
    ├── cpp26.hpp            # Compile-time .rodata binary asset embedding macro (BF_EMBED_BINARY)
    ├── mesh.hpp             # 26-vertex, 20-triangle silhouette-trimmed 4-wing butterfly mesh
    ├── textures.hpp         # Embedded museum PNG loader -> 512x512x8 GL_TEXTURE_2D_ARRAY
    ├── sim.hpp              # 3-core OpenMP flocking, curl turbulence, gentle hand swirl & perching
    ├── vision.hpp           # /dev/v4l/by-id USB V4L2 camera worker + embedded TFLite MoveNet INT8
    ├── calibration.hpp      # Non-blocking 4-corner AprilTag (tag36h11) + V4L2 hardware zoom calibrator
    └── overlay_gl.hpp       # 2D GLES3 renderer for #000000 head blackout circle & AprilTag tiles
```

---

## Hardware & Architecture Specifications

### 1. 4-Wing Articulated 3D Butterfly Mesh (`src/mesh.hpp`)
* **26 vertices, 20 triangles** (`kButterflyVerts[26]`, `kButterflyIndices[60]`), marked `inline constexpr`.
* Splits each side at the natural `V = 0.50` forewing/hindwing cleft into independent **Forewings** (`side = ±1.0f`, leading the stroke) and **Hindwings** (`side = ±2.0f`, trailing behind in phase like silk cloth), each with inner and outer spanwise hinge segments at `|x| = 0.38`.
* Center body strip uses `side = 0.0f` so the thorax/abdomen never folds.
* Counter-clockwise (CCW) dorsal winding around `+Y`.

### 2. Per-Instance VBO Layout (`52 Bytes` Packed) (`src/sim.hpp` & `src/main.cpp`)
```cpp
struct ButterflyInstance {
    float pos[3];         // Location 3 (vec3): World position (meters)
    float forward[3];     // Location 4 (vec3): Normalized heading vector
    float up[3];          // Location 5 (vec3): Normalized banked up vector
    float wing_angle;     // Location 6 (float): Current main flap angle in radians
    float wing_flex;      // Location 7 (float): Wingtip lag/bend factor
    float scale;          // Location 8 (float): World wingspan scale
    uint32_t species_id;  // Location 9 (uint):  Layer index [0..7] in GL_TEXTURE_2D_ARRAY
};
static_assert(sizeof(ButterflyInstance) == 52, "ButterflyInstance must be tightly packed to 52 bytes (13 x 4B)");
```
* **Critical OpenGL ES 3.0 Rule:** Attributes `3..8` bind with `glVertexAttribPointer(..., GL_FLOAT, ...)`. Attribute `9` (`species_id`) **MUST** bind with **`glVertexAttribIPointer(9, 1, GL_UNSIGNED_INT, sizeof(ButterflyInstance), ...)`** and `flat out uint v_species;` in GLSL ES 3.00.

### 3. `GL_TEXTURE_2D_ARRAY` Museum Specimens (`src/textures.hpp`)
* Allocates `glTexStorage3D(GL_TEXTURE_2D_ARRAY, 9, GL_RGBA8, 512, 512, 8)` and loads the 8 embedded museum specimen PNGs (`species_0_png` .. `species_7_png` from `.rodata`) via `LoadImageFromMemory`, flipping vertically so `V=1.0` is head/apex and `V=0.0` is tail, followed by `glGenerateMipmap(GL_TEXTURE_2D_ARRAY)`.

### 4. Orthographic 1080p Shaders (`shaders/butterfly.vert` & `shaders/butterfly.frag`)
* Line 1 of both shaders **MUST** be `#version 300 es`.
* `butterfly.vert` drives **4-wing biomechanical + cel-animated articulation**:
  * **Clap-and-Peel Dorsal Cupping:** At the top of the upstroke (`i_wing_angle > 0.80`), the stiff forewing leading edges peel apart first as the downstroke snaps while the hindwings and trailing margins stay momentarily cupped (`peelCup`).
  * **Seamless Cleft, C-Curve Hinge Whip & Bank Asymmetry:** Uses continuous distance behind the `z = 0.02` forewing/hindwing cleft (`hindTail`) so the hindwing trails in phase without ever tearing a gap at the seam, bends the outer wingtip (`|x| > 0.38`) into a scooped C-curve (`cCurveWhip`), and applies differential left/right wing amplitude when banking (`bankAsym = -wingSign * right.y * (0.10 + 0.06 * span)`).
  * **3D Pronation/Supination Twist & Slanted Figure-8 Sweep:** Rotates the wing chord `(wy, wz)` in 3D around the `z = 0.02` cleft axis (`feather`) and sweeps wingtips forward (`+Z`) on downstrokes and aft (`-Z`) on upstrokes (`sweepZ`).
  * **Thorax Pitch, Abdomen Counter-Swing & Delayed Body Bounce:** Couples wing strokes to breaststroke thorax pitch (`strokePitch = i_wing_flex * 0.44`), inertial abdomen counter-flexion (`abdomenCounter`), and vertical body pop (`local.y += (0.58 - i_wing_angle) * 0.19 + i_wing_flex * 0.08`).
  * Applies subtle Z-depth scaling (`depthScale = clamp(1.0 + (i_pos.z + 1.8) * 0.14, 0.58, 1.38)`) inside orthographic projection (`MatrixOrtho`) so distant butterflies appear smaller with zero perspective distortion at screen corners.
* `butterfly.frag` uses a constant orthographic view vector `const vec3 V = vec3(0.0, 0.0, 1.0);`, low-threshold alpha cutout (`if (tex.a < 0.08) discard;`) to preserve soft wing margins, a **velvety solid black floor (`velvetBlack`) + chitin specular sheen** so dark wing veins/interiors never look hollow against `#000000`, **dorsal vs. ventral "Morpho flash" contrast** (warmer matte ventral underside when clapped vs. full jewel saturation and `dorsalShimmer` iridescence when snapped open), and two-sided subsurface scattering.

### 5. USB V4L2 Camera Discovery & Async MoveNet Pose (`src/vision.hpp`)
* Enumerates `/dev/v4l/by-id` and `/dev/v4l/by-path` via `std::filesystem::directory_iterator`, filtering strictly for USB devices (`"usb"` in symlink filename AND `cap.bus_info` starting with `"usb-"`).
* Captures `640x480` (or `320x240`) `YUYV` via zero-copy `V4L2_MEMORY_MMAP`.
* Extracts full-resolution grayscale luminance (`Y`) for AprilTag detection and runs embedded TFLite MoveNet SinglePose Lightning INT8 (`kTfLiteUInt8`) on 1 background thread (`SetNumThreads(1)`).
* When `confidentCount < 3`, immediately clears `state_.has_head = false` and `state_.palms.clear()`.

### 6. Non-Blocking 4-Corner AprilTag (`tag36h11`) & Hardware Zoom Calibrator (`src/calibration.hpp`)
* Projects 4 official `tag36h11` AprilTag fiducial tiles (`IDs 0:TL, 1:TR, 2:BL, 3:BR`, `264x264` px at 1080p) at the 4 corners of the projector screen.
* Resets camera hardware zoom (`V4L2_CID_ZOOM_ABSOLUTE`) to `minHwZoom()` (widest angle) before detection.
* If all 4 tags are detected at widest zoom and the camera supports `V4L2_CID_ZOOM_ABSOLUTE`, steps zoom inward while all 4 tags remain inside `ZOOM_SAFE_BORDER_NORM` (`0.045`), and backs up one step as soon as any tag clips or touches the border.
* Computes the 3x3 perspective homography `Homography3x3` mapping normalized camera `(u, v)` to projector pixels `(x, y)` via 8x8 partial-pivoting Gaussian elimination (`computeHomographyFromTagCenters`).
* Hard-capped at `MAX_TOTAL_CALIB_SEC = 15.0` seconds (`WIDE_DETECT_BUDGET_SEC = 5.0` seconds at widest zoom) so startup calibration can **never** block or hang.

### 7. Multi-Core Simulation, Flap-and-Glide Kinematics, 3D Roll, Perching & Eye Safety (`src/sim.hpp` & `src/main.cpp`)
* Default `75` hero-scale (2x) butterflies updated across 3 Cortex-A72 cores (`#pragma omp parallel for schedule(static) num_threads(3)`), leaving the 4th core free for async TFLite vision.
* Flat `O(N)` spatial hash grid (`16 x 12 x 16 = 3,072` cells) with zero per-frame heap allocations.
* **Independent Meandering & Meadow Home (`meadowPull`):** Uses `evalCurlTurbulence` + soft personal space + a per-butterfly wandering interior meadow home so mutual separation never pushes the population out to the perimeter walls.
* **Flap-and-Glide Kinematics:**
  * Turning, climbing, and speed changes occur **only while actively flapping** (`STATE_FLAPPING`), using an asymmetric cel-animator **"Snap-and-Hold"** waveform (`skewPhase = a.phase - 0.32f * std::sin(a.phase)`): a fast ~34% downstroke snap into a cushioned wide-open pose hold at **`+12°` (`0.20 rad`)** (so open wing patterns flash clearly on every beat without flipping edge-on) followed by a feathered recovery upstroke up to **`+80°` (`1.40 rad`)** dorsal clap, coupled with a quadrature C-curve whip (`downstrokePhase`) and a ~35°-delayed body counter-bounce (`delayedBounce = -std::cos(a.phase - 0.58f)`).
  * Glides (`STATE_GLIDING`) lock into a **straight-line downward-drifting velocity vector** (resuming flapping first if boundary or personal-space separation forces require turning) with wings held in a **species-specific upward 'V' dihedral (`kSpeciesGlideRad`: `+45°..+49°` for large soarers, `+55°..+60°` for smaller darters)** so gliding butterflies read as sleek 3D V-profiles traveling forward rather than wide-open kites sliding sideways.
* **True 3D Roll Around the Flight Axis (`computeRolledUp`):**
  * Projects world sky (`+Y`) and camera (`+Z`) perpendicular to `a.forward` (`skyPerp`, `camPerp`) and rotates `a.up` in that perpendicular roll plane (avoiding the cross-product cancellation of components parallel to `a.forward`).
  * Sweeps roll across **`-33°` to `+49°` (`-0.58` to `+0.85 rad`)** driven by turn banking, wingbeat roll rock (`flapRock`), depth drift (`a.vel.z`), and thermal drift waves—so butterflies naturally transition between banking toward the viewer (far wing wide), balanced 3/4 V-glides (both wings open), and banking away (near wing wide).
* Rejects any detected head or palm whose homography-mapped screen coordinates fall outside `[0..renderW, 0..renderH]`.
* Raised palms inside the projection area attract nearby butterflies and a ~20% cohort (`((i / 4) % 10) < 2`) into a gentle half-speed orbit (`swirlSpd = 0.52f .. 0.72f`).
* Holding a hand steady for `0.80s` invites up to `5` butterflies per hand to land in a `consteval` golden-angle spiral (`kGoldenPerchTable`) and slowly breathe their wings; moving the hand (`>= 0.20m`) startles perched butterflies upward back into 3D flight.
* **Eye Safety:** If a head is detected inside `[0..renderW, 0..renderH]`, `main.cpp` draws a solid `#000000` black circle (`overlay.drawSolidCircle`) over the head **last**, immediately before `EndDrawing()` / `swap()`.

### 8. CLI Flags & Systemd Service (`src/main.cpp` & `butterfly.service`)
* Supported CLI flags:
  * `--count N` (default `75`, clamped `10..3000`)
  * `--frames N` (run `N` frames then exit cleanly; `0` = infinite)
  * `--headless` (force offscreen `1920x1080` GBM/EGL context on `/dev/dri/renderD128`)
  * `--demo` (synthetic visitor head + 2 raised hands for testing without a camera)
  * `--wait-hardware` (poll `/sys/class/drm/card*-HDMI-A-*/status` every `500 ms` until a projector is connected)
  * `--screenshot PATH` (save final frame of a `--frames N` run to PNG)
* `butterfly.service` runs `/home/benjamin/butterfly/butterfly_vis/butterfly_vis --count 75 --wait-hardware`.
