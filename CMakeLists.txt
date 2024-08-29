cmake_minimum_required(VERSION 3.16)

# Project name and version
project(ft_vox VERSION 1.0.0 LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set global flags for all build types
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -Wextra ")

# Set specific flags depending on the build type
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -fsanitize=address")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g")

# Add source files
file(GLOB_RECURSE SOURCES "srcs/*.cpp")

# Define the executable target
add_executable(ft_vox ${SOURCES})

# Add the engine directory as a subdirectory
add_subdirectory(engine)

# Link the engine library
target_link_libraries(ft_vox PRIVATE engine)

# Add include directories for game headers and engine headers
target_include_directories(ft_vox PRIVATE ${PROJECT_SOURCE_DIR}/incs)
target_include_directories(ft_vox PRIVATE ${PROJECT_SOURCE_DIR}/engine/incs)
