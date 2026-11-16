cmake_minimum_required(VERSION 3.18)

project(engine VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Host dependency: OpenGL only
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Vendored dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# GLFW build options (must be set BEFORE FetchContent_MakeAvailable)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

# Linux: X11 only by default so Wayland dev packages aren't required.
if(UNIX AND NOT APPLE)
    option(ENGINE_GLFW_WAYLAND "Build GLFW with Wayland support" OFF)
    set(GLFW_BUILD_WAYLAND ${ENGINE_GLFW_WAYLAND} CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
endif()

# GLM: header-only is all we need
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.2
    GIT_SHALLOW    TRUE
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glm
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw
)

# stb has no CMakeLists: download only.
# Consider pinning GIT_TAG to a commit hash for reproducible builds.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb
)

FetchContent_MakeAvailable(glm glfw stb)

# Pick whichever GLM target this version exposes
if(TARGET glm::glm-header-only)
    set(ENGINE_GLM_TARGET glm::glm-header-only)
elseif(TARGET glm::glm)
    set(ENGINE_GLM_TARGET glm::glm)
else()
    add_library(engine_glm INTERFACE)
    target_include_directories(engine_glm SYSTEM INTERFACE ${glm_SOURCE_DIR})
    set(ENGINE_GLM_TARGET engine_glm)
endif()

# ---------------------------------------------------------------------------
# GLAD (committed in vendor/glad)
# ---------------------------------------------------------------------------
add_library(glad STATIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/src/glad.c)
target_include_directories(glad SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/include
)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})   # dlopen on Linux/macOS

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "srcs/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "incs/*.hpp")

add_library(engine STATIC ${SOURCES} ${HEADERS})

target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/incs)
target_include_directories(engine SYSTEM PUBLIC ${stb_SOURCE_DIR})

# glfw carries its own include dirs and platform libs (X11, Cocoa, IOKit, gdi32...)
target_link_libraries(engine PUBLIC
    glad
    glfw
    ${ENGINE_GLM_TARGET}
    OpenGL::GL
    Threads::Threads
)

# ---------------------------------------------------------------------------
# GLM configuration (PUBLIC: engine and ft_vox must compile GLM identically)
# Applies to ALL build types so GLM is always optimized.
# ---------------------------------------------------------------------------
target_compile_definitions(engine PUBLIC
    GLM_FORCE_INLINE          # always_inline / __forceinline, works even at -O0
    GLM_ENABLE_EXPERIMENTAL   # allow <glm/gtx/...>; harmless if unused
)

# ---------------------------------------------------------------------------
# SIMD level (x86-64 only). Default AVX2+FMA: needs Haswell (2013) / Zen or newer.
# Use -DENGINE_SIMD=SSE2 for binaries that must run on any x86-64 CPU.
# ---------------------------------------------------------------------------
set(ENGINE_SIMD "AVX2" CACHE STRING "SIMD level for GLM and engine code (x86-64 only)")
set_property(CACHE ENGINE_SIMD PROPERTY STRINGS SSE2 SSE42 AVX AVX2)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64|x64)"
   AND NOT CMAKE_OSX_ARCHITECTURES MATCHES ";")   # skip universal macOS builds
    if(ENGINE_SIMD STREQUAL "SSE42")
        if(NOT MSVC)
            target_compile_options(engine PUBLIC -msse4.2)
        endif()
        target_compile_definitions(engine PUBLIC GLM_FORCE_SSE42)
    elseif(ENGINE_SIMD STREQUAL "AVX")
        if(MSVC)
            target_compile_options(engine PUBLIC /arch:AVX)
        else()
            target_compile_options(engine PUBLIC -mavx)
        endif()
        target_compile_definitions(engine PUBLIC GLM_FORCE_AVX)
    elseif(ENGINE_SIMD STREQUAL "AVX2")
        if(MSVC)
            target_compile_options(engine PUBLIC /arch:AVX2)
        else()
            target_compile_options(engine PUBLIC -mavx2 -mfma)
        endif()
        target_compile_definitions(engine PUBLIC GLM_FORCE_AVX2)
    endif()
    message(STATUS "Engine SIMD level: ${ENGINE_SIMD}")
endif()

# ---------------------------------------------------------------------------
# Platform specifics
# ---------------------------------------------------------------------------
if(WIN32)
    target_compile_definitions(engine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(APPLE)
    target_compile_definitions(engine PUBLIC GL_SILENCE_DEPRECATION)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS engine ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY incs/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/engine)