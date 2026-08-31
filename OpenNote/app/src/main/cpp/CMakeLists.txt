cmake_minimum_required(VERSION 3.22.1)
project("openwhiteboard" CXX)

# ── C++ Standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler Flags ────────────────────────────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(
        -O3
        -ffast-math
        -funroll-loops
        -fvisibility=hidden   # Hide all symbols except JNI exports
        -fno-exceptions
    )
else()
    add_compile_options(-O0 -g)
    # Note: ASan disabled by default — enable explicitly if needed:
    # add_compile_options(-fsanitize=address)
    # add_link_options(-fsanitize=address)
endif()

# ── ARM SIMD ──────────────────────────────────────────────────────────────────
if(ANDROID_ABI STREQUAL "arm64-v8a")
    add_compile_options(-march=armv8-a+simd -mtune=cortex-a55)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    add_compile_options(-mfpu=neon)
endif()

# ── Source Files ──────────────────────────────────────────────────────────────
# Engine Core — 100% platform-agnostic (no EGL, GLES, Android headers)
set(CORE_SOURCES
    src/engine/WhiteboardEngine.cpp
    src/engine/Document.cpp
    src/engine/CommandStack.cpp
    src/engine/AutoSaveManager.cpp
    src/canvas/CanvasCamera.cpp
    src/input/InputDispatcher.cpp
    src/tools/Tools.cpp
    src/file/FileFormat.cpp
)

# Android Platform Layer — EGL/GLES + JNI (isolated from core)
set(ANDROID_SOURCES
    src/rendering/RenderPipeline.cpp   # GlesRenderPipeline impl (was RenderPipeline.cpp)
    src/jni/jni_bridge.cpp
)

set(ENGINE_SOURCES
    ${CORE_SOURCES}
    ${ANDROID_SOURCES}
)

# ── Shared Library ────────────────────────────────────────────────────────────
add_library(openwhiteboard SHARED ${ENGINE_SOURCES})

# ── Include Directories ───────────────────────────────────────────────────────
target_include_directories(openwhiteboard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include   # ob/*.h public headers
)

# ── Android System Libraries ──────────────────────────────────────────────────
find_library(log-lib         log)
find_library(android-lib     android)
find_library(EGL-lib         EGL)
find_library(GLESv3-lib      GLESv3)
find_library(jnigraphics-lib jnigraphics)

target_link_libraries(openwhiteboard
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
    ${jnigraphics-lib}
)

# ── Link-time Optimization (Release only) ─────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_property(TARGET openwhiteboard PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

message(STATUS "OpenWhiteBoard Engine | ABI: ${ANDROID_ABI} | Build: ${CMAKE_BUILD_TYPE}")
message(STATUS "Core sources: ${CORE_SOURCES}")
