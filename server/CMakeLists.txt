cmake_minimum_required(VERSION 3.18)
project(net VERSION 1.0.0 LANGUAGES C CXX)

# Net.hpp uses concepts and <bit>: C++20 is required.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "srcs/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "incs/*.hpp")

add_library(net STATIC ${SOURCES} ${HEADERS})
target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/incs)

if(WIN32)
    target_link_libraries(net PUBLIC ws2_32)
    # _WIN32_WINNT=0x0A00 targets Windows 10 (WSAPoll needs Vista+).
    # LEAN_AND_MEAN / NOMINMAX keep <windows.h> light and free of min/max macros.
    target_compile_definitions(net PUBLIC
        _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(net PUBLIC Threads::Threads)
endif()

if(MSVC)
    target_compile_options(net PRIVATE /W4 /permissive-)
else()
    target_compile_options(net PRIVATE -Wall -Wextra)
endif()

# Loopback test of the platform layer. Off by default so a broken or missing
# test never blocks building the game.
option(NET_BUILD_TESTS "Build net test executables" OFF)
if(NET_BUILD_TESTS)
    enable_testing()
    add_executable(test_sockets test_sockets.cpp)
    target_link_libraries(test_sockets PRIVATE net)
    add_test(NAME test_sockets COMMAND test_sockets)
endif()