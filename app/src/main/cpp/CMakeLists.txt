cmake_minimum_required(VERSION 3.22.1)
project(modern_vulkan_tutorial LANGUAGES CXX)

add_library(vulkan_tutorial SHARED vulkan_bridge.cpp)

target_compile_features(vulkan_tutorial PRIVATE cxx_std_20)
target_compile_definitions(vulkan_tutorial PRIVATE VK_USE_PLATFORM_ANDROID_KHR)

find_library(android_log log REQUIRED)
find_library(android_native android REQUIRED)
find_library(vulkan_lib vulkan REQUIRED)

target_link_libraries(vulkan_tutorial PRIVATE
    ${android_log}
    ${android_native}
    ${vulkan_lib}
)
