cmake_minimum_required(VERSION 3.16)

# Project name and version
project(ft_vox VERSION 1.0.0 LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set global flags for all build types
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -fsanitize=address")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g")

# Create the vendor directory within the project directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

# Dependencies
include(FetchContent)

# Fetch ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.0
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
)

# Fetch the dependencies into the vendor directory
FetchContent_MakeAvailable(imgui)

# Automatically collect all ImGui source files
file(GLOB IMGUI_SOURCES
  "${imgui_SOURCE_DIR}/*.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)

# Add ImGui as a static library
add_library(ImGui STATIC ${IMGUI_SOURCES})

# Add include directories for ImGui and GLFW
target_include_directories(ImGui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_include_directories(ImGui PUBLIC ${CMAKE_SOURCE_DIR}/engine/vendor/glfw/include)

# Define the executable target for the game
file(GLOB_RECURSE SOURCES "srcs/*.cpp")
file(GLOB_RECURSE HEADERS "incs/*.hpp")
add_executable(ft_vox ${SOURCES} ${HEADERS})

# Add the engine directory as a subdirectory
add_subdirectory(engine)

# Find and link OpenGL
find_package(OpenGL REQUIRED)

# Link the ImGui and engine libraries to your executable
target_link_libraries(ft_vox PRIVATE engine ImGui)
target_link_libraries(ft_vox PRIVATE stb_image)

# Add include directories for the game and engine headers
target_include_directories(ft_vox PRIVATE ${PROJECT_SOURCE_DIR}/incs)
target_include_directories(ft_vox PRIVATE ${CMAKE_SOURCE_DIR}/engine/incs)
target_include_directories(ft_vox PRIVATE ${CMAKE_SOURCE_DIR}/engine/vendor/glfw/include)
target_include_directories(ft_vox PRIVATE ${CMAKE_SOURCE_DIR}/engine/vendor/glfw/deps)
