package org.example.vulkantut

import android.graphics.Color as AndroidColor
import android.util.Log
import android.view.Surface
import androidx.compose.foundation.AndroidExternalSurface
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

private const val TAG = "VulkanSurface"

/**
 * Chapter 1 deliberately renders no Vulkan frames.
 *
 * AndroidExternalSurface creates a real Android Surface; its lifecycle is
 * independent of recomposition and will later drive our native renderer.
 */
@Composable
fun VulkanLabScreen(modifier: Modifier = Modifier) {
    var status by remember { mutableStateOf("Waiting for a Surface") }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Chapter 1 · Compose-hosted Surface")
        AndroidExternalSurface(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
        ) {
            onSurface { surface, width, height ->
                Log.d(TAG, "created: ${width}x${height}")
                status = "Created: ${width} × ${height}"
                surface.drawTestColor()

                surface.onChanged { newWidth, newHeight ->
                    Log.d(TAG, "resized: ${newWidth}x${newHeight}")
                    status = "Resized: ${newWidth} × ${newHeight}"
                    surface.drawTestColor()
                }

                surface.onDestroyed {
                    Log.d(TAG, "destroyed")
                    status = "Surface destroyed"
                }
            }
        }
        Text(status, style = MaterialTheme.typography.bodyMedium)
        Text(
            "The blue region is drawn with Android Canvas, not Vulkan. " +
                "Its Surface will become the native Vulkan swapchain target.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

/** One test frame; intentionally no continuous rendering loop yet. */
private fun Surface.drawTestColor() {
    val canvas = lockCanvas(null)
    try {
        canvas.drawColor(AndroidColor.rgb(26, 72, 114))
    } finally {
        unlockCanvasAndPost(canvas)
    }
}
