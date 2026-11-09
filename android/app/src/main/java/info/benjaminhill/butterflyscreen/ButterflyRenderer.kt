package info.benjaminhill.butterflyscreen

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.os.SystemClock
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.sqrt

class ButterflyRenderer(private val context: Context) : GLSurfaceView.Renderer {

    @Volatile
    var butterflyCount: Int = ButterflyPrefs.getCount(context)

    @Volatile
    var zoomLevel: Float = ButterflyPrefs.getZoom(context)

    private val sim = ButterflySimulation(butterflyCount, zoomLevel)

    private var screenW = 1080
    private var screenH = 2400
    private var lastFrameNs = 0L

    private var butterflyProg = 0
    private var locViewProj = -1
    private var locTexArray = -1
    private var locSurfaceToLight = -1
    private var locHalfVec = -1
    private var locSssBase = -1

    private var moteProg = 0
    private var locMoteRes = -1

    private var speciesTexArray = 0
    private var butterflyVao = 0
    private var instanceVbo = 0
    private var moteVao = 0
    private var moteVbo = 0

    private val viewProjMat = FloatArray(16)
    private val surfaceToLight = run {
        val lx = 0.35f
        val ly = 0.80f
        val lz = 0.48f
        val len = sqrt(lx * lx + ly * ly + lz * lz)
        floatArrayOf(lx / len, ly / len, lz / len)
    }
    private val halfVec = run {
        val hx = surfaceToLight[0]
        val hy = surfaceToLight[1]
        val hz = surfaceToLight[2] + 1.0f
        val len = sqrt(hx * hx + hy * hy + hz * hz)
        floatArrayOf(hx / len, hy / len, hz / len)
    }
    private val sssBase = run {
        val backAlign = (-surfaceToLight[2]).coerceIn(0.0f, 1.0f)
        backAlign * backAlign * 0.55f + 0.22f
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        lastFrameNs = SystemClock.elapsedRealtimeNanos()

        butterflyProg = createProgram(BUTTERFLY_VS_SRC, BUTTERFLY_FS_SRC)
        locViewProj = GLES30.glGetUniformLocation(butterflyProg, "u_viewProj")
        locTexArray = GLES30.glGetUniformLocation(butterflyProg, "u_texArray")
        locSurfaceToLight = GLES30.glGetUniformLocation(butterflyProg, "u_surfaceToLight")
        locHalfVec = GLES30.glGetUniformLocation(butterflyProg, "u_halfVec")
        locSssBase = GLES30.glGetUniformLocation(butterflyProg, "u_sssBase")

        moteProg = createProgram(MOTE_VS_SRC, MOTE_FS_SRC)
        locMoteRes = GLES30.glGetUniformLocation(moteProg, "u_resolution")

        speciesTexArray = loadSpeciesTextureArray()
        setupBuffers()
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        screenW = width.coerceAtLeast(1)
        screenH = height.coerceAtLeast(1)
        GLES30.glViewport(0, 0, screenW, screenH)
    }

    override fun onDrawFrame(gl: GL10?) {
        val nowNs = SystemClock.elapsedRealtimeNanos()
        val dt = ((nowNs - lastFrameNs) * 1e-9f).coerceIn(0.001f, 0.033f)
        lastFrameNs = nowNs

        sim.activeCount =
            butterflyCount.coerceIn(ButterflyPrefs.MIN_COUNT, ButterflyPrefs.MAX_COUNT)
        sim.zoomLevel = zoomLevel.coerceIn(ButterflyPrefs.MIN_ZOOM, ButterflyPrefs.MAX_ZOOM)

        val aspect = screenW.toFloat() / screenH.toFloat()
        val orthoHalfW: Float
        val orthoHalfH: Float
        if (aspect < 1.0f) {
            orthoHalfW = 1.65f
            orthoHalfH = 1.65f / aspect
        } else {
            orthoHalfH = 2.25f
            orthoHalfW = 2.25f * aspect
        }

        sim.step(dt, orthoHalfW, orthoHalfH)

        GLES30.glViewport(0, 0, screenW, screenH)
        GLES30.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT or GLES30.GL_DEPTH_BUFFER_BIT)

