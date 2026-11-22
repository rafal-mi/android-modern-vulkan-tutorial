#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <vulkan/vulkan.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr char kTag[] = "NativeVulkan";

void logError(const char* message, VkResult result = VK_SUCCESS) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s (VkResult=%d)", message, result);
}

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

Session* fromHandle(jlong handle) {
    return reinterpret_cast<Session*>(handle);
}
} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_org_example_vulkantut_NativeVulkan_create(
    JNIEnv* env, jobject /* thiz */, jobject javaSurface) {
    if (javaSurface == nullptr) {
        logError("Surface argument was null");
        return 0;
    }

    auto session = std::make_unique<Session>();
    session->window = ANativeWindow_fromSurface(env, javaSurface);
    if (!session->window) {
        logError("ANativeWindow_fromSurface failed");
        return 0;
    }

    // We request Vulkan 1.0 to permit feature discovery on older drivers.
    // A later chapter will require and enable the features our renderer needs.
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Modern Vulkan Tutorial";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = 2;
    instanceInfo.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateInstance(&instanceInfo, nullptr, &session->instance);
    if (result != VK_SUCCESS) {
        logError("vkCreateInstance failed", result);
        return 0;
    }

    VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.window = session->window;

    result = vkCreateAndroidSurfaceKHR(
        session->instance, &surfaceInfo, nullptr, &session->surface);
    if (result != VK_SUCCESS) {
        logError("vkCreateAndroidSurfaceKHR failed", result);
        return 0;
    }

    uint32_t count = 0;
    result = vkEnumeratePhysicalDevices(session->instance, &count, nullptr);
    if (result != VK_SUCCESS) {
        logError("vkEnumeratePhysicalDevices(count) failed", result);
        return 0;
    }

    std::vector<VkPhysicalDevice> physicalDevices(count);
    if (count > 0) {
        result = vkEnumeratePhysicalDevices(
            session->instance, &count, physicalDevices.data());
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            logError("vkEnumeratePhysicalDevices(list) failed", result);
            return 0;
        }
    }

    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevices[i], &properties);
        session->devices.push_back(properties);
        __android_log_print(
            ANDROID_LOG_INFO, kTag,
            "GPU %u: %s, Vulkan %u.%u.%u",
            i, properties.deviceName,
            VK_VERSION_MAJOR(properties.apiVersion),
            VK_VERSION_MINOR(properties.apiVersion),
            VK_VERSION_PATCH(properties.apiVersion));
    }

    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "Created VkInstance and VkSurfaceKHR; found %zu GPU(s)",
        session->devices.size());
    return reinterpret_cast<jlong>(session.release());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_example_vulkantut_NativeVulkan_describe(
    JNIEnv* env, jobject /* thiz */, jlong handle) {
    auto* session = fromHandle(handle);
    if (!session) {
        return env->NewStringUTF("Vulkan initialization failed. Check NativeVulkan in Logcat.");
    }

    std::ostringstream stream;
    stream << "VkInstance: created\\n"
           << "VkSurfaceKHR: created\\n"
           << "Physical devices: " << session->devices.size();

    for (const auto& gpu : session->devices) {
        stream << "\\n" << gpu.deviceName
               << " — Vulkan " << VK_VERSION_MAJOR(gpu.apiVersion)
               << "." << VK_VERSION_MINOR(gpu.apiVersion)
               << "." << VK_VERSION_PATCH(gpu.apiVersion);
    }
    const std::string description = stream.str();
    return env->NewStringUTF(description.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_org_example_vulkantut_NativeVulkan_destroy(
    JNIEnv* /* env */, jobject /* thiz */, jlong handle) {
    if (handle == 0) return;
    delete fromHandle(handle);
    __android_log_print(ANDROID_LOG_INFO, kTag, "Destroyed Vulkan session");
}
