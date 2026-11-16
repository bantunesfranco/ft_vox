cmake_minimum_required(VERSION 3.18)

project(ft_vox VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Language standard / build type
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Optimization: applied before any subproject so everything inherits it
# ---------------------------------------------------------------------------

# Optimize Debug builds too (keeps symbols and asserts). Makes header-only
# GLM fast regardless of build type.
option(FT_VOX_OPTIMIZE_DEBUG "Optimize Debug builds (keeps -g and asserts)" ON)
if(FT_VOX_OPTIMIZE_DEBUG)
    if(MSVC)
        foreach(var CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
            string(REGEX REPLACE "/RTC[1csu]+" "" ${var} "${${var}}")  # incompatible with /O2
            string(REGEX REPLACE "/Ob0"        "/Ob2" ${var} "${${var}}")
            string(REGEX REPLACE "/Od"         "/O2"  ${var} "${${var}}")
            string(REGEX REPLACE "/ZI"         "/Zi"  ${var} "${${var}}")  # edit-and-continue conflicts with /O2
            string(REGEX REPLACE "/JMC"        ""     ${var} "${${var}}")
        endforeach()
    else()
        set(CMAKE_C_FLAGS_DEBUG   "-g -O2")
        set(CMAKE_CXX_FLAGS_DEBUG "-g -O2")
    endif()
endif()

# Link-time optimization for Release / RelWithDebInfo
cmake_policy(SET CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)   # also for subprojects with old cmake_minimum_required
include(CheckIPOSupported)
check_ipo_supported(RESULT FT_VOX_IPO_OK OUTPUT FT_VOX_IPO_MSG LANGUAGES C CXX)
if(FT_VOX_IPO_OK)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE        ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    message(STATUS "LTO/IPO enabled for Release builds")
else()
    message(STATUS "LTO/IPO not supported: ${FT_VOX_IPO_MSG}")
endif()

# MSVC: parallel compilation
if(MSVC)
    add_compile_options(/MP)
endif()

# ---------------------------------------------------------------------------
# Host dependency: OpenGL only
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# Sub-projects (engine provides the vendored GLFW, GLM, GLAD, stb)
# ---------------------------------------------------------------------------
add_subdirectory(engine)
add_subdirectory(server)

if(NOT TARGET glfw)
    message(FATAL_ERROR "Target 'glfw' not found. Check engine/CMakeLists.txt.")
endif()

# ---------------------------------------------------------------------------
# ImGui (fetched into vendor/imgui if missing)
# ---------------------------------------------------------------------------
include(FetchContent)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)

if(EXISTS ${IMGUI_DIR}/imgui.cpp)
    message(STATUS "Using local ImGui in vendor/imgui")
else()
    message(STATUS "ImGui not found locally. Fetching from GitHub...")
endif()

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.93.0
    GIT_SHALLOW    TRUE
    SOURCE_DIR     ${IMGUI_DIR}
)
FetchContent_MakeAvailable(imgui)   # no CMakeLists in ImGui: download only

add_library(ImGui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(ImGui SYSTEM PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(ImGui PUBLIC glfw OpenGL::GL)

# ---------------------------------------------------------------------------
# Game executable
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "srcs/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "incs/*.hpp")

add_executable(ft_vox ${SOURCES} ${HEADERS})

target_include_directories(ft_vox PRIVATE ${PROJECT_SOURCE_DIR}/incs)

# engine (PUBLIC) exports: engine/incs, glfw, glm (+ SIMD flags), glad, stb, OpenGL
# net (PUBLIC) exports: server/incs, sockets, threads
target_link_libraries(ft_vox PRIVATE
    engine
    net
    ImGui
)

# Network client test (connects to a running `ft_vox --server`). Unit tests
# run inside the game binary: `ft_vox --selftest`.
option(FT_VOX_BUILD_TESTS "Build test executables" OFF)
if(FT_VOX_BUILD_TESTS)
    add_executable(netclient_test tests/netclient_test.cpp)
    target_link_libraries(netclient_test PRIVATE net)
endif()

# ---------------------------------------------------------------------------
# Warnings: our own target only, per compiler
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(ft_vox PRIVATE /W4 /WX /permissive-)
    target_compile_definitions(ft_vox PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # -ffp-contract=off: generated terrain is bit-identical on every build (no FMA).
    target_compile_options(ft_vox PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()

# ---------------------------------------------------------------------------
# Platform specifics
# ---------------------------------------------------------------------------
if(APPLE)
    # OpenGL is deprecated on macOS; silence it so -Werror doesn't fail the build
    target_compile_definitions(ImGui PRIVATE GL_SILENCE_DEPRECATION)
elseif(WIN32)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ft_vox)
endif()