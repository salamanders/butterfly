package info.benjaminhill.butterflyscreen

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

class ButterflySimulation(
    initialCount: Int = ButterflyPrefs.DEFAULT_COUNT,
    initialZoom: Float = ButterflyPrefs.DEFAULT_ZOOM
) {
    companion object {
        const val MAX_BUTTERFLIES = ButterflyPrefs.MAX_COUNT
        const val INSTANCE_FLOATS = 13
        const val INSTANCE_STRIDE_BYTES = INSTANCE_FLOATS * 4 // 52 bytes
        const val MAX_FAIRY_MOTES = 200
        const val MOTE_FLOATS_PER_VERT = 7 // x, y, u, v, r, g, b

        private const val TWO_PI = (Math.PI * 2.0).toFloat()
        private const val PI_F = Math.PI.toFloat()
        private const val PHI = 1.618034f

        private const val STATE_FLAPPING = 0
        private const val STATE_GLIDING = 1

        private val SPECIES_SCALE =
            floatArrayOf(0.320f, 0.350f, 0.340f, 0.250f, 0.230f, 0.260f, 0.310f, 0.250f)
        private val SPECIES_FLAP_HZ = floatArrayOf(2.5f, 2.2f, 2.4f, 3.6f, 4.0f, 3.7f, 2.7f, 3.8f)
        private val SPECIES_GLIDE_RAD =
            floatArrayOf(0.82f, 0.78f, 0.84f, 0.98f, 1.04f, 0.96f, 0.86f, 1.00f)

        private val MOTE_COLORS = arrayOf(
            floatArrayOf(1.00f, 0.68f, 0.22f), // 0: Monarch
            floatArrayOf(0.25f, 0.78f, 1.00f), // 1: Blue Morpho
            floatArrayOf(1.00f, 0.88f, 0.32f), // 2: Tiger Swallowtail
            floatArrayOf(0.88f, 0.45f, 1.00f), // 3: Question Mark
            floatArrayOf(0.62f, 0.96f, 1.00f), // 4: Cabbage White
            floatArrayOf(1.00f, 0.35f, 0.72f), // 5: Red Admiral
            floatArrayOf(0.18f, 0.96f, 0.85f), // 6: Pipevine Swallowtail
            floatArrayOf(0.82f, 0.42f, 1.00f)  // 7: Buckeye
        )
    }

    private class Agent(
        var px: Float,
        var py: Float,
        var pz: Float,
        var vx: Float,
        var vy: Float,
        var vz: Float,
        var fx: Float,
        var fy: Float,
        var fz: Float,
        var ux: Float,
        var uy: Float,
        var uz: Float,
        var phase: Float,
        val flapFreq: Float,
        var wingAngle: Float,
        var wingFlex: Float,
        var state: Int,
        var stateTimer: Float,
        val noiseSeed: Float,
        val baseScale: Float,
        val speciesId: Int,
        var rngState: Int
    ) {
        fun next01(): Float {
            val old = rngState
            rngState = old * 747796405 + -1403630843 // 2891336453U as signed Int
            val shift = (old ushr 28) + 4
            val word = ((old ushr shift) xor old) * 277803737
            val res = (word ushr 22) xor word
            return (res and 0x00ffffff).toFloat() / 0x01000000.toFloat()
        }

        fun range(lo: Float, hi: Float): Float = lo + (hi - lo) * next01()
    }

    private class FairyMote {
        var px = 0f
        var py = 0f
        var pz = 0f
        var vx = 0f
        var vy = 0f
        var vz = 0f
        var age = 0f
        var maxAge = 2f
        var sizePx = 8f
        var phase = 0f
        var r = 1f
        var g = 1f
        var b = 1f
    }

    var activeCount: Int = initialCount.coerceIn(ButterflyPrefs.MIN_COUNT, ButterflyPrefs.MAX_COUNT)
    var zoomLevel: Float = initialZoom.coerceIn(ButterflyPrefs.MIN_ZOOM, ButterflyPrefs.MAX_ZOOM)

    private var simTime = 0.0f
    private val turbScratch = FloatArray(3)
    private val upScratch = FloatArray(3)
    private val sortedIndices = IntArray(MAX_BUTTERFLIES) { it }

    private val agents: Array<Agent>
    private val motes: Array<FairyMote>
    private var moteRngState = 77777

    private val instanceArray = FloatArray(MAX_BUTTERFLIES * INSTANCE_FLOATS)
    val instanceBuffer: FloatBuffer =
        ByteBuffer.allocateDirect(instanceArray.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()

    private val moteArray = FloatArray(MAX_FAIRY_MOTES * 6 * MOTE_FLOATS_PER_VERT)
    val moteFloatBuffer: FloatBuffer =
        ByteBuffer.allocateDirect(moteArray.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()

    private fun nextMoteRange(lo: Float, hi: Float): Float {
        val old = moteRngState
        moteRngState = old * 747796405 + -1403630843
        val shift = (old ushr 28) + 4
        val word = ((old ushr shift) xor old) * 277803737
        val res = (word ushr 22) xor word
        val r01 = (res and 0x00ffffff).toFloat() / 0x01000000.toFloat()
        return lo + (hi - lo) * r01
    }

    init {
        agents = Array(MAX_BUTTERFLIES) { i ->
            val sp = i % 8
            val a = Agent(
                px = 0f, py = 0f, pz = 0f,
                vx = 0f, vy = 0f, vz = 0f,
                fx = 0f, fy = 0f, fz = 1f,
                ux = 0f, uy = 1f, uz = 0f,
                phase = 0f,
                flapFreq = 1f,
                wingAngle = 0.56f,
                wingFlex = 0.0f,
                state = STATE_FLAPPING,
                stateTimer = 1f,
                noiseSeed = 0f,
                baseScale = SPECIES_SCALE[sp],
                speciesId = sp,
                rngState = 1337 + i * 9781
            )
            val headingAng = a.range(0.0f, TWO_PI)
            var dx = cos(headingAng)
            var dy = a.range(-0.35f, 0.35f)
            var dz = sin(headingAng) * 0.35f
            val dlen = sqrt(dx * dx + dy * dy + dz * dz).let { if (it < 1e-5f) 1.0f else it }
            dx /= dlen
            dy /= dlen
            dz /= dlen

            val spd = a.range(0.32f, 0.58f)
            computeRolledUp(dx, dy, dz, a.range(-0.45f, 0.75f), upScratch)

            Agent(
                px = a.range(-1.40f, 1.40f),
                py = a.range(-2.60f, 2.60f),
                pz = a.range(-4.20f, 1.00f),
                vx = dx * spd,
                vy = dy * spd,
                vz = dz * spd,
                fx = dx,
                fy = dy,
                fz = dz,
                ux = upScratch[0],
                uy = upScratch[1],
                uz = upScratch[2],
                phase = a.range(0.0f, TWO_PI),
                flapFreq = SPECIES_FLAP_HZ[sp] * a.range(0.92f, 1.08f),
                wingAngle = 0.56f,
                wingFlex = 0.0f,
                state = if (a.next01() < 0.45f) STATE_GLIDING else STATE_FLAPPING,
                stateTimer = a.range(0.6f, 2.4f),
                noiseSeed = a.range(0.0f, 100.0f),
                baseScale = SPECIES_SCALE[sp] * a.range(0.92f, 1.08f),
                speciesId = sp,
                rngState = a.rngState
            ).also { writeInstance(i, it) }
        }
        instanceBuffer.position(0)
        instanceBuffer.put(instanceArray, 0, activeCount * INSTANCE_FLOATS)
        instanceBuffer.position(0)

        motes = Array(MAX_FAIRY_MOTES) { m ->
            FairyMote().also {
                spawnMoteOnButterfly(it, m, m % activeCount, initialRandomAge = true)
            }
        }
    }

    private fun writeInstance(idx: Int, a: Agent) {
        val base = idx * INSTANCE_FLOATS
        val out = instanceArray
        out[base + 0] = a.px
        out[base + 1] = a.py
        out[base + 2] = a.pz
        out[base + 3] = a.fx
        out[base + 4] = a.fy
        out[base + 5] = a.fz
        out[base + 6] = a.ux
        out[base + 7] = a.uy
        out[base + 8] = a.uz
        out[base + 9] = a.wingAngle
        out[base + 10] = a.wingFlex
        out[base + 11] = a.baseScale * zoomLevel
        out[base + 12] = Float.fromBits(a.speciesId)
    }

    private fun spawnMoteOnButterfly(
        m: FairyMote,
        mIdx: Int,
        bIdx: Int,
        initialRandomAge: Boolean
    ) {
        val b = agents[bIdx]
        var rx = b.fy * b.uz - b.fz * b.uy
        var ry = b.fz * b.ux - b.fx * b.uz
        var rz = b.fx * b.uy - b.fy * b.ux
        val rlen = sqrt(rx * rx + ry * ry + rz * rz).let { if (it < 1e-5f) 1.0f else it }
        rx /= rlen
        ry /= rlen
        rz /= rlen

        val side = if ((mIdx and 1) != 0) 0.70f else -0.70f
        val scaledSpan = side * b.baseScale * zoomLevel
        m.px = b.px + rx * scaledSpan + nextMoteRange(-0.06f, 0.06f)
        m.py = b.py + ry * scaledSpan + nextMoteRange(-0.06f, 0.06f)
        m.pz = b.pz + rz * scaledSpan + nextMoteRange(-0.05f, 0.05f)

        m.vx = b.vx * 0.18f + nextMoteRange(-0.10f, 0.10f)
        m.vy = b.vy * 0.18f + nextMoteRange(-0.06f, 0.12f)
        m.vz = b.vz * 0.18f + nextMoteRange(-0.05f, 0.05f)

        m.maxAge = nextMoteRange(1.50f, 2.90f)
        m.age = if (initialRandomAge) nextMoteRange(0.0f, m.maxAge) else 0.0f
        m.sizePx = nextMoteRange(5.5f, 11.5f)
        m.phase = nextMoteRange(0.0f, TWO_PI)

        val col = MOTE_COLORS[b.speciesId and 7]
        m.r = col[0]
        m.g = col[1]
        m.b = col[2]
    }

    fun step(rawDt: Float, orthoHalfW: Float, orthoHalfH: Float) {
        val dt = rawDt.coerceIn(0.001f, 0.033f)
        simTime += dt
        if (simTime > 86400.0f) simTime -= 86400.0f

        val count = activeCount.coerceIn(ButterflyPrefs.MIN_COUNT, MAX_BUTTERFLIES)

        // Adapt bounds smoothly to both portrait and landscape phone screens
        val margin = 0.18f * zoomLevel.coerceAtMost(1.8f)
        val boundMaxX = max(0.75f, orthoHalfW - margin)
        val meadowAmpX = max(0.55f, orthoHalfW - margin * 1.6f)
        val boundMaxY = max(0.75f, orthoHalfH - margin)
        val boundMinY = -boundMaxY
        val meadowAmpY = max(0.55f, orthoHalfH - margin * 1.6f)

        for (i in 0 until count) {
            val a = agents[i]

            // Soft boundary return force keeping butterflies inside full screen
            var boundX = 0.0f
            var boundY = 0.0f
            var boundZ = 0.0f
            if (a.px > boundMaxX) boundX -= (a.px - boundMaxX) * 1.8f
            if (a.px < -boundMaxX) boundX -= (a.px + boundMaxX) * 1.8f
            if (a.py > boundMaxY) boundY -= (a.py - boundMaxY) * 2.0f
            if (a.py < boundMinY) boundY -= (a.py - boundMinY) * 2.0f
            if (a.pz > 1.20f) boundZ -= (a.pz - 1.20f) * 1.8f
            if (a.pz < -4.40f) boundZ -= (a.pz + 4.40f) * 1.4f

            // Wandering full-screen meadow home
            val fi = i.toFloat()
            val homeX = meadowAmpX * sin(simTime * 0.21f + a.noiseSeed + fi * 2.39996f)
            val homeY = meadowAmpY * cos(simTime * 0.27f + a.noiseSeed * 1.3f + fi * PHI)
            val homeZ = -1.60f + 1.40f * sin(simTime * 0.19f + a.noiseSeed * 0.7f + fi)

            val meadowX = (homeX - a.px) * 0.45f
            val meadowY = (homeY - a.py) * 0.45f
            val meadowZ = (homeZ - a.pz) * 0.45f

            evalCurlTurbulence(a.px, a.py, a.pz, simTime, a.noiseSeed, turbScratch)
            val soaringSpecies =
                (a.speciesId == 0 || a.speciesId == 1 || a.speciesId == 2 || a.speciesId == 6)
            val turbMul = if (soaringSpecies) 0.72f else 1.05f

            val accelX = meadowX + boundX + turbScratch[0] * turbMul
            var accelY = meadowY + boundY + turbScratch[1] * turbMul
            val accelZ = meadowZ + boundZ + turbScratch[2] * turbMul

            val boundLenSq = boundX * boundX + boundY * boundY + boundZ * boundZ

            if (a.state == STATE_GLIDING && boundLenSq > 0.01f) {
                a.state = STATE_FLAPPING
                a.phase = 2.38f
                a.stateTimer = a.range(0.90f, 1.60f)
            }

            a.stateTimer -= dt
            if (a.state == STATE_GLIDING) {
                if (a.stateTimer <= 0.0f) {
                    a.state = STATE_FLAPPING
                    a.phase = 2.38f
                    a.stateTimer =
                        if (soaringSpecies) a.range(1.00f, 1.75f) else a.range(0.65f, 1.25f)
                }
            } else {
                if (a.stateTimer <= 0.0f && sin(a.phase) > 0.35f && boundLenSq <= 0.01f) {
                    val glideProb = if (soaringSpecies) 0.85f else 0.60f
                    if (a.next01() < glideProb) {
                        a.state = STATE_GLIDING
                        a.stateTimer =
                            if (soaringSpecies) a.range(1.60f, 3.30f) else a.range(
                                0.55f,
                                1.20f
                            )
                        val horizSpd = sqrt(a.vx * a.vx + a.vz * a.vz)
                        val targetHoriz =
                            horizSpd.coerceIn(0.24f, if (soaringSpecies) 0.44f else 0.52f)
                        val hx =
                            if (horizSpd > 1e-4f) (a.vx / horizSpd) else (if (a.fx >= 0.0f) 1.0f else -1.0f)
                        val hz = if (horizSpd > 1e-4f) (a.vz / horizSpd) else 0.0f
                        a.vx = hx * targetHoriz
                        a.vz = hz * targetHoriz
                        a.vy = if (soaringSpecies) -0.17f else -0.20f
                    } else {
                        a.stateTimer = a.range(0.80f, 1.50f)
                    }
                }
            }

            val surge: Float
            if (a.state == STATE_FLAPPING) {
                a.phase += a.flapFreq * TWO_PI * dt
                if (a.phase > 100.0f * TWO_PI) a.phase -= 100.0f * TWO_PI

                // Asymmetric "Snap-and-Hold" stroke curve
                val skewPhase = a.phase - 0.32f * sin(a.phase)
                val raw01 = 0.5f + 0.5f * sin(skewPhase)
                val s01 = raw01 * sqrt(raw01)
                val targetAngle = 0.20f + s01 * 1.20f

                val downstrokePhase = -cos(skewPhase) * (1.0f - 0.22f * cos(a.phase))
                val targetFlex = downstrokePhase * 0.44f
                val blend = min(1.0f, dt * 18.0f)
                a.wingAngle = lerp(a.wingAngle, targetAngle, blend)
                a.wingFlex = lerp(a.wingFlex, targetFlex, blend)

                // Phase-delayed (~35 deg) body counter-bounce & forward surge
                val delayedBounce = -cos(a.phase - 0.58f)
                val climbBias = if (soaringSpecies) 0.34f else 0.16f
                accelY += climbBias + delayedBounce * (if (soaringSpecies) 0.56f else 0.74f)
                surge = 1.0f + (if (soaringSpecies) 0.36f else 0.46f) * delayedBounce

                a.vx += accelX * dt
                a.vy += accelY * dt
                a.vz += accelZ * dt
                a.vy = a.vy.coerceIn(if (soaringSpecies) -0.14f else -0.24f, 0.64f)

                val spd = sqrt(a.vx * a.vx + a.vy * a.vy + a.vz * a.vz)
                val minSpd = if (soaringSpecies) 0.28f else 0.36f
                val maxSpd = if (soaringSpecies) 0.58f else 0.68f
                if (spd > 1e-5f) {
                    if (spd < minSpd) {
                        val s = minSpd / spd
                        a.vx *= s; a.vy *= s; a.vz *= s
                    } else if (spd > maxSpd) {
                        val s = maxSpd / spd
                        a.vx *= s; a.vy *= s; a.vz *= s
                    }
                }

                // Align forward heading
                var dfx = a.vx
                var dfy = a.vy * 0.85f
                var dfz = a.vz * 0.45f
                val dflen = sqrt(dfx * dfx + dfy * dfy + dfz * dfz)
                if (dflen > 1e-5f) {
                    dfx /= dflen; dfy /= dflen; dfz /= dflen
                } else {
                    dfx = a.fx; dfy = a.fy; dfz = a.fz
                }
                val fBlend = min(1.0f, dt * (if (soaringSpecies) 6.5f else 9.0f))
                a.fx = lerp(a.fx, dfx, fBlend)
                a.fy = lerp(a.fy, dfy, fBlend)
                a.fz = lerp(a.fz, dfz, fBlend)
                val flen = sqrt(a.fx * a.fx + a.fy * a.fy + a.fz * a.fz).let { if (it < 1e-5f) 1.0f else it }
                a.fx /= flen; a.fy /= flen; a.fz /= flen

                // 3D roll around forward axis
                val baseRoll = 0.16f + 0.48f * sin(simTime * 0.72f + a.noiseSeed * 1.9f)
                val turnRoll = (accelZ * 0.55f + accelX * 0.25f).coerceIn(-0.48f, 0.48f)
                val flapRock = 0.24f * sin(a.phase + a.noiseSeed)
                val flapRoll = (baseRoll + turnRoll + flapRock).coerceIn(-0.62f, 0.85f)

                computeRolledUp(a.fx, a.fy, a.fz, flapRoll, upScratch)
                val upBlend = min(1.0f, dt * 6.5f)
                a.ux = lerp(a.ux, upScratch[0], upBlend)
                a.uy = lerp(a.uy, upScratch[1], upBlend)
                a.uz = lerp(a.uz, upScratch[2], upBlend)
                val ulen = sqrt(a.ux * a.ux + a.uy * a.uy + a.uz * a.uz).let { if (it < 1e-5f) 1.0f else it }
                a.ux /= ulen; a.uy /= ulen; a.uz /= ulen
            } else {
                val damp = min(1.0f, dt * 8.0f)
                val targetGlideAngle =
                    SPECIES_GLIDE_RAD[a.speciesId and 7] + 0.04f * sin(a.noiseSeed)
                a.wingAngle = lerp(a.wingAngle, targetGlideAngle, damp)
                a.wingFlex = lerp(a.wingFlex, -0.04f, damp)
                surge = 0.95f

                if (a.vy > -0.14f) {
                    a.vy = if (soaringSpecies) -0.17f else -0.20f
                }
                val spd = sqrt(a.vx * a.vx + a.vy * a.vy + a.vz * a.vz)
                val minGlide = if (soaringSpecies) 0.26f else 0.32f
                val maxGlide = if (soaringSpecies) 0.50f else 0.58f
                val glideSpd = (spd * (1.0f - 0.12f * dt)).coerceIn(minGlide, maxGlide)
                if (spd > 1e-5f) {
                    val s = glideSpd / spd
                    a.vx *= s; a.vy *= s; a.vz *= s
                }

                var gfx = a.vx
                var gfy = a.vy * 0.85f
                var gfz = a.vz * 0.45f
                val gflen = sqrt(gfx * gfx + gfy * gfy + gfz * gfz)
                if (gflen > 1e-5f) {
                    gfx /= gflen; gfy /= gflen; gfz /= gfz
                } else {
                    gfx = a.fx; gfy = a.fy; gfz = a.fz
                }
                val gBlend = min(1.0f, dt * 8.0f)
                a.fx = lerp(a.fx, gfx, gBlend)
                a.fy = lerp(a.fy, gfy, gBlend)
                a.fz = lerp(a.fz, gfz, gBlend)
                val flen = sqrt(a.fx * a.fx + a.fy * a.fy + a.fz * a.fz).let { if (it < 1e-5f) 1.0f else it }
                a.fx /= flen; a.fy /= flen; a.fz /= flen

                val depthBank = a.vz * 0.85f
                val thermalDrift = 0.15f +
                        0.54f * sin(simTime * 1.05f + a.noiseSeed * 2.3f) +
                        0.26f * cos(simTime * 1.75f + a.noiseSeed * 0.9f)
                val glideRoll = (depthBank + thermalDrift).coerceIn(-0.58f, 0.85f)

                computeRolledUp(a.fx, a.fy, a.fz, glideRoll, upScratch)
                val upBlend = min(1.0f, dt * 5.0f)
                a.ux = lerp(a.ux, upScratch[0], upBlend)
                a.uy = lerp(a.uy, upScratch[1], upBlend)
                a.uz = lerp(a.uz, upScratch[2], upBlend)
                val ulen = sqrt(a.ux * a.ux + a.uy * a.uy + a.uz * a.uz).let { if (it < 1e-5f) 1.0f else it }
                a.ux /= ulen; a.uy /= ulen; a.uz /= ulen
            }

            a.px += a.vx * (dt * surge)
            a.py += a.vy * (dt * surge)
            a.pz += a.vz * (dt * surge)
            sortedIndices[i] = i
        }

        // Insertion sort by Z depth (back-to-front: ascending pz from -4.4 to +1.2)
        for (i in 1 until count) {
            val idx = sortedIndices[i]
            val z = agents[idx].pz
            var j = i - 1
            while (j >= 0 && agents[sortedIndices[j]].pz > z) {
                sortedIndices[j + 1] = sortedIndices[j]
                j--
            }
            sortedIndices[j + 1] = idx
        }
        for (i in 0 until count) {
            writeInstance(i, agents[sortedIndices[i]])
        }
        instanceBuffer.position(0)
        instanceBuffer.put(instanceArray, 0, count * INSTANCE_FLOATS)
        instanceBuffer.position(0)

        // Step wingtip fairy-dust motes
        for (m in 0 until MAX_FAIRY_MOTES) {
            val mote = motes[m]
            mote.age += dt
            if (mote.age >= mote.maxAge) {
                val bIdx = (m * 7 + (simTime * 17.0f).toInt()) % count
                spawnMoteOnButterfly(mote, m, bIdx, initialRandomAge = false)
            } else {
                val swirlX = 0.08f * sin(simTime * 1.4f + mote.phase)
                val swirlY = 0.06f * cos(simTime * 1.1f + mote.phase * 1.3f)
                mote.px += (mote.vx + swirlX) * dt
                mote.py += (mote.vy + swirlY) * dt
                mote.pz += mote.vz * dt
                val drag = 1.0f - 0.65f * dt
                mote.vx *= drag
                mote.vy *= drag
                mote.vz *= drag
            }
        }
    }

    fun buildFairyMoteVertices(
        orthoHalfW: Float,
        orthoHalfH: Float,
        screenW: Float,
        screenH: Float
    ): Int {
        var vFloats = 0
        val out = moteArray

        for (i in 0 until MAX_FAIRY_MOTES) {
            val m = motes[i]
            val life01 = (m.age / m.maxAge).coerceIn(0.0f, 1.0f)
            val env = sin(life01 * PI_F)
            val twinkle = 0.62f + 0.38f * sin(simTime * 5.2f + m.phase)
            val brightness = env * twinkle
            if (brightness < 0.05f) continue

            val px = ((m.px / orthoHalfW) + 1.0f) * 0.5f * screenW
            val py = (1.0f - (m.py / orthoHalfH)) * 0.5f * screenH
            if (px < -16.0f || px > screenW + 16.0f || py < -16.0f || py > screenH + 16.0f) continue

            val depthScale = (1.0f + (m.pz + 1.8f) * 0.14f).coerceIn(0.58f, 1.38f)
            val rad = m.sizePx * depthScale * (0.55f + 0.45f * brightness)
            val cr = m.r * brightness
            val cg = m.g * brightness
            val cb = m.b * brightness

            val x0 = px - rad
            val x1 = px + rad
            val y0 = py - rad
            val y1 = py + rad

            // Tri 1
            out[vFloats++] = x0; out[vFloats++] = y0; out[vFloats++] = -1f; out[vFloats++] =
                -1f; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb
            out[vFloats++] = x1; out[vFloats++] = y0; out[vFloats++] = 1f; out[vFloats++] =
                -1f; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb
            out[vFloats++] = x1; out[vFloats++] = y1; out[vFloats++] = 1f; out[vFloats++] =
                1f; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb
            // Tri 2
            out[vFloats++] = x0; out[vFloats++] = y0; out[vFloats++] = -1f; out[vFloats++] =
                -1f; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb
            out[vFloats++] = x1; out[vFloats++] = y1; out[vFloats++] = 1f; out[vFloats++] =
                1f; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb
            out[vFloats++] = x0; out[vFloats++] = y1; out[vFloats++] = -1f; out[vFloats++] =
                1f; out[vFloats++] = cr; out[vFloats++] = cg; out[vFloats++] = cb
        }

        moteFloatBuffer.position(0)
        moteFloatBuffer.put(out, 0, vFloats)
        moteFloatBuffer.position(0)
        return vFloats / MOTE_FLOATS_PER_VERT
    }

    private fun lerp(a: Float, b: Float, t: Float): Float = a + (b - a) * t

    private fun computeRolledUp(
        fx: Float,
        fy: Float,
        fz: Float,
        rollToCamRad: Float,
        out: FloatArray
    ) {
        var sx = -fx * fy
        var sy = 1.0f - fy * fy
        var sz = -fz * fy
        val slen = sqrt(sx * sx + sy * sy + sz * sz)
        if (slen < 1e-4f) {
            sx = 0.0f; sy = 1.0f; sz = 0.0f
        } else {
            sx /= slen; sy /= slen; sz /= slen
        }

        val dotCamFwd = fz
        val dotCamSky = sz
        var cx = -fx * dotCamFwd - sx * dotCamSky
        var cy = -fy * dotCamFwd - sy * dotCamSky
        var cz = 1.0f - fz * dotCamFwd - sz * dotCamSky
        val clen = sqrt(cx * cx + cy * cy + cz * cz)
        if (clen < 1e-4f) {
            cx = 0.0f; cy = 0.0f; cz = 1.0f
        } else {
            cx /= clen; cy /= clen; cz /= clen
        }

        val c = cos(rollToCamRad)
        val s = sin(rollToCamRad)
        val ux = sx * c + cx * s
        val uy = sy * c + cy * s
        val uz = sz * c + cz * s
        val ulen = sqrt(ux * ux + uy * uy + uz * uz)
        if (ulen < 1e-4f) {
            out[0] = 0.0f; out[1] = 0.70f; out[2] = 0.71f
        } else {
            out[0] = ux / ulen; out[1] = uy / ulen; out[2] = uz / ulen
        }
    }

    private fun evalCurlTurbulence(
        px: Float,
        py: Float,
        pz: Float,
        t: Float,
        seed: Float,
        out: FloatArray
    ) {
        val sx = px * 0.75f + t * 0.32f + seed
        val sy = py * 0.75f - t * 0.26f + seed * 1.7f
        val sz = pz * 0.75f + t * 0.29f - seed * 1.3f

        out[0] = sin(sy) * cos(sz) + 0.45f * sin(1.8f * sy + 1.1f * sz)
        out[1] = (sin(sz) * cos(sx) + 0.45f * sin(1.8f * sz + 1.1f * sx)) * 0.65f
        out[2] = (sin(sx) * cos(sy) + 0.45f * sin(1.8f * sx + 1.1f * sy)) * 0.75f
    }
}
