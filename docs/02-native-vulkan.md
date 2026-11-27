# Chapter 2 — From Jetpack Compose to the Vulkan API

In [Chapter 1](01-compose-surface.md), we embedded an Android `Surface` inside a Jetpack Compose screen. Our application was still an ordinary Android application: `ComponentActivity`, `Scaffold`, `TopAppBar`, and `AndroidExternalSurface`. We verified that the surface appeared, received pixel dimensions, and was destroyed and recreated during rotation.

But a `Surface` is not a Vulkan object. The next task is to cross two boundaries:

1. From managed Kotlin/Java objects to the Android NDK.
2. From Android's native window to Vulkan's window-system integration (WSI).

By the end of this chapter we will have created a Vulkan instance, a Vulkan surface, and enumerated the available physical devices. **We will not render anything yet.** The viewport may remain black. A GPU name and Vulkan version in the Compose UI and Logcat are the observable result.

We will examine the code that is already in this repository rather than presenting a second, incompatible example. The main files are:

- [`NativeVulkan.kt`](../app/src/main/java/org/example/vulkantut/NativeVulkan.kt), the Kotlin/JNI boundary;
- [`VulkanLabScreen.kt`](../app/src/main/java/org/example/vulkantut/VulkanLabScreen.kt), our Compose surface lifecycle;
- [`vulkan_bridge.cpp`](../app/src/main/cpp/vulkan_bridge.cpp), the native implementation;
- [`CMakeLists.txt`](../app/src/main/cpp/CMakeLists.txt), the small Android NDK build configuration.

## 1. What is a Vulkan instance?

It is tempting to begin with the GPU, but Vulkan begins with an *instance*.

A `VkInstance` represents an initialized connection between our application and a Vulkan implementation. It gives access to instance-level operations such as discovering physical devices. The instance is not a GPU, does not own command queues, and cannot execute graphics commands.

The distinction can be represented as:

```text
Application
   |
   +-- VkInstance                 instance-level API context
          |
          +-- VkPhysicalDevice A   discoverable GPU/driver
          +-- VkPhysicalDevice B   another possible GPU/driver
                 |
                 +-- VkDevice      logical device (later chapter)
                       |
                       +-- queues, buffers, images, pipelines ...
```

A `VkPhysicalDevice` is a handle identifying a physical-device implementation exposed to the instance. A `VkDevice` is an explicitly created logical device, which we will introduce later. The same words, *device* and *physical device*, must not be used interchangeably.

Android's `Surface` fits into a different branch of this picture:

```text
Jetpack Compose
    AndroidExternalSurface
          |
    android.view.Surface
          |
    ANativeWindow
          |
    VkSurfaceKHR  ------ created with a VkInstance
          |
    VkSwapchainKHR (later chapter; created through a VkDevice)
```

The surface is about **where images will eventually be presented**. The physical device is about **which implementation may eventually produce them**. A usable rendering system will have to establish that the chosen device can present to this surface.

## 2. Why Kotlin needs a native bridge

Our UI callback receives an `android.view.Surface` object. Vulkan's Android presentation extension, however, expects a pointer to an `ANativeWindow` in native code.

The Java Native Interface (JNI) is the bridge between these worlds. Consider the Kotlin declaration:

```kotlin
internal object NativeVulkan {
    init {
        System.loadLibrary("vulkan_tutorial")
    }

    external fun create(surface: Surface): Long
    external fun describe(handle: Long): String
    external fun destroy(handle: Long)
}
```

`System.loadLibrary` loads `libvulkan_tutorial.so`, the shared library built by our NDK project. The `external` functions have no Kotlin implementation; their implementation is found through JNI.

Our native function is exported with a corresponding name:

```cpp
extern "C" JNIEXPORT jlong JNICALL
Java_org_example_vulkantut_NativeVulkan_create(
    JNIEnv* env,
    jobject /* thiz */,
    jobject javaSurface)
```

The long function name encodes the package, class/object name and method name. `extern "C"` prevents C++ name mangling; `JNIEXPORT` and `JNICALL` express the JNI calling convention and visibility requirements. The `JNIEnv*` parameter supplies access to JNI functions; the second parameter represents the receiver of this instance-style native method; the third refers to our Android `Surface`.

This chapter uses simple, statically named JNI functions. Larger projects sometimes prefer `RegisterNatives`, which connects Java methods to native functions explicitly.

### What does the Kotlin Long contain?

The `Long` returned by `create` is **not** a `VkInstance`, a `VkSurfaceKHR`, or a hardware address. In our implementation it contains a cast C++ pointer:

