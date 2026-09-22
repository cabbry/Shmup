# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.22.1)

# Declares and names the project.

project("shmup")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format-security -DANDROID -DNATIVE_ACTIVITY")


set(BASE "../../../../../src")
file(GLOB SOURCES ${BASE}/core/*.h ${BASE}/core/*.c)
file(GLOB ANDROID_BACKENDS ${BASE}/backends/android/*.h ${BASE}/backends/android/*.c)
# Round 90: the OpenGL backend (GL ES 2 flavour on Android) and the version,
# from the one place it is kept.
set(GL_BACKEND ${BASE}/backends/gl/renderer_gl.c ${BASE}/backends/gl/renderer_gl.h)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../ios/shmup.plist" SHMUP_PLIST)
string(REGEX MATCH "CFBundleShortVersionString</key>[ \t\r\n]*<string>([^<]+)</string>" _m "${SHMUP_PLIST}")
set(SHMUP_VERSION "${CMAKE_MATCH_1}")
if(NOT SHMUP_VERSION)
    set(SHMUP_VERSION "dev")
endif()
add_compile_definitions(SHMUP_VERSION="${SHMUP_VERSION}")
include_directories(${BASE}/core ${BASE}/backends/gl)

add_library( # Sets the name of the library.
        shmup

        # Sets the library as a shared library.
        SHARED

        ${BASE}/third_party/libpng/png.c
        ${BASE}/third_party/libpng/pngerror.c
        ${BASE}/third_party/libpng/pngget.c
        ${BASE}/third_party/libpng/pngmem.c
        ${BASE}/third_party/libpng/pngpread.c
        ${BASE}/third_party/libpng/pngread.c
        ${BASE}/third_party/libpng/pngrio.c
        ${BASE}/third_party/libpng/pngrtran.c
        ${BASE}/third_party/libpng/pngrutil.c
        ${BASE}/third_party/libpng/pngset.c
        ${BASE}/third_party/libpng/pngtrans.c
        ${BASE}/third_party/libpng/pngwio.c
        ${BASE}/third_party/libpng/pngwrite.c
        ${BASE}/third_party/libpng/pngwtran.c
        ${BASE}/third_party/libpng/pngwutil.c
        ${BASE}/third_party/libpng/arm/arm_init.c
        ${BASE}/third_party/libpng/arm/filter_neon.S
        ${BASE}/third_party/libpng/arm/filter_neon_intrinsics.c
        ${BASE}/third_party/libpng/arm/palette_neon_intrinsics.c
        ${SOURCES}
        ${ANDROID_BACKENDS}
        ${GL_BACKEND}
        )

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        shmup
        android
        EGL
        GLESv2
        z
        OpenSLES
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib})