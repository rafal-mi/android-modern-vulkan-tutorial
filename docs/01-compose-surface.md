# Chapter 1 — A Compose-hosted Android Surface

## Goal

Place a graphics surface *inside* an ordinary Jetpack Compose layout.
We will render one blue frame using Android Canvas to verify that the
surface exists and accepts drawing commands. **No Vulkan, JNI, native
activity, or rendering thread is involved yet.**

The surface is managed by `AndroidExternalSurface` from Compose Foundation.
This is not `AndroidView(SurfaceView(...))`, nor a full-screen rendering
activity. Its callbacks give us the Android `Surface` that we will later
pass to a C++ Vulkan renderer.

## Run

1. Check out `feature/01-compose-surface` and open the repository in Android Studio.
2. Let Gradle sync, select a device or emulator, and run `app`.
3. Observe the blue rectangle in the center of the normal Compose UI.
4. Read the surface size reported beneath it.
5. Rotate the device (if permitted), navigate away and back, or
   background/foreground the activity. Watch how the lifecycle behaves.

In Android Studio Logcat, filter by `tag:VulkanSurface`.
From a terminal, use:

```sh
adb logcat -s VulkanSurface:D '*:S'
```

Typical *kinds* of events (exact ordering and frequency depend on the device):

```text
created: 1080x1600
resized: 1080x900
destroyed
```

All dimensions here are **surface pixels**, not Compose `dp`.
Do not assume that changing Compose state produces a new Surface:
recomposition is not the same as surface creation. Similarly, changing
device orientation need not produce just a resize; Android may recreate
the activity and its surface.

## Read the source

- `MainActivity.kt` uses ordinary `ComponentActivity` and `setContent`.
- `VulkanLabScreen.kt` places `AndroidExternalSurface` in a `Column`.
- `onSurface { surface, width, height -> ... }` is called when a real
  `Surface` is available.
- `surface.onChanged { width, height -> ... }` reports size changes.
- `surface.onDestroyed { ... }` signals the end of that surface's lifetime.
- `lockCanvas` and `unlockCanvasAndPost` submit **one test frame**.
  They are temporary and will **not** be used to render Vulkan frames.

`onSurface`, `onChanged`, and `onDestroyed` are dispatched on the main
thread. In later chapters the native renderer must be arranged so its
work stops accessing the surface on destruction; cancellation or a UI
status change alone is not enough to make concurrent native rendering safe.

## Experiment: test the lifecycle

1. Set a breakpoint on the `Log.d(TAG, "created: ...")` line.
2. Debug the app using the Android Studio Kotlin debugger.
3. Inspect `surface`, `width`, and `height`.
4. Continue, trigger a resize, and break on the `onChanged` callback.
5. Navigate away from the activity or close it and check for
   `onDestroyed`. The callback may not occur at exactly the same point
   as the activity's `onPause` or `onStop`.

A useful variant is to comment out `surface.drawTestColor()` in
`onChanged` and compare the visible result when resizing. The correct
behavior of a real renderer is to adapt to the new dimensions.

## What the experiment proves — and does not prove

We have a real Android `Surface`, independently managed by Compose,
which can be used as a future native rendering target. We have **not**
created a Vulkan instance, queried a Vulkan-capable GPU, selected a
queue family, or made a swapchain.

The blue frame was drawn by Android Canvas, not by Vulkan. Do not call
`lockCanvas` on a surface while a Vulkan swapchain is using it.

## Next chapter

Introduce JNI, obtain an `ANativeWindow` from the `Surface`, create a
Vulkan instance, enumerate available physical devices and inspect Vulkan
features. We will establish ownership and lifetime rules before
creating the swapchain.

## References

- [Compose AndroidExternalSurface API](https://developer.android.com/reference/kotlin/androidx/compose/foundation/AndroidExternalSurface)
- [AndroidExternalSurfaceScope](https://developer.android.com/reference/kotlin/androidx/compose/foundation/AndroidExternalSurfaceScope)
- [SurfaceCoroutineScope](https://developer.android.com/reference/kotlin/androidx/compose/foundation/SurfaceCoroutineScope)
- [SurfaceScope](https://developer.android.com/reference/kotlin/androidx/compose/foundation/SurfaceScope)