```cpp
return reinterpret_cast<jlong>(session.release());
```

The session aggregates native resources. Later:

```cpp
Session* fromHandle(jlong handle) {
    return reinterpret_cast<Session*>(handle);
}
```

This is an opaque-handle technique: Kotlin transports the numeric token but does not inspect the pointed-to C++ object. The native implementation controls its interpretation. It works in our exercise because the same process creates and consumes the handle; it must never be persisted, serialized, or reused after destruction.

Returning `0` indicates a failed creation. The Kotlin code must then avoid destroying or dereferencing it as an actual pointer. **A nonzero handle is owned by precisely one surface callback and must be destroyed exactly once.**

## 3. From an Android Surface to ANativeWindow

Our starting point is:

```cpp
session->window = ANativeWindow_fromSurface(env, javaSurface);
if (!session->window) {
    logError("ANativeWindow_fromSurface failed");
    return 0;
}
```

An `ANativeWindow` is Android's native representation of a window-like producer endpoint for graphical buffers. It is not a conventional desktop window, and it is not the memory storing our future vertex coordinates. It provides the native presentation target needed by Android's Vulkan WSI integration.

Crucially, `ANativeWindow_fromSurface` **acquires a reference**. Acquiring a reference has a concrete consequence: our code must eventually call `ANativeWindow_release`. This is separate from the Kotlin garbage collector's treatment of the `Surface` object.

Do not confuse reference ownership with the Android window's actual availability. An acquired native reference is not permission to keep submitting frames after Android says the surface has been destroyed. This is why the callbacks from Chapter 1 matter.

In this chapter the resources are created and destroyed synchronously. When we introduce a rendering thread, simply remembering an `ANativeWindow*` will not be sufficient: we must stop presentation, synchronize relevant in-flight work, and manage the associated swapchain before discarding the surface.

## 4. The application's Vulkan version declaration

Vulkan uses C structures to specify creation parameters. Here is our first:

```cpp
VkApplicationInfo appInfo{};
appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
appInfo.pApplicationName = "Modern Vulkan Tutorial";
appInfo.apiVersion = VK_API_VERSION_1_0;
```

Why do we write `{}`? In C++, value-initialization zero-initializes this aggregate. Unless otherwise set, its pointer members become null and numeric members become zero. Explicit initialization prevents unspecified garbage from being passed to the Vulkan driver.

Most extensible Vulkan structures begin with two fields:

- `sType` identifies which kind of structure the driver is reading.
- `pNext` may point to a chain of extension or newer-version structures.

The structure's C++ *type* alone is not enough because Vulkan functions receive pointers to C structures and may traverse `pNext` chains at runtime. We set `sType` explicitly; here `pNext` remains null.

The application name is informational. `apiVersion` is more significant.

### Requested version versus supported version

We currently request `VK_API_VERSION_1_0` to keep device discovery available on older Vulkan-capable Android devices. This is a deliberate teaching choice, not our final renderer's required version.

Later, `vkGetPhysicalDeviceProperties` returns `properties.apiVersion`, which reports the API version supported by a physical device.

The fact that a GPU reports, for example, Vulkan 1.3 **does not automatically allow this Vulkan 1.0 instance to use 1.3 core commands**. The application's requested API version, the implementation's capabilities, enabled extensions, and the logical device's enabled features all matter.

We will later raise our target version and check dynamic-rendering support explicitly. For now we are exploring, not enabling, that functionality.

## 5. Instance extensions: why Android requires them

Vulkan is deliberately platform-independent in its core. Creating a presentation surface requires platform-specific mechanisms.

Our code selects two *instance extensions*:

```cpp
const char* extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
};
```

`VK_KHR_surface` supplies the general abstraction for platform presentation surfaces. `VK_KHR_android_surface` supplies the Android-specific means of creating one from an `ANativeWindow`.

These extensions are requested when we create the instance:

```cpp
VkInstanceCreateInfo instanceInfo{};
instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
instanceInfo.pApplicationInfo = &appInfo;
instanceInfo.enabledExtensionCount = 2;
instanceInfo.ppEnabledExtensionNames = extensions;
```

Notice the relationship between `enabledExtensionCount` and `ppEnabledExtensionNames`. The latter is a pointer to an array of pointers to null-terminated strings. The integer tells Vulkan how many elements to read.

There are two broad categories of extension in Vulkan: **instance extensions** and **device extensions**. They are enabled at different times. Our Android presentation-surface extension is an *instance* extension; swapchain creation later requires the *device* extension `VK_KHR_swapchain`.

