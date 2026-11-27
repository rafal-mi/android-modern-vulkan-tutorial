# Chapter 2 — JNI, ANativeWindow, and a Vulkan Instance

## Goal

We keep the ordinary `ComponentActivity` and `AndroidExternalSurface` from Chapter 1.
We cross into native C++, establish an Android native window, create our first Vulkan
instance and platform surface, and enumerate physical GPUs.

**There is no logical device, swapchain, graphics pipeline, or rendered image yet.**
Expect a dark or undefined viewport; the displayed native GPU description is
this chapter's observable result.

## Native object chain

```text
AndroidExternalSurface
  └── android.view.Surface (Java/Kotlin)
        └── ANativeWindow_fromSurface (acquires a reference)
              └── vkCreateAndroidSurfaceKHR (VkSurfaceKHR)
VkInstance
  ├── VkSurfaceKHR
  └── vkEnumeratePhysicalDevices
```

A `VkInstance` represents an application connection to Vulkan.
A `VkPhysicalDevice` identifies a physical GPU/driver exposed by the Vulkan
implementation. It is **not** a `VkDevice` (logical device) and no command
queues have been created.

A `VkSurfaceKHR` represents the platform presentation surface; it is not a
swapchain or a framebuffer. `vkCreateAndroidSurfaceKHR` requires the
`VK_KHR_surface` and `VK_KHR_android_surface` instance extensions.

## Build and run

1. Check out `feature/02-native-vulkan` and sync Android Studio.
2. Ensure SDK Manager has **NDK (Side by side)** and **CMake** installed.
3. Build and run the app on a physical Vulkan-capable Android device.
4. See the device name and Vulkan API version below the viewport.
5. Filter Logcat for `NativeVulkan` and `VulkanSurface`.

```sh
adb logcat -s NativeVulkan:I VulkanSurface:D '*:S'
```

Example only (device name and version will differ):

```text
D VulkanSurface: created: 984x1481
I NativeVulkan: GPU 0: Example GPU, Vulkan 1.3.0
I NativeVulkan: Created VkInstance and VkSurfaceKHR; found 1 GPU(s)
D VulkanSurface: destroyed
I NativeVulkan: Destroyed Vulkan session
```

If `vkCreateInstance` or `vkCreateAndroidSurfaceKHR` fails, Logcat prints
the raw `VkResult`. The Kotlin description also reports failure.

## Study the native code

- `NativeVulkan.kt`: three external JNI functions; the Kotlin `Long`
  represents an opaque C++ `Session*`, **not** a Vulkan handle.
- `vulkan_bridge.cpp`: `ANativeWindow_fromSurface`, `VkApplicationInfo`,
  `VkInstanceCreateInfo`, `vkCreateInstance`,
  `VkAndroidSurfaceCreateInfoKHR`, `vkCreateAndroidSurfaceKHR`,
  `vkEnumeratePhysicalDevices` and `vkGetPhysicalDeviceProperties`.
- `Session::~Session`: destroy `VkSurfaceKHR` before `VkInstance`, then
  release the acquired `ANativeWindow` reference.
- `VulkanLabScreen.kt`: each `onSurface` callback owns one native session;
  `onDestroyed` destroys it synchronously, before the Android Surface
  ceases to be valid. We do no native asynchronous rendering yet.

`ANativeWindow_fromSurface` returns a reference that must be released with
`ANativeWindow_release`. A distinct native window reference is acquired
for every new Surface. The C++ `std::unique_ptr` retains ownership during
initialization and handles early failures automatically; successful sessions
are destroyed by the explicit JNI destroy call.

We request Vulkan **1.0** at instance creation to allow discovery on older
drivers; the reported GPU API version may be newer. This does **not** enable
Vulkan 1.3 features yet.

## Debugging in Android Studio

1. Build a **debug** variant and start the app with **Debug**, selecting a
   debugger that supports both Java/Kotlin and native C++ (Dual), or attach
   LLDB to the running app.
2. Place a native breakpoint at `vkCreateInstance` in `vulkan_bridge.cpp`.
3. Inspect `appInfo.apiVersion`, `extensions[0]`, `extensions[1]`,
   `instanceInfo.enabledExtensionCount`.
4. Step over `vkCreateInstance`; inspect `result` and `session->instance`.
5. Stop after `ANativeWindow_fromSurface` and inspect `session->window`.
   Compare it with `session->surface` after `vkCreateAndroidSurfaceKHR`.
   These are **different types of handles**.
6. Stop after `vkEnumeratePhysicalDevices` and inspect `count` and each
   physical device's `deviceName` and `apiVersion`.
7. Rotate to landscape and back. Set a breakpoint inside
   `Session::~Session`. Check cleanup happens between creation events.

The pointer address alone is not proof that a Vulkan handle remains valid.
Validity comes from successful API calls and correct lifetime management.

## Important restriction

Chapter 1 used `Surface.lockCanvas()`. Chapter 2 intentionally removes
that drawing. Do **not** draw with Canvas on a Surface while Vulkan has a
platform presentation surface attached to the same Android window.
We will show a picture again only after implementing a Vulkan swapchain.

## Next chapter

Choose queue families, create a logical `VkDevice`, check presentation
support, and prepare swapchain creation. Dynamic rendering comes after
swapchain setup.

## References

- https://docs.vulkan.org/guide/latest/windowing_audio_input.html
- https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateAndroidSurfaceKHR.html
- https://developer.android.com/ndk/guides/graphics/getting-started
