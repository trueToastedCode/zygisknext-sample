cmake_minimum_required(VERSION 3.22.1)
project(hostsredirect)

# Configure LSPlant to build only static library
set(LSPLANT_BUILD_SHARED OFF CACHE BOOL "Build LSPlant as shared library" FORCE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
    -fno-exceptions \
    -fno-rtti \
    -fvisibility=hidden \
    -fvisibility-inlines-hidden")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# System-Header hinzufügen
include_directories(
    ${ANDROID_SYSROOT}/usr/include
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# include LSPlant
add_subdirectory(LSPlant/lsplant/src/main/jni)

add_library(${MODULE_NAME} SHARED hook.cpp utils.cpp)
target_link_libraries(${MODULE_NAME} 
    log 
    c++_static
    android
)

# Zusätzliche Sicherheit für Android-Typen
target_compile_definitions(${MODULE_NAME} PRIVATE
    -D__STDC_LIMIT_MACROS
    -D__STDC_CONSTANT_MACROS
)

add_library(entrypoint_lib SHARED entrypoint_lib/entrypoint_lib.cpp)

target_include_directories(entrypoint_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/LSPlant/lsplant/src/main/jni/include
)

target_link_libraries(entrypoint_lib 
    PRIVATE
    lsplant_static
    log 
    c++_static
    android
)

# Zusätzliche Sicherheit für Android-Typen
target_compile_definitions(entrypoint_lib PRIVATE
    -D__STDC_LIMIT_MACROS
    -D__STDC_CONSTANT_MACROS
)
