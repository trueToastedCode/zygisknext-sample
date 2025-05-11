cmake_minimum_required(VERSION 3.22.1)
project(hostsredirect)

# Diese Flags sind entscheidend für die Android-NDK-Integration
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

add_library(${MODULE_NAME} SHARED hook.cpp utils.cpp)
target_link_libraries(${MODULE_NAME} 
    log 
    c++_static
    android
)

add_library(entrypoint_lib SHARED entrypoint_lib/entrypoint_lib.cpp)
target_link_libraries(entrypoint_lib 
    log 
    c++_static
    android
)

# Zusätzliche Sicherheit für Android-Typen
target_compile_definitions(${MODULE_NAME} PRIVATE
    -D__STDC_LIMIT_MACROS
    -D__STDC_CONSTANT_MACROS
)
