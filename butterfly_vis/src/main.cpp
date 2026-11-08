#include <raylib.h>
#include <rlgl.h>
#include <raymath.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numbers>
#include <span>
#include <string>
#include <string_view>

#include "cpp26.hpp"
#include "mesh.hpp"
#include "textures.hpp"
#include "sim.hpp"
#include "overlay_gl.hpp"
#include "vision.hpp"
#include "calibration.hpp"

// Compile-time embedded GLSL ES 3.00 shaders in .rodata
BF_EMBED_BINARY(butterfly_vert, "shaders/butterfly.vert");
BF_EMBED_BINARY(butterfly_frag, "shaders/butterfly.frag");

namespace {

volatile std::sig_atomic_t g_keepRunning = 1;

void handleSignal(int) {
    g_keepRunning = 0;
}

GLuint compileEmbeddedShaderOrDie(GLenum type, const char* label, std::string_view srcView) {
    std::string src(srcView);
    const char* csrc = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);

    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::println(stderr, "[ERROR] Shader compilation failed ({}):\n{}", label, log);
        std::exit(EXIT_FAILURE);
    }
    return sh;
}

GLuint linkEmbeddedProgramOrDie() {
    GLuint vs = compileEmbeddedShaderOrDie(GL_VERTEX_SHADER, "butterfly.vert", embedded_butterfly_vert_str());
    GLuint fs = compileEmbeddedShaderOrDie(GL_FRAGMENT_SHADER, "butterfly.frag", embedded_butterfly_frag_str());
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::println(stderr, "[ERROR] Program link failed:\n{}", log);
        std::exit(EXIT_FAILURE);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

struct HeadlessGbmContext {
    int drmFd = -1;
    gbm_device* gbmDev = nullptr;
    gbm_surface* gbmSurf = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;

    bool init(int width, int height) {
        const char* nodes[] = {"/dev/dri/renderD128", "/dev/dri/card0", "/dev/dri/card1"};
        for (const char* p : nodes) {
            drmFd = open(p, O_RDWR | O_CLOEXEC);
            if (drmFd >= 0) {
                gbmDev = gbm_create_device(drmFd);
                if (gbmDev) {
                    std::println("[Headless] Opened DRM render node: {}", p);
                    break;
                }
                close(drmFd);
                drmFd = -1;
            }
        }
        if (!gbmDev) return false;

        gbmSurf = gbm_surface_create(gbmDev, width, height, GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING);
        if (!gbmSurf) return false;

        display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(gbmDev));
        if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) return false;
        eglBindAPI(EGL_OPENGL_ES_API);

        const EGLint cfgAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
        };
        EGLConfig cfg = nullptr;
        EGLint numCfgs = 0;
        if (!eglChooseConfig(display, cfgAttribs, &cfg, 1, &numCfgs) || numCfgs == 0) return false;

        const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ctxAttribs);
        if (context == EGL_NO_CONTEXT) return false;

        surface = eglCreateWindowSurface(display, cfg, reinterpret_cast<EGLNativeWindowType>(gbmSurf), nullptr);
        if (surface == EGL_NO_SURFACE) return false;

        return eglMakeCurrent(display, surface, surface, context) == EGL_TRUE;
    }

    void swap() {
        eglSwapBuffers(display, surface);
        gbm_bo* bo = gbm_surface_lock_front_buffer(gbmSurf);
        if (bo) gbm_surface_release_buffer(gbmSurf, bo);
    }

    void destroy() {
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
            eglTerminate(display);
        }
        if (gbmSurf) gbm_surface_destroy(gbmSurf);
        if (gbmDev) gbm_device_destroy(gbmDev);
        if (drmFd >= 0) close(drmFd);
    }
};