A production implementation should use `vkEnumerateInstanceExtensionProperties` to query and verify available instance extensions before creation. This small chapter relies on the Android Vulkan environment providing the relevant WSI extensions; when absent, initialization fails with a Vulkan error rather than rendering.

## 6. Creating VkInstance

The central call is:

```cpp
VkResult result =
    vkCreateInstance(&instanceInfo, nullptr, &session->instance);
if (result != VK_SUCCESS) {
    logError("vkCreateInstance failed", result);
    return 0;
}
```

The function's arguments are worth reading carefully:

1. `&instanceInfo` points to the creation parameters discussed above.
2. `nullptr` requests Vulkan's default allocation callbacks. Custom `VkAllocationCallbacks` are possible, but unnecessary for this tutorial.
3. `&session->instance` points to the storage in which the created Vulkan handle will be returned.

The return value, `VkResult`, is a result code. The returned handle and the result code serve distinct purposes: a value of `VK_SUCCESS` indicates that the operation succeeded; the handle is then available for subsequent Vulkan calls.

For failed creation, our `std::unique_ptr<Session>` is destroyed automatically as the function returns. Its destructor releases any resources successfully acquired before the failure. This is C++ resource acquisition is initialization (RAII) used to simplify cleanup across several early-return paths.

The instance has not chosen a GPU, allocated vertex memory, or opened a command queue. It is the necessary foundation for those operations.

## 7. Creating VkSurfaceKHR for Android

Now we connect Vulkan to the window obtained from Compose:

```cpp
VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
surfaceInfo.sType =
    VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
surfaceInfo.window = session->window;

result = vkCreateAndroidSurfaceKHR(
    session->instance,
    &surfaceInfo,
    nullptr,
    &session->surface);
```

A `VkSurfaceKHR` is a Vulkan *surface handle*. It describes a platform presentation endpoint. It does not own a rendered image, does not represent a texture, and does not define the eventual frame count.

The actual displayable images will belong to a **swapchain**, introduced in a later chapter. A swapchain ties together a logical device and a presentation surface, providing images that the application can acquire, render into, and present.

It is useful to maintain the distinction:

| Concept | Type | Introduced |
| --- | --- | --- |
| Android-managed presentation target | `Surface` | Chapter 1 |
| Native Android reference | `ANativeWindow*` | Chapter 2 |
| Vulkan platform presentation handle | `VkSurfaceKHR` | Chapter 2 |
| GPU-capable logical device | `VkDevice` | Next chapter |
| Presentable image sequence | `VkSwapchainKHR` | Later chapter |

Creating a surface **does not establish that any particular physical device can present to it**. We'll inspect queue families and presentation support in the next chapter.

## 8. Enumerating physical devices: the two-call pattern

Many Vulkan functions return variable-length arrays. Rather than allocate a fixed-size array and hope it is large enough, the application asks Vulkan how many elements exist.

First:

```cpp
uint32_t count = 0;
result = vkEnumeratePhysicalDevices(
    session->instance, &count, nullptr);
```

Here `pPhysicalDeviceCount` is an *input/output* pointer. With `pPhysicalDevices == nullptr`, the implementation reports the number of devices. A count of zero means there are no physical devices to enumerate.

Then allocate sufficient storage and fetch the handles:

```cpp
std::vector<VkPhysicalDevice> physicalDevices(count);
if (count > 0) {
    result = vkEnumeratePhysicalDevices(
        session->instance, &count, physicalDevices.data());
}
```

This is not a unique peculiarity of Vulkan; it is a common C API technique for variable-length output. The same pattern will recur for extensions, queue families, surface formats, present modes and more.

### Why can the second call return VK_INCOMPLETE?

The environment might change between the counting call and the filling call. If the supplied array is insufficient, Vulkan can return `VK_INCOMPLETE`, meaning the result is not exhaustive.

Our teaching implementation accepts `VK_INCOMPLETE` and prints whatever was returned. It does **not** promise a complete list if the set changed. A robust enumeration helper would retry, resizing its array, and would distinguish ordinary errors from an incomplete result.

This is an example of a distinction we will preserve throughout the book: *a small educational implementation* need not yet be a fully general library, but it should explain which guarantees it does and does not provide.

## 9. Inspecting GPU properties

We have physical-device handles. To learn something about them, we call:

```cpp
VkPhysicalDeviceProperties properties{};
vkGetPhysicalDeviceProperties(physicalDevices[i], &properties);
```

We then inspect:

