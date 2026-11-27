# Modern Vulkan Tutorial

A modern Vulkan tutorial for Android, rendering inside Jetpack Compose using dynamic rendering, 
native C++, and explicit GPU resource management.

## Tutorial chapters

1. [Chapter 1 — A Compose-hosted Android Surface](docs/01-compose-surface.md) — surface lifecycle, a Canvas test frame, and Kotlin debugging.

2. [Chapter 2 — JNI, ANativeWindow, and a Vulkan Instance](docs/02-native-vulkan.md) — native window ownership, Vulkan instance, Android platform surface, physical-device enumeration, and LLDB debugging.

Each chapter develops the same Compose app without a `NativeActivity` or `GameActivity`. A swapchain and actual Vulkan rendering will follow.
