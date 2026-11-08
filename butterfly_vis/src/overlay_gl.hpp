#pragma once
#include "sim.hpp"
#include <GLES3/gl3.h>
#include <apriltag/apriltag.h>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

namespace bf {

inline constexpr float kTwoPiOverlay = 2.0f * std::numbers::pi_v<float>;

struct MoteVertex {
    float x, y;
    float u, v;
    float r, g, b;
};

// Lightweight OpenGL ES 3.0 2D screen-space primitive renderer
// Used for:
// 1. Wingtip fairy-dust / firefly motes (drawn onto #000000 background before 3D butterflies)
// 2. Solid #000000 head blackout circle (drawn LAST before swap)
// 3. Standard AprilTag fiducial corner tiles during startup calibration
class OverlayRenderer {
public:
    GLuint prog = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint locRes = -1;
    GLint locColor = -1;

    GLuint moteProg = 0;
    GLuint moteVao = 0;
    GLuint moteVbo = 0;
    GLint locMoteRes = -1;
    std::vector<MoteVertex> moteVerts_;

    void init() {
        const char* vsSrc =
            "#version 300 es\n"
            "precision highp float;\n"
            "layout(location = 0) in vec2 a_posPx;\n"
            "uniform vec2 u_resolution;\n"
            "void main() {\n"
            "    vec2 ndc = (a_posPx / u_resolution) * 2.0 - 1.0;\n"
            "    ndc.y = -ndc.y;\n"
            "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
            "}\n";

        const char* fsSrc =
            "#version 300 es\n"
            "precision highp float;\n"
            "uniform vec4 u_color;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "    fragColor = u_color;\n"
            "}\n";

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vsSrc, nullptr);
        glCompileShader(vs);

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fsSrc, nullptr);
        glCompileShader(fs);

        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);

        locRes   = glGetUniformLocation(prog, "u_resolution");
        locColor = glGetUniformLocation(prog, "u_color");

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float), nullptr, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Bioluminescent fairy-dust mote shader (fades smoothly to #000000 with GL_BLEND disabled)
        const char* moteVsSrc =
            "#version 300 es\n"
            "precision highp float;\n"
            "layout(location = 0) in vec2 a_posPx;\n"
            "layout(location = 1) in vec2 a_uv;\n"
            "layout(location = 2) in vec3 a_color;\n"
            "uniform vec2 u_resolution;\n"
            "out vec2 v_uv;\n"
            "out vec3 v_color;\n"
            "void main() {\n"
            "    vec2 ndc = (a_posPx / u_resolution) * 2.0 - 1.0;\n"
            "    ndc.y = -ndc.y;\n"
            "    v_uv = a_uv;\n"
            "    v_color = a_color;\n"
            "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
            "}\n";

        const char* moteFsSrc =
            "#version 300 es\n"
            "precision highp float;\n"
            "in vec2 v_uv;\n"
            "in vec3 v_color;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "    float r2 = dot(v_uv, v_uv);\n"
            "    if (r2 > 1.0) discard;\n"
            "    float core = exp(-r2 * 4.5);\n"
            "    float rays = exp(-abs(v_uv.x) * 7.5) * exp(-abs(v_uv.y) * 1.8)\n"
            "               + exp(-abs(v_uv.y) * 7.5) * exp(-abs(v_uv.x) * 1.8);\n"
            "    float falloff = clamp((core + rays * 0.38) * (1.0 - r2), 0.0, 1.0);\n"
            "    if (falloff < 0.02) discard;\n"
            "    vec3 col = mix(v_color * falloff, vec3(1.0, 0.98, 0.92), core * core * 0.65);\n"
            "    fragColor = vec4(col, 1.0);\n"
            "}\n";

        GLuint mvs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(mvs, 1, &moteVsSrc, nullptr);
        glCompileShader(mvs);

        GLuint mfs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(mfs, 1, &moteFsSrc, nullptr);
        glCompileShader(mfs);

        moteProg = glCreateProgram();
        glAttachShader(moteProg, mvs);
        glAttachShader(moteProg, mfs);
        glLinkProgram(moteProg);
        glDeleteShader(mvs);
        glDeleteShader(mfs);

        locMoteRes = glGetUniformLocation(moteProg, "u_resolution");

        glGenVertexArrays(1, &moteVao);
        glGenBuffers(1, &moteVbo);
        glBindVertexArray(moteVao);
        glBindBuffer(GL_ARRAY_BUFFER, moteVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<size_t>(MAX_FAIRY_MOTES) * 6 * sizeof(MoteVertex), nullptr, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(MoteVertex), reinterpret_cast<void*>(offsetof(MoteVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MoteVertex), reinterpret_cast<void*>(offsetof(MoteVertex, u)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MoteVertex), reinterpret_cast<void*>(offsetof(MoteVertex, r)));
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        moteVerts_.reserve(static_cast<size_t>(MAX_FAIRY_MOTES) * 6);
    }

    void destroy() {
        if (moteVbo) glDeleteBuffers(1, &moteVbo);
        if (moteVao) glDeleteVertexArrays(1, &moteVao);
        if (moteProg) glDeleteProgram(moteProg);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (prog) glDeleteProgram(prog);
        moteVbo = moteVao = moteProg = vbo = vao = prog = 0;
    }

    // Render glowing fairy-dust / firefly motes onto the #000000 background (single draw call, no GL_BLEND)
    void drawFairyDust(
        std::span<const FairyMote> motes,
        float simTime,
        float orthoHalfW, float orthoHalfH,
        int screenW, int screenH)
    {
        moteVerts_.clear();
        const auto fw = float(screenW);
        const auto fh = float(screenH);

        for (const auto& m : motes) {
            float life01 = std::clamp(m.age / m.max_age, 0.0f, 1.0f);
            float env = std::sin(life01 * std::numbers::pi_v<float>);
            float twinkle = 0.62f + 0.38f * std::sin(simTime * 5.2f + m.phase);
            float brightness = env * twinkle;
            if (brightness < 0.05f) continue;

            float px = ((m.pos.x / orthoHalfW) + 1.0f) * 0.5f * fw;
            float py = (1.0f - (m.pos.y / orthoHalfH)) * 0.5f * fh;
            if (px < -16.0f || px > fw + 16.0f || py < -16.0f || py > fh + 16.0f) continue;

            float depthScale = std::clamp(1.0f + (m.pos.z + 1.8f) * 0.14f, 0.58f, 1.38f);
            float rad = m.size_px * depthScale * (0.55f + 0.45f * brightness);
            float cr = m.r * brightness;
            float cg = m.g * brightness;
            float cb = m.b * brightness;

            moteVerts_.push_back({px - rad, py - rad, -1.0f, -1.0f, cr, cg, cb});
            moteVerts_.push_back({px + rad, py - rad,  1.0f, -1.0f, cr, cg, cb});
            moteVerts_.push_back({px + rad, py + rad,  1.0f,  1.0f, cr, cg, cb});
            moteVerts_.push_back({px - rad, py - rad, -1.0f, -1.0f, cr, cg, cb});
            moteVerts_.push_back({px + rad, py + rad,  1.0f,  1.0f, cr, cg, cb});
            moteVerts_.push_back({px - rad, py + rad, -1.0f,  1.0f, cr, cg, cb});
        }

        if (moteVerts_.empty()) return;

        glUseProgram(moteProg);
        glUniform2f(locMoteRes, fw, fh);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        glBindVertexArray(moteVao);
        glBindBuffer(GL_ARRAY_BUFFER, moteVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<size_t>(MAX_FAIRY_MOTES) * 6 * sizeof(MoteVertex), nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, moteVerts_.size() * sizeof(MoteVertex), moteVerts_.data());
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(moteVerts_.size()));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    // Draw a solid filled circle in pixel coordinates (0..screenW, 0..screenH)
    void drawSolidCircle(
        float cx, float cy, float radiusPx,
        float r, float g, float b, float a,
        int screenW, int screenH, int segments = 64)
    {
        std::vector<float> verts;
        verts.reserve(static_cast<size_t>(segments + 2) * 2);
        verts.push_back(cx);
        verts.push_back(cy);
        for (int i = 0; i <= segments; ++i) {
            float ang = (float(i) / float(segments)) * kTwoPiOverlay;
            verts.push_back(cx + std::cos(ang) * radiusPx);
            verts.push_back(cy + std::sin(ang) * radiusPx);
        }

        glUseProgram(prog);
        glUniform2f(locRes, float(screenW), float(screenH));
        glUniform4f(locColor, r, g, b, a);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLE_FAN, 0, GLsizei(verts.size() / 2));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    // Draw an official AprilTag image_u8_t centered at (cx, cy) with total outer size totalSizePx.
    // Includes a 1-cell white quiet zone around the tag so it is detected cleanly against #000000 black.
    void drawAprilTag(
        const image_u8_t* tagImg,
        float cx, float cy, float totalSizePx,
        int screenW, int screenH)
    {
        if (!tagImg || tagImg->width <= 0 || tagImg->height <= 0) return;

        // apriltag_to_image(tag36h11) returns a 10x10 image (8x8 black tag + 1-cell white border on all sides).
        // Add 1 extra cell of white quiet-zone padding around it for high-contrast projector detection.
        const int w = tagImg->width;
        const int h = tagImg->height;
        const float cellPx = totalSizePx / float(w + 2);
        const float x0 = cx - 0.5f * totalSizePx;
        const float y0 = cy - 0.5f * totalSizePx;

        std::vector<float> whiteTris;
        whiteTris.reserve(static_cast<size_t>(w + 2) * static_cast<size_t>(h + 2) * 12);

        auto pushQuad = [&](float qx0, float qy0, float qx1, float qy1) {
            whiteTris.push_back(qx0); whiteTris.push_back(qy0);
            whiteTris.push_back(qx1); whiteTris.push_back(qy0);
            whiteTris.push_back(qx1); whiteTris.push_back(qy1);
            whiteTris.push_back(qx0); whiteTris.push_back(qy0);
            whiteTris.push_back(qx1); whiteTris.push_back(qy1);
            whiteTris.push_back(qx0); whiteTris.push_back(qy1);
        };

        for (int gy = -1; gy <= h; ++gy) {
            for (int gx = -1; gx <= w; ++gx) {
                bool isWhite = true;
                if (gx >= 0 && gx < w && gy >= 0 && gy < h) {
                    isWhite = (tagImg->buf[gy * tagImg->stride + gx] > 127);
                }
                if (isWhite) {
                    float px0 = x0 + float(gx + 1) * cellPx;
                    float py0 = y0 + float(gy + 1) * cellPx;
                    pushQuad(px0, py0, px0 + cellPx, py0 + cellPx);
                }
            }
        }

        if (whiteTris.empty()) return;

        glUseProgram(prog);
        glUniform2f(locRes, float(screenW), float(screenH));
        glUniform4f(locColor, 1.0f, 1.0f, 1.0f, 1.0f);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, whiteTris.size() * sizeof(float), whiteTris.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(whiteTris.size() / 2));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glUseProgram(0);
    }
};

} // namespace bf