- `deviceName`, a human-readable implementation name;
- `apiVersion`, the physical device's supported Vulkan API version.

Our code logs:

```cpp
__android_log_print(
    ANDROID_LOG_INFO, kTag,
    "GPU %u: %s, Vulkan %u.%u.%u",
    i, properties.deviceName,
    VK_VERSION_MAJOR(properties.apiVersion),
    VK_VERSION_MINOR(properties.apiVersion),
    VK_VERSION_PATCH(properties.apiVersion));
```

The version field is an integer containing multiple components. The Vulkan macros extract its major, minor and patch values. It is better to use the macros than manually invent masks and shifts: the packing is an API-defined encoding.

A physical device also exposes limits, features, memory properties and queue-family capabilities. We have not examined those yet. A device supporting a certain API version does not prove that every *optional feature* is supported or enabled.

The properties we need for displaying a short description are copied into `session->devices`. The temporary `VkPhysicalDevice` handles themselves are not retained, because our present chapter does not create a logical device from them.

## 10. C++ ownership: why Session exists

Our C++ session is deliberately small:

```cpp
struct Session {
    ANativeWindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::vector<VkPhysicalDeviceProperties> devices;

    ~Session() {
        if (instance != VK_NULL_HANDLE) {
            if (surface != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(instance, surface, nullptr);
            }
            vkDestroyInstance(instance, nullptr);
        }
        if (window != nullptr) {
            ANativeWindow_release(window);
        }
    }
};
```

The initialization values matter: a null pointer or `VK_NULL_HANDLE` means that the corresponding acquisition has not yet succeeded. The destructor can therefore clean up a *partially initialized* session safely.

The destruction order is intentional:

1. Destroy `VkSurfaceKHR`, which was created using `VkInstance`.
2. Destroy `VkInstance`.
3. Release the `ANativeWindow` reference we acquired from Android.

This order also helps explain ownership. A resource must not outlive the parent object needed to destroy it. In later chapters we will introduce more complicated dependencies: swapchain image views, swapchains, devices, semaphores and work in flight on queues.

We use a `std::unique_ptr<Session>` during creation. Until initialization succeeds, the pointer is owned automatically by local C++ scope. At success, `release()` transfers responsibility to our JNI handle protocol. When Kotlin calls `NativeVulkan.destroy(handle)`, the C++ code executes `delete fromHandle(handle)` and the destructor releases the resources.

The explicit JNI protocol is important: Kotlin's garbage collector **does not automatically call the C++ destructor of a pointer encoded as Long**.

### Why not keep one VkInstance forever?

An instance is permitted to outlive an individual Android surface. In fact, a mature renderer will probably separate device-level state from surface/swapchain-level state.

This first native exercise intentionally stores both in a single session. It makes the resources and their lifetimes easy to inspect, and avoids sharing state across callbacks. It is not a claim that creating a new instance on every rotation is the final architecture.

Our Chapter 1 experiment already told us that rotation may follow:

```text
created 984x1481
destroyed
created 1997x426
destroyed
created 984x1481
```

For the present code, each `created` event constructs a new session and each `destroyed` event deletes it. A later renderer should reuse stable device resources where appropriate while rebuilding the presentation resources associated with each new surface.

## 11. Returning native information to Compose

After creation, Kotlin calls:

```kotlin
val handle = NativeVulkan.create(surface)
nativeInfo = NativeVulkan.describe(handle)
```

The native `describe` function constructs a string and returns it through JNI via `NewStringUTF`. Compose observes the changed `nativeInfo` state and can update the text without recreating the surface.

**Recomposition is not surface recreation.** This is a crucial separation: the UI may recompose because a label changes, while the platform surface and its native session remain alive.

Our native `describe` function currently contains C++ string literals with `"\\n"`. Those are literal backslash-n sequences rather than newline characters. The intended multiline UI formatting would use `"\n"` in the C++ source. This is a small presentation defect in the current implementation, not a change to Vulkan initialization.

Likewise, when `create` fails and returns zero, `describe` returns an error message rather than attempting to access a null session.

## 12. Running and debugging the chapter

Use the merged `main` branch (or the documentation branch when reviewing this revision). Install the Android NDK and CMake in Android Studio's SDK Manager, then run the **debug** build on a Vulkan-capable device.

The viewport is not expected to show a Vulkan image yet. The device description below it and the Logcat messages are our test output.

```sh
adb logcat -s NativeVulkan:I VulkanSurface:D '*:S'
```

You should see messages resembling:

