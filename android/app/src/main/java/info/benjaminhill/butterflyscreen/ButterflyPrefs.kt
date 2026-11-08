package info.benjaminhill.butterflyscreen

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit

object ButterflyPrefs {
    private const val PREFS_NAME = "butterfly_prefs"
    const val KEY_COUNT = "butterfly_count"
    const val KEY_ZOOM = "zoom_level"

    const val MIN_COUNT = 10
    const val MAX_COUNT = 200
    const val DEFAULT_COUNT = 50

    const val MIN_ZOOM = 0.5f
    const val MAX_ZOOM = 3.0f
    const val DEFAULT_ZOOM = 1.0f

    fun prefs(context: Context): SharedPreferences =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun getCount(context: Context): Int =
        prefs(context).getInt(KEY_COUNT, DEFAULT_COUNT).coerceIn(MIN_COUNT, MAX_COUNT)

    fun setCount(context: Context, count: Int) {
        prefs(context).edit {
            putInt(KEY_COUNT, count.coerceIn(MIN_COUNT, MAX_COUNT))
        }
    }

    fun getZoom(context: Context): Float =
        prefs(context).getFloat(KEY_ZOOM, DEFAULT_ZOOM).coerceIn(MIN_ZOOM, MAX_ZOOM)

    fun setZoom(context: Context, zoom: Float) {
        prefs(context).edit {
            putFloat(KEY_ZOOM, zoom.coerceIn(MIN_ZOOM, MAX_ZOOM))
        }
    }
}