        // 1. Fairy-dust motes pass
        val numMoteVerts = sim.buildFairyMoteVertices(
            orthoHalfW,
            orthoHalfH,
            screenW.toFloat(),
            screenH.toFloat()
        )
        if (numMoteVerts > 0) {
            GLES30.glUseProgram(moteProg)
            GLES30.glUniform2f(locMoteRes, screenW.toFloat(), screenH.toFloat())
            GLES30.glDisable(GLES30.GL_DEPTH_TEST)
            GLES30.glDisable(GLES30.GL_CULL_FACE)
            GLES30.glDisable(GLES30.GL_BLEND)

            GLES30.glBindVertexArray(moteVao)
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, moteVbo)
            val moteCapacityBytes =
                ButterflySimulation.MAX_FAIRY_MOTES * 6 * ButterflySimulation.MOTE_FLOATS_PER_VERT * 4
            GLES30.glBufferData(
                GLES30.GL_ARRAY_BUFFER,
                moteCapacityBytes,
                null,
                GLES30.GL_STREAM_DRAW
            )
            GLES30.glBufferSubData(
                GLES30.GL_ARRAY_BUFFER,
                0,
                numMoteVerts * ButterflySimulation.MOTE_FLOATS_PER_VERT * 4,
                sim.moteFloatBuffer
            )
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, numMoteVerts)
        }

        // 2. Instanced 3D Butterflies pass
        computeOrthoViewProj(orthoHalfW, orthoHalfH, viewProjMat)
        GLES30.glUseProgram(butterflyProg)
        GLES30.glUniformMatrix4fv(locViewProj, 1, false, viewProjMat, 0)
        GLES30.glUniform1i(locTexArray, 0)
        GLES30.glUniform3fv(locSurfaceToLight, 1, surfaceToLight, 0)
        GLES30.glUniform3fv(locHalfVec, 1, halfVec, 0)
        GLES30.glUniform1f(locSssBase, sssBase)

        GLES30.glEnable(GLES30.GL_DEPTH_TEST)
        GLES30.glDepthFunc(GLES30.GL_LESS)
        GLES30.glDepthMask(true)
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_CULL_FACE)

        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D_ARRAY, speciesTexArray)

        GLES30.glBindVertexArray(butterflyVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, instanceVbo)
        val instanceCapacityBytes =
            ButterflySimulation.MAX_BUTTERFLIES * ButterflySimulation.INSTANCE_STRIDE_BYTES
        GLES30.glBufferData(
            GLES30.GL_ARRAY_BUFFER,
            instanceCapacityBytes,
            null,
            GLES30.GL_STREAM_DRAW
        )
        sim.instanceBuffer.position(0)
        GLES30.glBufferSubData(
            GLES30.GL_ARRAY_BUFFER,
            0,
            sim.activeCount * ButterflySimulation.INSTANCE_STRIDE_BYTES,
            sim.instanceBuffer
        )
        GLES30.glDrawElementsInstanced(
            GLES30.GL_TRIANGLES,
            BUTTERFLY_INDICES.size,
            GLES30.GL_UNSIGNED_SHORT,
            0,
            sim.activeCount
        )

        GLES30.glBindVertexArray(0)
    }

    private fun setupBuffers() {
        val vaos = IntArray(2)
        GLES30.glGenVertexArrays(2, vaos, 0)
        butterflyVao = vaos[0]
        moteVao = vaos[1]

        val vbos = IntArray(4)
        GLES30.glGenBuffers(4, vbos, 0)
        val meshVbo = vbos[0]
        val meshEbo = vbos[1]
        instanceVbo = vbos[2]
        moteVbo = vbos[3]

        val meshVertBuf = ByteBuffer.allocateDirect(BUTTERFLY_VERTS.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .apply {
                put(BUTTERFLY_VERTS)
                position(0)
            }

        val meshIdxBuf = ByteBuffer.allocateDirect(BUTTERFLY_INDICES.size * 2)
            .order(ByteOrder.nativeOrder())
            .asShortBuffer()
            .apply {
                put(BUTTERFLY_INDICES)
                position(0)
            }

        GLES30.glBindVertexArray(butterflyVao)

        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, meshVbo)
        GLES30.glBufferData(
            GLES30.GL_ARRAY_BUFFER,
            BUTTERFLY_VERTS.size * 4,
            meshVertBuf,
            GLES30.GL_STATIC_DRAW
        )

        GLES30.glBindBuffer(GLES30.GL_ELEMENT_ARRAY_BUFFER, meshEbo)
        GLES30.glBufferData(
            GLES30.GL_ELEMENT_ARRAY_BUFFER,
            BUTTERFLY_INDICES.size * 2,
            meshIdxBuf,
            GLES30.GL_STATIC_DRAW
        )

        // Per-vertex attributes (24 bytes stride = 6 floats)
        GLES30.glEnableVertexAttribArray(0)
        GLES30.glVertexAttribPointer(0, 3, GLES30.GL_FLOAT, false, 24, 0)
        GLES30.glVertexAttribDivisor(0, 0)

        GLES30.glEnableVertexAttribArray(1)
        GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT, false, 24, 12)
        GLES30.glVertexAttribDivisor(1, 0)

        GLES30.glEnableVertexAttribArray(2)
        GLES30.glVertexAttribPointer(2, 1, GLES30.GL_FLOAT, false, 24, 20)
        GLES30.glVertexAttribDivisor(2, 0)

        // Per-instance attributes (52 bytes stride)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, instanceVbo)
        GLES30.glBufferData(
            GLES30.GL_ARRAY_BUFFER,
            ButterflySimulation.MAX_BUTTERFLIES * ButterflySimulation.INSTANCE_STRIDE_BYTES,
            null,
            GLES30.GL_STREAM_DRAW
        )

        val stride = ButterflySimulation.INSTANCE_STRIDE_BYTES
        GLES30.glEnableVertexAttribArray(3)
        GLES30.glVertexAttribPointer(3, 3, GLES30.GL_FLOAT, false, stride, 0)
        GLES30.glVertexAttribDivisor(3, 1)

        GLES30.glEnableVertexAttribArray(4)
        GLES30.glVertexAttribPointer(4, 3, GLES30.GL_FLOAT, false, stride, 12)
        GLES30.glVertexAttribDivisor(4, 1)

        GLES30.glEnableVertexAttribArray(5)
        GLES30.glVertexAttribPointer(5, 3, GLES30.GL_FLOAT, false, stride, 24)
        GLES30.glVertexAttribDivisor(5, 1)

        GLES30.glEnableVertexAttribArray(6)
        GLES30.glVertexAttribPointer(6, 1, GLES30.GL_FLOAT, false, stride, 36)
        GLES30.glVertexAttribDivisor(6, 1)

        GLES30.glEnableVertexAttribArray(7)
        GLES30.glVertexAttribPointer(7, 1, GLES30.GL_FLOAT, false, stride, 40)
        GLES30.glVertexAttribDivisor(7, 1)

        GLES30.glEnableVertexAttribArray(8)
        GLES30.glVertexAttribPointer(8, 1, GLES30.GL_FLOAT, false, stride, 44)
        GLES30.glVertexAttribDivisor(8, 1)

        // Attribute 9 (species_id) MUST bind with glVertexAttribIPointer
        GLES30.glEnableVertexAttribArray(9)
        GLES30.glVertexAttribIPointer(9, 1, GLES30.GL_UNSIGNED_INT, stride, 48)
        GLES30.glVertexAttribDivisor(9, 1)

        GLES30.glBindVertexArray(0)

        // Setup Fairy-Dust Mote VAO
        GLES30.glBindVertexArray(moteVao)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, moteVbo)
        GLES30.glBufferData(
            GLES30.GL_ARRAY_BUFFER,
            ButterflySimulation.MAX_FAIRY_MOTES * 6 * ButterflySimulation.MOTE_FLOATS_PER_VERT * 4,
            null,
            GLES30.GL_STREAM_DRAW
        )

        val moteStride = ButterflySimulation.MOTE_FLOATS_PER_VERT * 4 // 28 bytes
        GLES30.glEnableVertexAttribArray(0)
        GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, moteStride, 0)
        GLES30.glEnableVertexAttribArray(1)
        GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT, false, moteStride, 8)
        GLES30.glEnableVertexAttribArray(2)
        GLES30.glVertexAttribPointer(2, 3, GLES30.GL_FLOAT, false, moteStride, 16)
        GLES30.glBindVertexArray(0)
    }

    private fun loadSpeciesTextureArray(): Int {
        val texIds = IntArray(1)
        GLES30.glGenTextures(1, texIds, 0)
        val tex = texIds[0]

        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D_ARRAY, tex)
        GLES30.glTexStorage3D(GLES30.GL_TEXTURE_2D_ARRAY, 9, GLES30.GL_RGBA8, 512, 512, 8)

        val rowBytes = 512 * 4
        val rawBuffer = ByteBuffer.allocateDirect(512 * 512 * 4).order(ByteOrder.nativeOrder())
        val flippedBuffer = ByteBuffer.allocateDirect(512 * 512 * 4).order(ByteOrder.nativeOrder())
        val rowScratch = ByteArray(rowBytes)

        val opts = BitmapFactory.Options().apply {
            inScaled = false
            inPremultiplied = false
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }

        for (layer in 0 until 8) {
            val bmp = context.assets.open("species_$layer.png").use { stream ->
                BitmapFactory.decodeStream(stream, null, opts)
            } ?: error("Missing asset species_$layer.png")

            rawBuffer.clear()
            bmp.copyPixelsToBuffer(rawBuffer)
            bmp.recycle()

            // Flip vertically so V=1.0 is head/apex and V=0.0 is tail
            flippedBuffer.clear()
            for (y in 511 downTo 0) {
                rawBuffer.position(y * rowBytes)
                rawBuffer.get(rowScratch, 0, rowBytes)
                flippedBuffer.put(rowScratch, 0, rowBytes)
            }
            flippedBuffer.position(0)

            GLES30.glTexSubImage3D(
                GLES30.GL_TEXTURE_2D_ARRAY,
                0,
                0, 0, layer,
                512, 512, 1,
                GLES30.GL_RGBA,
                GLES30.GL_UNSIGNED_BYTE,
                flippedBuffer
            )
        }

        GLES30.glTexParameteri(
            GLES30.GL_TEXTURE_2D_ARRAY,
            GLES30.GL_TEXTURE_MIN_FILTER,
            GLES30.GL_LINEAR_MIPMAP_LINEAR
        )
        GLES30.glTexParameteri(
            GLES30.GL_TEXTURE_2D_ARRAY,
            GLES30.GL_TEXTURE_MAG_FILTER,
            GLES30.GL_LINEAR
        )
        GLES30.glTexParameteri(
            GLES30.GL_TEXTURE_2D_ARRAY,
            GLES30.GL_TEXTURE_WRAP_S,
            GLES30.GL_CLAMP_TO_EDGE
        )
        GLES30.glTexParameteri(
            GLES30.GL_TEXTURE_2D_ARRAY,
            GLES30.GL_TEXTURE_WRAP_T,
            GLES30.GL_CLAMP_TO_EDGE
        )
        GLES30.glGenerateMipmap(GLES30.GL_TEXTURE_2D_ARRAY)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D_ARRAY, 0)

        return tex
    }

    private fun computeOrthoViewProj(orthoHalfW: Float, orthoHalfH: Float, out: FloatArray) {
        val near = 0.5f
        val far = 35.0f
        val camZ = 8.6f
        out[0] = 1.0f / orthoHalfW; out[1] = 0.0f; out[2] = 0.0f; out[3] = 0.0f
        out[4] = 0.0f; out[5] = 1.0f / orthoHalfH; out[6] = 0.0f; out[7] = 0.0f
        out[8] = 0.0f; out[9] = 0.0f; out[10] = -2.0f / (far - near); out[11] = 0.0f
        out[12] = 0.0f; out[13] = 0.0f; out[14] =
            -(far + near - 2.0f * camZ) / (far - near); out[15] = 1.0f
    }

    private fun compileShader(type: Int, src: String): Int {
        val sh = GLES30.glCreateShader(type)
        GLES30.glShaderSource(sh, src)
        GLES30.glCompileShader(sh)
        val status = IntArray(1)
        GLES30.glGetShaderiv(sh, GLES30.GL_COMPILE_STATUS, status, 0)
        if (status[0] == 0) {
            val log = GLES30.glGetShaderInfoLog(sh)
            GLES30.glDeleteShader(sh)
            error("Shader compile error: $log")
        }
        return sh
    }

    private fun createProgram(vsSrc: String, fsSrc: String): Int {
        val vs = compileShader(GLES30.GL_VERTEX_SHADER, vsSrc)
        val fs = compileShader(GLES30.GL_FRAGMENT_SHADER, fsSrc)
        val prog = GLES30.glCreateProgram()
        GLES30.glAttachShader(prog, vs)
        GLES30.glAttachShader(prog, fs)
        GLES30.glLinkProgram(prog)
        GLES30.glDeleteShader(vs)
        GLES30.glDeleteShader(fs)
        val status = IntArray(1)
        GLES30.glGetProgramiv(prog, GLES30.GL_LINK_STATUS, status, 0)
        if (status[0] == 0) {
            val log = GLES30.glGetProgramInfoLog(prog)
            GLES30.glDeleteProgram(prog)
            error("Program link error: $log")
        }
        return prog
    }

    companion object {
        // 26-Vertex, 20-Triangle 4-Wing Articulated Butterfly Mesh (from mesh.hpp)
        private val BUTTERFLY_VERTS = floatArrayOf(
            // Left Forewing (side = -1.0)
            -0.76f, 0.000f, 0.62f, 0.010f, 0.97f, -1.0f,
            -0.78f, 0.000f, -0.02f, 0.000f, 0.49f, -1.0f,
            -0.38f, 0.000f, 0.54f, 0.260f, 0.91f, -1.0f,
            -0.38f, 0.000f, -0.02f, 0.260f, 0.49f, -1.0f,
            -0.025f, 0.000f, 0.46f, 0.484f, 0.85f, -1.0f,
            -0.025f, 0.000f, -0.02f, 0.484f, 0.49f, -1.0f,

            // Left Hindwing (side = -2.0)
            -0.72f, -0.004f, 0.02f, 0.040f, 0.50f, -2.0f,
            -0.56f, -0.004f, -0.62f, 0.140f, 0.03f, -2.0f,
            -0.38f, -0.004f, 0.02f, 0.260f, 0.50f, -2.0f,
            -0.38f, -0.004f, -0.62f, 0.260f, 0.03f, -2.0f,
            -0.025f, -0.004f, 0.02f, 0.484f, 0.50f, -2.0f,
            -0.025f, -0.004f, -0.54f, 0.484f, 0.08f, -2.0f,

            // Right Forewing (side = +1.0)
            0.025f, 0.000f, 0.46f, 0.516f, 0.85f, 1.0f,
            0.025f, 0.000f, -0.02f, 0.516f, 0.49f, 1.0f,
            0.38f, 0.000f, 0.54f, 0.740f, 0.91f, 1.0f,
            0.38f, 0.000f, -0.02f, 0.740f, 0.49f, 1.0f,
            0.76f, 0.000f, 0.62f, 0.990f, 0.97f, 1.0f,
            0.78f, 0.000f, -0.02f, 1.000f, 0.49f, 1.0f,

            // Right Hindwing (side = +2.0)
            0.025f, -0.004f, 0.02f, 0.516f, 0.50f, 2.0f,
            0.025f, -0.004f, -0.54f, 0.516f, 0.08f, 2.0f,
            0.38f, -0.004f, 0.02f, 0.740f, 0.50f, 2.0f,
            0.38f, -0.004f, -0.62f, 0.740f, 0.03f, 2.0f,
            0.72f, -0.004f, 0.02f, 0.960f, 0.50f, 2.0f,
            0.56f, -0.004f, -0.62f, 0.860f, 0.03f, 2.0f,

            // Center Body Strip (side = 0.0)
            -0.028f, 0.020f, 0.46f, 0.484f, 0.85f, 0.0f,
            0.028f, 0.020f, -0.52f, 0.516f, 0.10f, 0.0f
        )

        private val BUTTERFLY_INDICES = shortArrayOf(
            0, 2, 1, 1, 2, 3, 2, 4, 3, 3, 4, 5,
            6, 8, 7, 7, 8, 9, 8, 10, 9, 9, 10, 11,
            12, 14, 13, 13, 14, 15, 14, 16, 15, 15, 16, 17,
            18, 20, 19, 19, 20, 21, 20, 22, 21, 21, 22, 23,
            4, 12, 11, 11, 12, 19, 24, 12, 11, 12, 25, 11
        )

        private const val BUTTERFLY_VS_SRC = """#version 300 es
precision highp float;

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in float a_side;

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
    vec3 fwd = normalize(i_forward);
    vec3 right = normalize(cross(fwd, i_up));
    vec3 up = cross(right, fwd);

    vec3 local = a_pos;
    vec3 localNormal = vec3(0.0, 1.0, 0.0);

    if (abs(a_side) > 0.5) {
        float wingSign   = sign(a_side);
        float span       = clamp(abs(local.x) / 0.78, 0.0, 1.0);
        float outerTip   = clamp((span - 0.45) / 0.55, 0.0, 1.0);
        float trail      = clamp((0.58 - local.z) / 1.20, 0.0, 1.0);
        float hindTail   = clamp((0.02 - local.z) / 0.64, 0.0, 1.0);

        float dorsalClap = clamp((i_wing_angle - 0.80) / 0.60, 0.0, 1.0);
        float peelCup    = dorsalClap * max(i_wing_flex, 0.0) * (0.20 + 0.80 * trail) * 0.32;

        float hindWave   = hindTail * (-0.04 + i_wing_flex * 0.55);
        float cCurveWhip = i_wing_flex * (span * 0.20 + outerTip * outerTip * 0.34);
        float chordLag   = trail * trail * (0.30 + 0.70 * span) * 0.48;
        float bankAsym   = -wingSign * right.y * (0.10 + 0.06 * span);
        float angle      = clamp(i_wing_angle + bankAsym + hindWave + peelCup + cCurveWhip + i_wing_flex * chordLag, 0.14, 1.48);

        float cleftDist  = local.z - 0.02;
        float feather    = i_wing_flex * (0.22 + 0.26 * span);
        float fc         = cos(feather);
        float fs         = sin(feather);

        float billow     = i_wing_flex * trail * span * 0.09;
        float sweepZ     = span * (i_wing_flex * 0.11 + sin(i_wing_angle * 2.0) * 0.035);
        float wy         = local.y + billow - cleftDist * fs;
        float wz         = 0.02 + cleftDist * fc + sweepZ;

        float rot = wingSign * angle;
        float c = cos(rot);
        float s = sin(rot);

        local = vec3(local.x * c - wy * s,
                     local.x * s + wy * c,
                     wz);

        float chordTilt = i_wing_flex * (0.30 + 0.45 * trail) * (0.35 + 0.65 * span);
        localNormal = normalize(vec3(-s, c, chordTilt));
    }

    float abdomenCounter = (abs(a_side) < 0.5 && local.z < 0.0) ? (-i_wing_flex * local.z * 0.24) : 0.0;

    float strokePitch = i_wing_flex * 0.44;
    float pc = cos(strokePitch);
    float ps = sin(strokePitch);
    local = vec3(local.x,
                 local.y * pc + local.z * ps + abdomenCounter,
                 -local.y * ps + local.z * pc);
    localNormal = normalize(vec3(localNormal.x,
                                 localNormal.y * pc + localNormal.z * ps,
                                 -localNormal.y * ps + localNormal.z * pc));

    local.y += (0.58 - i_wing_angle) * 0.19 + i_wing_flex * 0.08;

    float depthScale = clamp(1.0 + (i_pos.z + 1.8) * 0.14, 0.58, 1.38);
    vec3 worldOffset = (right * local.x + up * local.y + fwd * local.z) * (i_scale * depthScale);
    vec3 worldPos = i_pos + worldOffset;
    vec3 worldNormal = normalize(right * localNormal.x + up * localNormal.y + fwd * localNormal.z);

    v_uv = a_uv;
    v_normal = worldNormal;
    v_species = i_species;

    gl_Position = u_viewProj * vec4(worldPos, 1.0);
}
"""

        private const val BUTTERFLY_FS_SRC = """#version 300 es
precision mediump float;
precision mediump sampler2DArray;

in highp vec2 v_uv;
in mediump vec3 v_normal;
flat in uint v_species;

uniform sampler2DArray u_texArray;
uniform mediump vec3 u_surfaceToLight;
uniform mediump vec3 u_halfVec;
uniform mediump float u_sssBase;

out vec4 fragColor;

const vec3 BIO_TINT[8] = vec3[8](
    vec3(1.00, 0.50, 0.12),
    vec3(0.10, 0.65, 1.00),
    vec3(1.00, 0.80, 0.18),
    vec3(0.78, 0.34, 0.96),
    vec3(0.55, 0.92, 1.00),
    vec3(0.88, 0.18, 0.68),
    vec3(0.06, 0.88, 0.82),
    vec3(0.72, 0.28, 0.96)
);

const vec3 IRID_HUE_A[8] = vec3[8](
    vec3(1.00, 0.72, 0.20),
    vec3(0.08, 0.72, 1.00),
    vec3(1.00, 0.88, 0.30),
    vec3(1.00, 0.58, 0.22),
    vec3(0.48, 0.95, 1.00),
    vec3(1.00, 0.28, 0.55),
    vec3(0.08, 0.95, 0.78),
    vec3(0.82, 0.36, 1.00)
);

const vec3 IRID_HUE_B[8] = vec3[8](
    vec3(0.75, 0.22, 0.95),
    vec3(0.58, 0.18, 1.00),
    vec3(0.12, 0.88, 0.98),
    vec3(0.72, 0.24, 1.00),
    vec3(0.96, 0.62, 1.00),
    vec3(0.52, 0.20, 1.00),
    vec3(0.28, 0.35, 1.00),
    vec3(1.00, 0.76, 0.24)
);

const float IRID_STRENGTH[8] = float[8](
    0.34, 0.72, 0.38, 0.52, 0.46, 0.56, 0.64, 0.54
);

void main() {
    vec4 tex = texture(u_texArray, vec3(v_uv, float(v_species)));
    if (tex.a < 0.08) discard;

    vec3 dorsalN = normalize(v_normal);
    vec3 L = u_surfaceToLight;

    float facing = dorsalN.z;
    float dorsalFactor = smoothstep(-0.18, 0.18, facing);
    vec3 N = dorsalN * (facing >= 0.0 ? 1.0 : -1.0);

    float texLuma = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
    vec3 ventralAlbedo = mix(vec3(texLuma) * vec3(0.84, 0.78, 0.70), tex.rgb * 0.76, 0.60);
    vec3 baseAlbedo = mix(ventralAlbedo, tex.rgb, dorsalFactor);

    uint sp = v_species & 7u;
    vec3 bioTint  = BIO_TINT[sp];
    vec3 iridHueA = IRID_HUE_A[sp];
    vec3 iridHueB = IRID_HUE_B[sp];
    float iridStrength = IRID_STRENGTH[sp];

    float luma = dot(baseAlbedo, vec3(0.299, 0.587, 0.114));
    float darkPigment = 1.0 - smoothstep(0.05, 0.42, luma);
    float wingSpan = clamp(abs(v_uv.x - 0.5) * 2.2, 0.0, 1.0);
    baseAlbedo = mix(baseAlbedo, baseAlbedo * 0.55 + bioTint * (0.28 + 0.18 * wingSpan), darkPigment * 0.68);

    vec3 velvetBlack = mix(vec3(0.09, 0.09, 0.11), bioTint * 0.22, 0.35);
    baseAlbedo = max(baseAlbedo, velvetBlack);

    float newLuma = dot(baseAlbedo, vec3(0.299, 0.587, 0.114));
    baseAlbedo = clamp(mix(vec3(newLuma), baseAlbedo, mix(1.12, 1.42, dorsalFactor)), 0.0, 1.0);

    float nDotL = dot(N, L);
    float posNdotL = max(nDotL, 0.0);
    float wrapBack = max(-nDotL, 0.0);
    float ndotl = posNdotL + 0.58 * wrapBack;
    float hemi  = 0.56 + 0.16 * dorsalN.y;
    vec3 diffuse = baseAlbedo * (hemi + 0.78 * ndotl);

    float sssFactor = u_sssBase + wrapBack * 0.45;
    float membrane  = smoothstep(0.06, 0.55, newLuma);
    vec3 sss = mix(baseAlbedo, bioTint, 0.35) * sssFactor * (0.28 + 0.65 * membrane);

    float dorsalShimmer = mix(0.24, 1.00, dorsalFactor);
    float oneMinusNdotV = 1.0 - clamp(abs(N.z), 0.0, 1.0);
    float fresnel = oneMinusNdotV * sqrt(oneMinusNdotV);
    float edgeGlow = smoothstep(0.28, 0.88, wingSpan);
    vec3 iridTint = mix(iridHueA, iridHueB, clamp(fresnel * 0.75 + edgeGlow * 0.35, 0.0, 1.0));
    vec3 iridescence = iridTint * (iridStrength * dorsalShimmer) * (0.45 + 0.55 * ndotl) * (0.28 + 0.72 * max(fresnel, edgeGlow * 0.65));

    highp vec2 scaleGrid = v_uv * 115.0 + dorsalN.xy * 9.5;
    float sw = clamp(sin(scaleGrid.x) * sin(scaleGrid.y) * cos((scaleGrid.x - scaleGrid.y) * 0.7), 0.0, 1.0);
    float sw2 = sw * sw;
    float sparkle = (sw2 * sw2 * sw) * (0.35 + 0.65 * edgeGlow);
    vec3 scaleShimmer = mix(iridHueA, vec3(1.0, 0.97, 0.88), 0.55) * sparkle * (0.58 * dorsalShimmer);

    float nh = max(dot(N, u_halfVec), 0.0);
    float nh2 = nh * nh;
    float nh4 = nh2 * nh2;
    float nh8 = nh4 * nh4;
    float spec = (nh8 * nh8) * 0.25;

    vec3 color = diffuse + sss + iridescence + scaleShimmer + vec3(spec);
    color = color / (vec3(1.0) + color * 0.16);

    fragColor = vec4(color, 1.0);
}
"""

        private const val MOTE_VS_SRC = """#version 300 es
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
"""

        private const val MOTE_FS_SRC = """#version 300 es
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
"""
    }
}
