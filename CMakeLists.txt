cmake_minimum_required(VERSION 3.18)
project(ft_vox VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

include(FetchContent)

FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.0
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
)

FetchContent_MakeAvailable(imgui)

file(GLOB IMGUI_SOURCES
        "${imgui_SOURCE_DIR}/*.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)

add_library(ImGui STATIC ${IMGUI_SOURCES})
target_include_directories(ImGui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)
target_include_directories(ImGui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/engine/vendor/glfw/include)

# Add engine subdirectory
add_subdirectory(engine)

# Collect sources BEFORE creating executable
file(GLOB_RECURSE SOURCES "srcs/*.cpp")
file(GLOB_RECURSE HEADERS "incs/*.hpp")

# Create executable
add_executable(ft_vox ${SOURCES} ${HEADERS})

# Find OpenGL
find_package(OpenGL REQUIRED)

# Link libraries
target_link_libraries(ft_vox PRIVATE engine ImGui ${OPENGL_LIBRARIES})

# Include directories
target_include_directories(ft_vox PRIVATE
        ${PROJECT_SOURCE_DIR}/incs
        ${CMAKE_SOURCE_DIR}/engine/incs
        ${CMAKE_SOURCE_DIR}/engine/vendor/glfw/include
        ${CMAKE_SOURCE_DIR}/engine/vendor/stb_image
)

# Apply compiler flags ONLY AFTER creating executable
if(MSVC)
    target_compile_options(ft_vox PRIVATE /W4 /WX)
    target_compile_options(ft_vox PRIVATE $<$<CONFIG:Release>:/O2 /Zi>)
else()
    target_compile_options(ft_vox PRIVATE -Wall -Wextra -Werror)
    target_compile_options(ft_vox PRIVATE $<$<CONFIG:Release>:-O2 -g>)
    target_compile_options(ft_vox PRIVATE $<$<CONFIG:Debug>:-g -O0>)

    # ASan only on Linux/Mac, not MinGW
    if(UNIX AND NOT APPLE)
        target_compile_options(ft_vox PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
        target_link_options(ft_vox PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
    endif()
endif()

# Platform-specific settings
if(MSVC)
    set_target_properties(ft_vox PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()