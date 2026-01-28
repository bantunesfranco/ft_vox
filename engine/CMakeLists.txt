cmake_minimum_required(VERSION 3.18)
project(engine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create the vendor directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

# Dependencies
include(FetchContent)

FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.2
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glm
)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw
)

FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb
)

FetchContent_MakeAvailable(glm glfw stb)

# GLAD library
add_library(glad STATIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/src/glad.c")
target_include_directories(glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/include")

# Collect source files
file(GLOB_RECURSE SOURCES "srcs/*.cpp")
file(GLOB_RECURSE HEADERS "incs/*.hpp")

# Define the library target
add_library(engine STATIC ${SOURCES} ${HEADERS} incs/Voxel.hpp)

# Apply compiler flags ONLY to engine, not dependencies
if(MSVC)
    target_compile_options(engine PRIVATE /W4 /WX)
    target_compile_options(engine PRIVATE $<$<CONFIG:Release>:/O2 /Z7>)
else()
    target_compile_options(engine PRIVATE -Wall -Wextra -Werror)
    target_compile_options(engine PRIVATE $<$<CONFIG:Release>:-O2 -g>)
    target_compile_options(engine PRIVATE $<$<CONFIG:Debug>:-g -O0>)

    # ASan only on Linux/Mac, not MinGW
    if(UNIX AND NOT APPLE)
        target_compile_options(ft_vox PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
        target_link_options(ft_vox PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
    endif()
endif()

# Link dependencies
target_link_libraries(engine PUBLIC glad glfw)

# Include directories
target_include_directories(engine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/incs
        ${glfw_SOURCE_DIR}/include
        ${glm_SOURCE_DIR}
        ${stb_SOURCE_DIR}
)

# Platform-specific linking
if(WIN32)
    target_link_libraries(engine PUBLIC opengl32)
elseif(APPLE)
    target_link_libraries(engine PUBLIC "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
elseif(UNIX AND NOT APPLE)
    target_link_libraries(engine PUBLIC OpenGL pthread dl)
endif()

# Version info
set_target_properties(engine PROPERTIES VERSION ${PROJECT_VERSION})

# Installation
install(TARGETS engine DESTINATION lib)
install(DIRECTORY incs/ DESTINATION include)