```text
D VulkanSurface: created: 984x1481
I NativeVulkan: GPU 0: Example GPU, Vulkan 1.3.0
I NativeVulkan: Created VkInstance and VkSurfaceKHR; found 1 GPU(s)
D VulkanSurface: destroyed
I NativeVulkan: Destroyed Vulkan session
```

The device name and exact version depend on your phone. The dimensions depend on the Compose viewport, not necessarily the entire physical display.

### Experiment A: step through JNI and native-window acquisition

Use Android Studio's Dual/Native debugging configuration and put a breakpoint at the beginning of `Java_org_example_vulkantut_NativeVulkan_create`. Inspect the `javaSurface` argument and the `JNIEnv*`. Step over `ANativeWindow_fromSurface`, then inspect `session->window`.

In the LLDB expression evaluator, useful expressions include:

```text
session.get()
session->window
ANativeWindow_getWidth(session->window)
ANativeWindow_getHeight(session->window)
```

The native width and height should correspond to the current surface's pixel size. If they differ, investigate *when* the values were sampled and whether the surface changed during the operation.

### Experiment B: inspect VkInstanceCreateInfo

Stop immediately before `vkCreateInstance` and examine:

```text
appInfo.sType
appInfo.apiVersion
instanceInfo.enabledExtensionCount
extensions[0]
extensions[1]
```

Step past the call and inspect:

```text
result
session->instance
```

A successful result and a non-null instance handle establish that Vulkan initialization succeeded. A non-null pointer alone is not a substitute for checking `VkResult`.

### Experiment C: enumerate devices

Stop after the first `vkEnumeratePhysicalDevices` call. Inspect `count`. Continue after the second call, then examine `physicalDevices` and `properties.deviceName`. Compare the numeric `apiVersion` with its formatted major/minor/patch output.

Ask yourself: why does the first call receive `nullptr` where the second receives `physicalDevices.data()`? Why must `count` be a mutable variable rather than an integer literal?

### Experiment D: rotate and inspect destruction

Set a breakpoint inside `Session::~Session`, rotate to landscape and return to portrait, and inspect:

```text
window
instance
surface
```

Watch the sequence of destructor calls and surface-creation callbacks. The current application is intentionally single-threaded as far as native Vulkan work is concerned. When we introduce asynchronous rendering, the surface-destruction callback must coordinate with work still using native resources.

### Errors worth understanding

If `System.loadLibrary` cannot locate the built shared object, you will see an `UnsatisfiedLinkError` before any native Vulkan code runs. This is an Android build/JNI problem, not a GPU failure.

If `vkCreateInstance` fails, inspect the enabled extensions, the requested API version and the `VkResult` in Logcat. If instance creation works but `vkCreateAndroidSurfaceKHR` fails, investigate the native-window pointer and Android surface lifetime.

If no physical devices are enumerated, this chapter has not found a GPU implementation to use. It does not make sense to proceed to logical-device creation until the cause is understood.

## 13. What we have proved

At this point our Compose app can:

1. Obtain and observe a real Android `Surface`.
2. Cross into C++ with JNI.
3. Acquire and release an `ANativeWindow` reference.
4. Initialize a Vulkan instance with Android presentation extensions.
5. Create a Vulkan surface.
6. Enumerate physical devices and read their properties.
7. Clean up the native session when Android replaces the surface.

We have **not** selected a queue family, checked presentation support, enabled Vulkan 1.3 features, created a logical device, allocated GPU memory, or submitted drawing commands.

This is exactly where the next chapter begins: **queue families, physical-device suitability, and `VkDevice` creation**. Once those concepts are understood, we can build a swapchain and finally clear a Vulkan image.

## Further reading

The following sources are normative API references or Android platform documentation; this chapter is independently written for our project.

- [Vulkan specification: Initialization](https://docs.vulkan.org/spec/latest/chapters/initialization.html)
- [Vulkan specification: Devices and Queues](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html)
- [Vulkan: VkApplicationInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkApplicationInfo.html)
- [Vulkan: vkCreateInstance](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateInstance.html)
- [Vulkan: vkEnumeratePhysicalDevices](https://docs.vulkan.org/refpages/latest/refpages/source/vkEnumeratePhysicalDevices.html)
- [Vulkan: vkCreateAndroidSurfaceKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateAndroidSurfaceKHR.html)
- [Android NDK: ANativeWindow](https://developer.android.com/ndk/reference/group/a-native-window)
- [Android NDK: ANativeWindow_fromSurface](https://developer.android.com/ndk/reference/group/native-activity)
