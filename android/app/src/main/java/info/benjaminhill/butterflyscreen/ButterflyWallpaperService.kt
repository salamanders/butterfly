package info.benjaminhill.butterflyscreen

import android.content.Context
import android.content.SharedPreferences
import android.opengl.GLSurfaceView
import android.service.wallpaper.WallpaperService
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceHolder

class ButterflyWallpaperService : WallpaperService() {

    override fun onCreateEngine(): Engine = ButterflyEngine()

    private inner class ButterflyEngine : Engine(),
        SharedPreferences.OnSharedPreferenceChangeListener,
        Choreographer.FrameCallback {
        private var glSurfaceView: WallpaperGLSurfaceView? = null
        private var renderer: ButterflyRenderer? = null
        private var isRunning = false
        private var fixedW = 0
        private var fixedH = 0

        override fun onCreate(surfaceHolder: SurfaceHolder) {
            super.onCreate(surfaceHolder)
            setTouchEventsEnabled(false)

            val context = this@ButterflyWallpaperService
            val butterflyRenderer = ButterflyRenderer(context)
            renderer = butterflyRenderer

            glSurfaceView = WallpaperGLSurfaceView(context).apply {
                setEGLContextClientVersion(3)
                setEGLConfigChooser(8, 8, 8, 0, 16, 0)
                preserveEGLContextOnPause = true
                setRenderer(butterflyRenderer)
                renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
            }

            ButterflyPrefs.prefs(context).registerOnSharedPreferenceChangeListener(this)
        }

        override fun doFrame(frameTimeNanos: Long) {
            if (!isRunning) return
            glSurfaceView?.requestRender()
            Choreographer.getInstance().postFrameCallback(this)
        }

        override fun onDesiredSizeChanged(desiredWidth: Int, desiredHeight: Int) {
            super.onDesiredSizeChanged(desiredWidth, desiredHeight)
            applyHalfResSize(surfaceHolder, desiredWidth, desiredHeight)
        }

        override fun onSurfaceChanged(
            holder: SurfaceHolder,
            format: Int,
            width: Int,
            height: Int
        ) {
            super.onSurfaceChanged(holder, format, width, height)
            applyHalfResSize(holder, width, height)
            if (holder.surface.isValid) {
                holder.surface.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
            }
        }

        private fun applyHalfResSize(holder: SurfaceHolder, width: Int, height: Int) {
            if (width <= 0 || height <= 0) return
            if (width == fixedW && height == fixedH) return
            val targetW = (width / 2).coerceAtLeast(1)
            val targetH = (height / 2).coerceAtLeast(1)
            fixedW = targetW
            fixedH = targetH
            holder.setFixedSize(targetW, targetH)
        }

        override fun onVisibilityChanged(visible: Boolean) {
            super.onVisibilityChanged(visible)
            val view = glSurfaceView ?: return
            if (visible) {
                syncPrefs()
                view.onResume()
                if (!isRunning) {
                    isRunning = true
                    Choreographer.getInstance().postFrameCallback(this)
                }
            } else {
                isRunning = false
                Choreographer.getInstance().removeFrameCallback(this)
                view.onPause()
            }
        }

        override fun onSharedPreferenceChanged(
            sharedPreferences: SharedPreferences?,
            key: String?
        ) {
            syncPrefs()
        }

        private fun syncPrefs() {
            val context = this@ButterflyWallpaperService
            renderer?.let {
                it.butterflyCount = ButterflyPrefs.getCount(context)
                it.zoomLevel = ButterflyPrefs.getZoom(context)
            }
        }

        override fun onDestroy() {
            isRunning = false
            Choreographer.getInstance().removeFrameCallback(this)
            ButterflyPrefs.prefs(this@ButterflyWallpaperService)
                .unregisterOnSharedPreferenceChangeListener(this)
            glSurfaceView?.onWallpaperDestroy()
            glSurfaceView = null
            renderer = null
            super.onDestroy()
        }

        private inner class WallpaperGLSurfaceView(context: Context) : GLSurfaceView(context) {
            override fun getHolder(): SurfaceHolder = this@ButterflyEngine.surfaceHolder

            fun onWallpaperDestroy() {
                super.onDetachedFromWindow()
            }
        }
    }
}
