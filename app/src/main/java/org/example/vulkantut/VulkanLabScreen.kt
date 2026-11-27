package org.example.vulkantut

import android.util.Log
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
 * Chapter 2: Vulkan instance and platform surface exist, but no swapchain
 * or frames are submitted yet. The viewport may appear black.
 */
@Composable
fun VulkanLabScreen(modifier: Modifier = Modifier) {
    var status by remember { mutableStateOf("Waiting for a Surface") }
    var nativeInfo by remember { mutableStateOf("No native session yet") }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Chapter 2 · First native Vulkan objects")
        AndroidExternalSurface(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
        ) {
            onSurface { surface, width, height ->
                Log.d(TAG, "created: ${width}x${height}")
                var handle = NativeVulkan.create(surface)
                status = "Created: ${width} × ${height}"
                nativeInfo = NativeVulkan.describe(handle)

                surface.onChanged { newWidth, newHeight ->
                    Log.d(TAG, "resized: ${newWidth}x${newHeight}")
                    status = "Resized: ${newWidth} × ${newHeight}"
                    // No swapchain yet; nothing native needs resizing.
                }

                surface.onDestroyed {
                    Log.d(TAG, "destroyed")
                    if (handle != 0L) {
                        NativeVulkan.destroy(handle)
                        handle = 0L
                    }
                    status = "Surface destroyed"
                    nativeInfo = "Native Vulkan session destroyed"
                }
            }
        }
        Text(status, style = MaterialTheme.typography.bodyMedium)
        Text(nativeInfo, style = MaterialTheme.typography.bodySmall)
        Text(
            "No pixels are rendered yet. Chapter 2 creates Vulkan objects; " +
                "a later chapter will introduce the swapchain.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}
