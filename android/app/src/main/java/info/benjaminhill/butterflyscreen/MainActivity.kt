package info.benjaminhill.butterflyscreen

import android.app.WallpaperManager
import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Intent
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.Surface
import android.view.SurfaceHolder
import android.view.ViewGroup
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updateLayoutParams
import com.google.android.material.button.MaterialButton
import com.google.android.material.card.MaterialCardView
import com.google.android.material.slider.Slider
import java.util.Locale
import kotlin.math.roundToInt

class MainActivity : AppCompatActivity() {

    private lateinit var glSurfaceView: GLSurfaceView
    private lateinit var renderer: ButterflyRenderer

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        setContentView(R.layout.activity_main)

        val settingsCard = findViewById<MaterialCardView>(R.id.settingsCard)
        val baseBottomMarginPx = (24f * resources.displayMetrics.density).roundToInt()
        ViewCompat.setOnApplyWindowInsetsListener(settingsCard) { view, windowInsets ->
            val insets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            view.updateLayoutParams<ViewGroup.MarginLayoutParams> {
                bottomMargin = baseBottomMarginPx + insets.bottom
            }
            WindowInsetsCompat.CONSUMED
        }

        glSurfaceView = findViewById(R.id.glSurfaceView)
        renderer = ButterflyRenderer(this)
        glSurfaceView.apply {
            setEGLContextClientVersion(3)
            setEGLConfigChooser(8, 8, 8, 0, 16, 0)
            preserveEGLContextOnPause = true
            setRenderer(renderer)
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
            holder.addCallback(object : SurfaceHolder.Callback {
                override fun surfaceCreated(holder: SurfaceHolder) {
                    if (holder.surface.isValid) {
                        holder.surface.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
                    }
                }

                override fun surfaceChanged(
                    holder: SurfaceHolder,
                    format: Int,
                    width: Int,
                    height: Int
                ) {
                    if (holder.surface.isValid) {
                        holder.surface.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
                    }
                }

                override fun surfaceDestroyed(holder: SurfaceHolder) = Unit
            })
        }

        val countValueText = findViewById<TextView>(R.id.countValueText)
        val countSlider = findViewById<Slider>(R.id.countSlider)
        val zoomValueText = findViewById<TextView>(R.id.zoomValueText)
        val zoomSlider = findViewById<Slider>(R.id.zoomSlider)
        val setWallpaperButton = findViewById<MaterialButton>(R.id.setWallpaperButton)

        val initialCount = ((ButterflyPrefs.getCount(this) / 5) * 5)
            .coerceIn(ButterflyPrefs.MIN_COUNT, ButterflyPrefs.MAX_COUNT)
        val initialZoom = ((ButterflyPrefs.getZoom(this) * 10f).roundToInt() / 10f)
            .coerceIn(ButterflyPrefs.MIN_ZOOM, ButterflyPrefs.MAX_ZOOM)

        countSlider.value = initialCount.toFloat()
        countValueText.text = String.format(Locale.US, "%d", initialCount)

        zoomSlider.value = initialZoom
        zoomValueText.text = String.format(Locale.US, "%.1f×", initialZoom)

        countSlider.addOnChangeListener { _, value, _ ->
            val count = ((value / 5f).roundToInt() * 5)
                .coerceIn(ButterflyPrefs.MIN_COUNT, ButterflyPrefs.MAX_COUNT)
            countValueText.text = String.format(Locale.US, "%d", count)
            renderer.butterflyCount = count
            ButterflyPrefs.setCount(this, count)
        }

        zoomSlider.addOnChangeListener { _, value, _ ->
            val zoom = ((value * 10f).roundToInt() / 10f)
                .coerceIn(ButterflyPrefs.MIN_ZOOM, ButterflyPrefs.MAX_ZOOM)
            zoomValueText.text = String.format(Locale.US, "%.1f×", zoom)
            renderer.zoomLevel = zoom
            ButterflyPrefs.setZoom(this, zoom)
        }

        setWallpaperButton.setOnClickListener {
            openLiveWallpaperPicker()
        }
    }

    private fun openLiveWallpaperPicker() {
        val component = ComponentName(this, ButterflyWallpaperService::class.java)
        val intent = Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER).apply {
            putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT, component)
        }
        try {
            startActivity(intent)
        } catch (_: ActivityNotFoundException) {
            try {
                startActivity(Intent(WallpaperManager.ACTION_LIVE_WALLPAPER_CHOOSER))
            } catch (_: ActivityNotFoundException) {
                // Device does not expose a live wallpaper picker
            }
        }
    }

    override fun onResume() {
        super.onResume()
        renderer.butterflyCount = ButterflyPrefs.getCount(this)
        renderer.zoomLevel = ButterflyPrefs.getZoom(this)
        glSurfaceView.onResume()
    }

    override fun onPause() {
        glSurfaceView.onPause()
        super.onPause()
    }
}
