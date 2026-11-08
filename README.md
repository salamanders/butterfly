# A Kaleidoscope of Butterflies (`butterfly_vis`)

Real-time **1080p @ 60 FPS** interactive 3D butterfly visualization in **C++26** and **OpenGL ES 3.0** for **Raspberry Pi 4B** driving an HD projector over direct DRM/KMS (`#000000` pure black background).

---

## Key Features

- **Zero-Compositor DRM/KMS & OpenGL ES 3.0 Pipeline:**
  - Runs directly on `/dev/dri/card*` via KMS/DRM/GBM/EGL (`PLATFORM_DRM` + `GRAPHICS_API_OPENGL_ES3`) with hardware VSync and single-call instanced rendering (`glDrawElementsInstanced`).
  - Zero runtime disk I/O: all 8 museum butterfly specimen textures (`512x512` RGBA8), GLSL ES 3.00 shaders, and the quantized MoveNet SinglePose Lightning INT8 `.tflite` model are embedded into `.rodata` at compile time via `BF_EMBED_BINARY`.
- **4-Wing Articulated Breaststroke & Flap-and-Glide Kinematics:**
  - **26-vertex, 20-triangle** silhouette-trimmed 3D mesh split at the natural forewing/hindwing cleft (`V = 0.50`) into leading forewings and phase-delayed trailing hindwings that ripple like silk cloth.
  - Butterflies actively flap to climb, turn, and accelerate (`STATE_FLAPPING`), then lock into sleek straight-line downward glides (`STATE_GLIDING`) holding a folded `+54°` (`0.94 rad`) upward 'V' dihedral.
  - **True 3D Flight-Axis Roll:** Butterflies dynamically roll around their forward flight axis (`-33°` to `+49°`) driven by turn banking, wingbeat rock, depth drift, and thermal waves.
  - **Two-Sided Subsurface Scattering & Iridescence:** Custom `#version 300 es` fragment shader with alpha cutout (`tex.a < 0.08`), velvety solid black floor (`velvetBlack`) + chitin specular sheen for dark inner veins, translucent backlight transmission, and structural view-angle iridescence.
- **Automated 4-Corner AprilTag Calibration & USB Camera Pose Tracking:**
  - Non-blocking startup calibration projects 4 `tag36h11` AprilTag fiducials (`IDs 0..3`), automatically steps USB webcam hardware zoom (`V4L2_CID_ZOOM_ABSOLUTE`), and solves the 3x3 camera-to-projector homography.
  - Asynchronous background thread captures V4L2 `YUYV` frames and runs TensorFlow Lite MoveNet INT8 on 1 core while 3 Cortex-A72 cores run the OpenMP simulation.
  - **Interactive Hand Swirl, Perching & Projector Eye Safety:** Raised hands invite nearby butterflies into a gentle orbit or golden-angle spiral perch (`kGoldenPerchTable`); detected heads are masked last before frame swap with a solid `#000000` blackout circle for eye safety.

---

## Repository Layout

```text
butterfly/
├── AGENTS.md                        # Complete architectural & mathematical specification
├── LOCAL.md                         # Raspberry Pi 4B hardware & systemd setup notes
├── README.md                        # Project overview & build guide
└── butterfly_vis/
    ├── Makefile                     # C++26 build system (-std=c++26 -O3 -mcpu=cortex-a72 -fopenmp)
    ├── butterfly.service            # Systemd unit (--count 75 --wait-hardware)
    ├── assets/                      # Embedded at compile time into .rodata
    │   ├── movenet_lightning_int8.tflite
    │   └── species_0.png .. species_7.png
    ├── patches/
    │   └── raylib-drm-gles3.patch   # Pi 4 DRM connector/CRTC discovery & GLES3 context patch
    ├── shaders/
    │   ├── butterfly.vert           # #version 300 es instanced 4-wing orthographic shader
    │   └── butterfly.frag           # #version 300 es two-sided SSS + iridescence shader
    ├── src/
    │   ├── main.cpp                 # DRM/GBM init, CLI flags, render loop, head blackout
    │   ├── cpp26.hpp                # Compile-time .rodata asset embedding (BF_EMBED_BINARY)
    │   ├── mesh.hpp                 # 26-vertex, 20-triangle 4-wing articulated butterfly mesh
    │   ├── textures.hpp             # Museum PNG loader -> 512x512x8 GL_TEXTURE_2D_ARRAY
    │   ├── sim.hpp                  # 3-core OpenMP simulation, 3D roll, curl flow & perching
    │   ├── vision.hpp               # USB V4L2 camera worker + embedded TFLite MoveNet INT8
    │   ├── calibration.hpp          # Non-blocking 4-corner AprilTag (tag36h11) + HW zoom
    │   └── overlay_gl.hpp           # 2D GLES3 renderer for eye-safety blackout & AprilTags
    └── third_party/
        ├── raylib/                  # Git submodule (raysan5/raylib @ 5.5, patched via Makefile)
        ├── apriltag/                # Vendored libapriltag headers & arm64 shared libraries
        └── tflite/                  # Vendored TensorFlow Lite 2.20 headers & arm64 libraries
```

---

## Building from Source

Clone with submodules and run `make` inside `butterfly_vis/` (the `Makefile` automatically initializes `third_party/raylib`, applies [`patches/raylib-drm-gles3.patch`](butterfly_vis/patches/raylib-drm-gles3.patch), builds `libraylib.a` for `PLATFORM_DRM` + `GRAPHICS_API_OPENGL_ES3`, and compiles `butterfly_vis`):

```bash
git clone --recurse-submodules https://github.com/salamanders/butterfly.git
cd butterfly/butterfly_vis
make -j4
```

---

## CLI Options

```bash
./butterfly_vis [options]
```

| Flag | Description |
| :--- | :--- |
| `--count N` | Number of butterflies (default `75` at 2x hero scale, clamped `10..3000`) |
| `--frames N` | Run `N` frames and exit cleanly (`0` = run indefinitely) |
| `--headless` | Force offscreen `1920x1080` GBM/EGL rendering on `/dev/dri/renderD128` |
| `--demo` | Inject synthetic visitor head + 2 raised hands for camera-free testing |
| `--wait-hardware` | Poll `/sys/class/drm/card*-HDMI-A-*/status` until a projector is connected |
| `--screenshot PATH` | Save final rendered frame of a `--frames N` run to PNG |
