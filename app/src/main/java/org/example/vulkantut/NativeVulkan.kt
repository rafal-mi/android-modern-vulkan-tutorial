package org.example.vulkantut

import android.view.Surface

/**
 * Native Vulkan functions use an opaque pointer returned as Long.
 * A nonzero handle must be destroyed exactly once, before its Android
 * Surface becomes invalid. No rendering thread exists in Chapter 2.
 */
internal object NativeVulkan {
    init {
        System.loadLibrary("vulkan_tutorial")
    }

    external fun create(surface: Surface): Long
    external fun describe(handle: Long): String
    external fun destroy(handle: Long)
}
