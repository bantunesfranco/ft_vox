cmake_minimum_required(VERSION 3.16)

# Project name and version
project(engine VERSION 1.0.0 LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Create the vendor directory within the engine directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

# Dependencies
include(FetchContent)

# Fetch GLM
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 0.9.9.8  # Use the latest stable version compatible with your code
)

# Fetch GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4  # Specific GLFW version tag
)

# Fetch stb_image
FetchContent_Declare(
  stb_image
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master  # Or use a specific commit/tag if needed
)

# Fetch the dependencies into the vendor directory
FetchContent_MakeAvailable(glm glfw stb_image)

# Automatically collect all source files and headers
file(GLOB_RECURSE SOURCES "srcs/*.cpp")
file(GLOB_RECURSE HEADERS "incs/*.hpp")

# Define the library target
add_library(engine ${SOURCES} ${HEADERS})

# Link the dependencies
target_link_libraries(engine PUBLIC glfw stb_image)

# Add include directories for GLM, GLFW, stb_image, and the engine headers
target_include_directories(engine PUBLIC ${glm_SOURCE_DIR})
target_include_directories(engine PUBLIC ${glfw_SOURCE_DIR}/include)
target_include_directories(engine PUBLIC ${glfw_SOURCE_DIR}/deps)
target_include_directories(engine PUBLIC ${stb_image_SOURCE_DIR})
target_include_directories(engine PUBLIC ${CMAKE_SOURCE_DIR}/engine/incs)  # Path to the headers

# Platform-specific options for x86_64 architecture
if(WIN32)
    # Windows-specific settings
    target_link_libraries(engine PUBLIC opengl32)
elseif(APPLE)
    # macOS-specific settings
    target_link_libraries(engine PUBLIC "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
elseif(UNIX)
    # Linux-specific settings
    find_package(X11 REQUIRED)
    target_link_libraries(engine PUBLIC ${X11_LIBRARIES} pthread dl)
endif()

# Set the version information for the shared library
set_target_properties(engine PROPERTIES VERSION ${PROJECT_VERSION})

# Install rules (adjust based on your needs)
install(TARGETS engine DESTINATION lib)
install(DIRECTORY incs/ DESTINATION incs)  # Ensure headers are installed properly
