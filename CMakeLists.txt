cmake_minimum_required(VERSION 3.18 FATAL_ERROR)

project(ft_vox)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)

ExternalProject_Add(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/glfw
    BINARY_DIR ${PROJECT_SOURCE_DIR}/glfw
    CMAKE_ARGS -DGLFW_BUILD_DOCS=OFF
               -DGLFW_BUILD_TESTS=OFF
               -DGLFW_BUILD_EXAMPLES=OFF
               -DCMAKE_INSTALL_PREFIX=${PROJECT_SOURCE_DIR}/glfw
)

if (UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    find_package(EGL REQUIRED)
    find_package(Threads REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND REQUIRED wayland-client)
    pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
    pkg_check_modules(XORG REQUIRED xorg-server)

    if (NOT X11_FOUND OR NOT EGL_FOUND OR NOT Threads_FOUND OR NOT OpenGL_FOUND OR NOT PkgConfig_FOUND OR NOT WAYLAND_FOUND OR NOT XKBCOMMON_FOUND OR NOT XORG_FOUND)
        execute_process(COMMAND bash ${PROJECT_SOURCE_DIR}/install_deps.sh)
    endif()

endif()

add_executable(${PROJECT_NAME} srcs/main.cpp)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_dependencies(${PROJECT_NAME} glfw)

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/glfw/include ${PROJECT_SOURCE_DIR}/glfw/deps)

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} ${X11_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS} ${OPENGL_LIBRARIES})
endif()

target_link_libraries(${PROJECT_NAME} ${PROJECT_SOURCE_DIR}/glfw/src/libglfw3.a)