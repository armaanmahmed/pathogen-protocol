cmake_minimum_required(VERSION 3.16)
project(pathogen_protocol CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Stamp extracted files at extraction time, so bumping the raylib URL triggers
# a rebuild. The policy only exists in CMake 3.24+.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
)
FetchContent_MakeAvailable(raylib)

# Data tables and pure rules, split out so the tests can link them without
# the game loop.
add_library(pp_content STATIC src/content.cpp)
target_link_libraries(pp_content PUBLIC raylib)
target_include_directories(pp_content PUBLIC src)

# PUBLIC so the test binary inherits them.
if(APPLE)
    target_link_libraries(pp_content PUBLIC "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

add_executable(pathogen_protocol
    src/main.cpp
    src/game.cpp
    src/sim.cpp
    src/render.cpp
)
target_link_libraries(pathogen_protocol PRIVATE pp_content)

foreach(t pp_content pathogen_protocol)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
endforeach()

enable_testing()
add_executable(pp_tests tests/test_rules.cpp)
target_link_libraries(pp_tests PRIVATE pp_content)
add_test(NAME rules COMMAND pp_tests)

# Headless autopilot as integration test: fails if any room stops being clearable.
add_test(NAME smoke COMMAND pathogen_protocol --smoke --seed 1 --frames 20000)
add_test(NAME smoke_tutorial COMMAND pathogen_protocol --smoke --tutorial --seed 2 --frames 300)
