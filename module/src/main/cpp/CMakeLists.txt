cmake_minimum_required(VERSION 3.22.1)
project(hostsredirect)

set(CXX_FLAGS "${CXX_FLAGS} -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS}")

find_package(cxx REQUIRED CONFIG)
link_libraries(cxx::cxx)

# Erste Library - das Zygisk-Modul selbst
add_library(${MODULE_NAME} SHARED hook.cpp utils.cpp)
target_link_libraries(${MODULE_NAME} log)

# Zweite Library - für EntryPoint.java
add_library(entrypoint_lib SHARED entrypoint_lib/entrypoint_lib.cpp)
# JNI-Header-Verzeichnis hinzufügen
include_directories(${ANDROID_NDK}/sources/android/native_app_glue)
# JNI-Bindung für die zweite Library
target_link_libraries(entrypoint_lib android log)