bool hasConnectedDrmDisplay(std::string* outConnectedPath = nullptr) {
    const char* statusPaths[] = {
        "/sys/class/drm/card0-HDMI-A-1/status",
        "/sys/class/drm/card0-HDMI-A-2/status",
        "/sys/class/drm/card1-HDMI-A-1/status",
        "/sys/class/drm/card1-HDMI-A-2/status",
        "/sys/class/drm/card2-HDMI-A-1/status",
        "/sys/class/drm/card2-HDMI-A-2/status"
    };
    for (const char* p : statusPaths) {
        std::ifstream ifs(p);
        std::string s;
        if (ifs && (ifs >> s) && s == "connected") {
            if (outConnectedPath) *outConnectedPath = p;
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    int butterflyCount = 75;
    int maxFrames = 0; // 0 = infinite until Ctrl+C / Esc
    bool forceHeadless = false;
    bool demoMode = false;
    bool waitHardware = false;
    const char* screenshotPath = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if (arg == "--count" && i + 1 < argc) {
            butterflyCount = std::clamp(std::atoi(argv[++i]), 10, 3000);
        } else if (arg == "--frames" && i + 1 < argc) {
            maxFrames = std::atoi(argv[++i]);
        } else if (arg == "--headless") {
            forceHeadless = true;
        } else if (arg == "--demo") {
            demoMode = true;
        } else if (arg == "--wait-hardware") {
            waitHardware = true;
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
        }
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    bool usingHeadlessGbm = false;
    HeadlessGbmContext headlessCtx;
    int renderW = 1920;
    int renderH = 1080;
    std::string activeDrmStatusPath;

    if (waitHardware && !forceHeadless) {
        bool printedWait = false;
        while (g_keepRunning && !hasConnectedDrmDisplay(&activeDrmStatusPath)) {
            if (!printedWait) {
                std::println("[Boot] Waiting for projector on either micro-HDMI port (HDMI-A-1 or HDMI-A-2)...");
                std::fflush(stdout);
                printedWait = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if (!activeDrmStatusPath.empty()) {
            std::println("[Boot] Detected connected projector on {}", activeDrmStatusPath);
            std::fflush(stdout);
        }
    }

    bool drmConnected = (!forceHeadless) && hasConnectedDrmDisplay(&activeDrmStatusPath);
    if (drmConnected) {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_VSYNC_HINT);
        InitWindow(1920, 1080, "RPi4B Butterfly Kaleidoscope");
    }

    if (drmConnected && IsWindowReady()) {
        HideCursor();
        renderW = GetRenderWidth();
        renderH = GetRenderHeight();
        if (renderW <= 0 || renderH <= 0) {
            renderW = GetScreenWidth();
            renderH = GetScreenHeight();
        }
    } else {
        if (!forceHeadless) {
            std::println(stderr, "[WARN] No connected HDMI KMS display detected; using 1920x1080 GBM/EGL ES3 hardware context.");
        }
        if (!headlessCtx.init(renderW, renderH)) {
            std::println(stderr, "[ERROR] Failed to initialize GBM/EGL ES3 hardware context.");
            return EXIT_FAILURE;
        }
        usingHeadlessGbm = true;
        glViewport(0, 0, renderW, renderH);
    }

    float aspect = (renderH > 0) ? (float(renderW) / float(renderH)) : (16.0f / 9.0f);
    float orthoHalfH = 2.25f;
    float orthoHalfW = orthoHalfH * aspect;

    std::println("[Init] GL_VERSION  : {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    std::println("[Init] GL_RENDERER : {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::println("[Init] Resolution  : {}x{} (aspect {:.4f}) | Butterflies: {} | Mode: {}",
                 renderW, renderH, aspect, butterflyCount, demoMode ? "Interactive Demo" : "Live Vision");

    // Compile and link embedded GLSL ES 3.00 instanced butterfly shader & 2D overlay shader
    GLuint butterflyProg = linkEmbeddedProgramOrDie();
    GLint locViewProj  = glGetUniformLocation(butterflyProg, "u_viewProj");
    GLint locTexArray  = glGetUniformLocation(butterflyProg, "u_texArray");
    GLint locLightDir  = glGetUniformLocation(butterflyProg, "u_lightDir");

    bf::OverlayRenderer overlay;
    overlay.init();

    // Generate 8-species 512x512 GL_TEXTURE_2D_ARRAY with mipmaps from embedded museum PNGs
    auto tTex0 = std::chrono::steady_clock::now();
    GLuint speciesTexArray = bf::createSpeciesTextureArray();
    auto tTex1 = std::chrono::steady_clock::now();
    double texMs = std::chrono::duration<double, std::milli>(tTex1 - tTex0).count();
    std::println("[Init] Built 512x512x8 RGBA8 mipmapped GL_TEXTURE_2D_ARRAY in {:.1f} ms", texMs);

    // Initialize simulation
    static bf::ButterflySimulation sim;
    sim.init(butterflyCount);

    // Create VAO, static mesh VBO/EBO, and stream instance VBO
    GLuint butterflyVAO = 0, meshVBO = 0, meshEBO = 0, instanceVBO = 0;
    glGenVertexArrays(1, &butterflyVAO);
    glGenBuffers(1, &meshVBO);
    glGenBuffers(1, &meshEBO);
    glGenBuffers(1, &instanceVBO);

    glBindVertexArray(butterflyVAO);

    glBindBuffer(GL_ARRAY_BUFFER, meshVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kButterflyVerts), kButterflyVerts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kButterflyIndices), kButterflyIndices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, pos)));
    glVertexAttribDivisor(0, 0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, uv)));
    glVertexAttribDivisor(1, 0);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, side)));
    glVertexAttribDivisor(2, 0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, butterflyCount * sizeof(ButterflyInstance), sim.instances.data(), GL_STREAM_DRAW);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), reinterpret_cast<void*>(offsetof(ButterflyInstance, pos)));
    glVertexAttribDivisor(3, 1);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), reinterpret_cast<void*>(offsetof(ButterflyInstance, forward)));
    glVertexAttribDivisor(4, 1);

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), reinterpret_cast<void*>(offsetof(ButterflyInstance, up)));
    glVertexAttribDivisor(5, 1);

    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), reinterpret_cast<void*>(offsetof(ButterflyInstance, wing_angle)));
    glVertexAttribDivisor(6, 1);

    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), reinterpret_cast<void*>(offsetof(ButterflyInstance, wing_flex)));
    glVertexAttribDivisor(7, 1);

    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(ButterflyInstance), reinterpret_cast<void*>(offsetof(ButterflyInstance, scale)));
    glVertexAttribDivisor(8, 1);

    // CRITICAL: Attribute 9 (species_id) is uint32_t -> MUST use glVertexAttribIPointer!
    glEnableVertexAttribArray(9);
    glVertexAttribIPointer(9, 1, GL_UNSIGNED_INT, sizeof(ButterflyInstance), reinterpret_cast<void*>(offsetof(ButterflyInstance, species_id)));
    glVertexAttribDivisor(9, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLenum initErr = glGetError();
    if (initErr != GL_NO_ERROR) {
        std::println(stderr, "[ERROR] OpenGL error after VAO/Texture setup: 0x{:04X}", initErr);
        overlay.destroy();
        if (!usingHeadlessGbm) CloseWindow(); else headlessCtx.destroy();
        return EXIT_FAILURE;
    }

    // Orthographic 1080p camera & projection setup via <raymath.h>:
    const Vector3 camPos = {0.0f, 0.0f, 8.6f};
    const Matrix viewMat = MatrixLookAt(camPos, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    const Matrix projMat = MatrixOrtho(-orthoHalfW, orthoHalfW, -orthoHalfH, orthoHalfH, 0.5f, 35.0f);
    // In raymath.h, MatrixMultiply(left, right) computes right * left in column-major OpenGL order:
    const float16 viewProj = MatrixToFloatV(MatrixMultiply(viewMat, projMat));
    const float* viewProjMat = viewProj.v;

    if (std::fabs(viewProjMat[15] - 1.0f) > 0.01f) {
        std::println(stderr, "[ERROR] Matrix sanity check failed: clip.w = {:.3f} (expected +1.0)", viewProjMat[15]);
        overlay.destroy();
        if (!usingHeadlessGbm) CloseWindow(); else headlessCtx.destroy();
        return EXIT_FAILURE;
    }

    // Start background VisionWorker thread (USB V4L2 camera via /dev/v4l/by-id + MoveNet pose)
    bf::VisionWorker vision;
    vision.start(demoMode);

    auto shouldCloseFn = [&]() -> bool {
        if (!g_keepRunning) return true;
        if (!usingHeadlessGbm && (WindowShouldClose() || IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE))) return true;
        return false;
    };
    auto flipPresentFn = [&](auto&& drawCallback) {
        if (!usingHeadlessGbm) {
            BeginDrawing();
            ClearBackground(BLACK);
            rlDrawRenderBatchActive();
            drawCallback();
            EndDrawing();
        } else {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawCallback();
            headlessCtx.swap();
        }
    };

    // Run Non-Blocking Standard AprilTag (tag36h11 IDs 0..3) Startup Calibration
    bf::Homography3x3 calibH = bf::runStartupCalibration(
        renderW, renderH, vision, overlay, flipPresentFn, shouldCloseFn
    );

    // Normalized directional key light vector pointing FROM light TO scene via <raymath.h>
    const Vector3 lightVec = Vector3Normalize({-0.35f, -0.80f, -0.48f});
    const float lightDir[3] = {lightVec.x, lightVec.y, lightVec.z};

    double telemetryAccumSec = 0.0;
    double simAccumMs = 0.0;
    int telemetryFrames = 0;
    int totalFramesRendered = 0;
    constexpr auto indexCount = static_cast<GLsizei>(sizeof(kButterflyIndices) / sizeof(kButterflyIndices[0]));

    // Screen-pixel <-> Orthographic 3D world coordinate converters
    auto pxToWorldX = [&](float px) -> float {
        return ((px / float(renderW)) * 2.0f - 1.0f) * orthoHalfW;
    };
    auto pyToWorldY = [&](float py) -> float {
        return (1.0f - (py / float(renderH)) * 2.0f) * orthoHalfH;
    };

    while (g_keepRunning && (usingHeadlessGbm || !WindowShouldClose())) {
        if (!usingHeadlessGbm && (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE))) {
            break;
        }

        auto nowClock = std::chrono::steady_clock::now();
        float dt = (usingHeadlessGbm || maxFrames > 0)
            ? (1.0f / 60.0f)
            : GetFrameTime();
        if (dt <= 0.0f) dt = 1.0f / 60.0f;

        // 1. Poll latest VisionState and map normalized camera (u,v) -> screen pixels & 3D world
        bf::VisionState vs = vision.getState();
        bool hasHeadBlackout = false;
        float headX_px = 0.0f, headY_px = 0.0f, headR_px = 0.0f;

        if (vs.has_head) {
            bf::mapCamToScreen(calibH, vs.head.u, vs.head.v, headX_px, headY_px);
            // Ignore heads outside the [0..renderW, 0..renderH] projection area
            if (headX_px >= 0.0f && headX_px <= float(renderW) &&
                headY_px >= 0.0f && headY_px <= float(renderH)) {
                float hxRight = 0.0f, hyRight = 0.0f, hxDown = 0.0f, hyDown = 0.0f;
                bf::mapCamToScreen(calibH, vs.head.u + vs.head.radius_u, vs.head.v, hxRight, hyRight);
                bf::mapCamToScreen(calibH, vs.head.u, vs.head.v + vs.head.radius_u, hxDown, hyDown);
                float dxPx = std::hypot(hxRight - headX_px, hyRight - headY_px);
                float dyPx = std::hypot(hxDown - headX_px, hyDown - headY_px);
                float minRadPx = 82.0f * (float(renderH) / 720.0f);
                float maxRadPx = 260.0f * (float(renderH) / 720.0f);
                headR_px = std::clamp(std::max(dxPx, dyPx) * 1.85f, minRadPx, maxRadPx);
                hasHeadBlackout = true;
            }
        }

        std::vector<std::pair<float, float>> safePalmsWorld;
        for (const auto& palm : vs.palms) {
            float px = 0.0f, py = 0.0f;
            bf::mapCamToScreen(calibH, palm.u, palm.v, px, py);
            // Strictly reject any palm outside the [0..renderW, 0..renderH] projection area
            if (px < 0.0f || px > float(renderW) || py < 0.0f || py > float(renderH)) continue;
            safePalmsWorld.emplace_back(pxToWorldX(px), pyToWorldY(py));
        }

        // 2. Step multi-core OpenMP simulation (flocking + gentle hand swirl + steady-hand perching)
        auto tSim0 = std::chrono::steady_clock::now();
        sim.step(dt, safePalmsWorld);
        auto tSim1 = std::chrono::steady_clock::now();
        double simMs = std::chrono::duration<double, std::milli>(tSim1 - tSim0).count();

        if (!usingHeadlessGbm) {
            BeginDrawing();
            ClearBackground(BLACK); // Pure (0, 0, 0, 255) for projector zero-black!
            rlDrawRenderBatchActive();
        } else {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        // 3a. Bioluminescent wingtip fairy-dust / firefly motes onto #000000 background (no GL_BLEND)
        overlay.drawFairyDust(sim.motes, sim.simTime, orthoHalfW, orthoHalfH, renderW, renderH);

        // 3b. Raw OpenGL ES 3.0 Instanced 3D Butterfly Pass
        glUseProgram(butterflyProg);
        glUniformMatrix4fv(locViewProj, 1, GL_FALSE, viewProjMat);
        glUniform1i(locTexArray, 0);
        glUniform3fv(locLightDir, 1, lightDir);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);      // CRITICAL: Keep TBDR HSR active!
        glDisable(GL_CULL_FACE);  // Both dorsal and ventral wing surfaces visible

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, speciesTexArray);

        glBindVertexArray(butterflyVAO);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        // Orphan buffer with nullptr first so VC6 driver never stalls waiting on previous frame's DMA!
        glBufferData(GL_ARRAY_BUFFER, sim.count * sizeof(ButterflyInstance), nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sim.count * sizeof(ButterflyInstance), sim.instances.data());

        glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr, sim.count);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);

        // 4. Draw solid #000000 head blackout circle LAST, immediately before flip/readback
        if (hasHeadBlackout) {
            overlay.drawSolidCircle(headX_px, headY_px, headR_px,
                                    0.0f, 0.0f, 0.0f, 1.0f, renderW, renderH);
        }

        if (screenshotPath != nullptr && maxFrames > 0 && (totalFramesRendered + 1) == maxFrames) {
            const size_t rowBytes = static_cast<size_t>(renderW) * 4;
            const size_t frameBytes = rowBytes * static_cast<size_t>(renderH);
            std::vector<uint8_t> pixels(frameBytes);
            glReadPixels(0, 0, renderW, renderH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            std::vector<uint8_t> flipped(frameBytes);
            for (int y = 0; y < renderH; ++y) {
                std::memcpy(
                    &flipped[static_cast<size_t>(y) * rowBytes],
                    &pixels[static_cast<size_t>(renderH - 1 - y) * rowBytes],
                    rowBytes
                );
            }
            Image img = {
                .data = flipped.data(),
                .width = renderW,
                .height = renderH,
                .mipmaps = 1,
                .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
            };
            ExportImage(img, screenshotPath);
            std::println("[Screenshot] Saved {}x{} frame to {}", renderW, renderH, screenshotPath);
        }

        if (!usingHeadlessGbm) {
            EndDrawing();
        } else {
            headlessCtx.swap();
        }

        double realFrameDt = std::chrono::duration<double>(std::chrono::steady_clock::now() - nowClock).count();
        telemetryAccumSec += std::max(realFrameDt, 1e-4);
        simAccumMs += simMs;
        telemetryFrames++;
        totalFramesRendered++;

        if (telemetryAccumSec >= 2.0 || (maxFrames > 0 && totalFramesRendered == maxFrames)) {
            int totalPerched = sim.perchedCounts_[0] + sim.perchedCounts_[1];
            double avgFps = double(telemetryFrames) / telemetryAccumSec;
            double avgFrameMs = (telemetryAccumSec * 1000.0) / double(telemetryFrames);
            double avgSimMs = simAccumMs / double(telemetryFrames);
            float visMs = vision.lastVisionMs.load();
            std::println("[Telemetry] FPS: {:6.1f} ({:5.2f} ms/frame) | Sim: {:.2f} ms | Vision: {:.2f} ms | Butterflies: {} (Perched: {}, Palms: {}) | Res: {}x{}",
                         avgFps, avgFrameMs, avgSimMs, visMs, sim.count, totalPerched, safePalmsWorld.size(), renderW, renderH);
            std::fflush(stdout);
            telemetryAccumSec = 0.0;
            simAccumMs = 0.0;
            telemetryFrames = 0;
        }

        if (maxFrames > 0 && totalFramesRendered >= maxFrames) {
            break;
        }
    }

    std::println("[Shutdown] Stopping vision worker and cleaning up GL resources...");
    vision.stop();
    overlay.destroy();
    glDeleteBuffers(1, &instanceVBO);
    glDeleteBuffers(1, &meshEBO);
    glDeleteBuffers(1, &meshVBO);
    glDeleteVertexArrays(1, &butterflyVAO);
    glDeleteTextures(1, &speciesTexArray);
    glDeleteProgram(butterflyProg);

    if (!usingHeadlessGbm) {
        CloseWindow();
    } else {
        headlessCtx.destroy();
    }
    return EXIT_SUCCESS;
}
