cmake_minimum_required(VERSION 3.16)
# by @mmoroca & Arena.AI 2026 (http://arena.ai/)
project(sdl3_dos_demo LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# DOS executables are statically linked; DOS has no SDL shared-library loader.
set(SDL_SHARED OFF CACHE BOOL "Build SDL shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL static library" FORCE)
set(SDL_AUDIO ON CACHE BOOL "Build SDL audio support" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL examples" FORCE)

# SDL3 is an external dependency; its source tree must include the DOS port.
set(SDL3_DOS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDL" CACHE PATH
    "Path to the SDL3 source tree containing the DOS backend")
if(NOT EXISTS "${SDL3_DOS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "SDL3 sources were not found at '${SDL3_DOS_SOURCE_DIR}'. "
        "Clone https://github.com/libsdl-org/SDL into SDL/ or set "
        "-DSDL3_DOS_SOURCE_DIR=/path/to/SDL.")
endif()
add_subdirectory("${SDL3_DOS_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)

add_executable(hello main.c)
target_link_libraries(hello PRIVATE SDL3::SDL3)

add_executable(mistify mistify.c)
target_link_libraries(mistify PRIVATE SDL3::SDL3)

# Keep executable names within the DOS 8.3 filename limit.
add_executable(sbtest soundtest.c)
target_link_libraries(sbtest PRIVATE SDL3::SDL3)

add_executable(modplay modplay.c)
target_link_libraries(modplay PRIVATE SDL3::SDL3)

# Mode-13h-style, four-panel SDL3 reference template (8.3 executable name).
add_executable(showcase showcase.c)
target_link_libraries(showcase PRIVATE SDL3::SDL3)

# Full-screen 320x200 arcade shooter built on the Showcase template (8.3 name).
add_executable(shooter shooter.c)
target_link_libraries(shooter PRIVATE SDL3::SDL3)

# Short DOS 8.3 executable name for the obstacle-placement game.
add_executable(dino dinorun.c)
target_link_libraries(dino PRIVATE SDL3::SDL3)

# Ship the preconverted 8-bit grayscale sprites beside DINO.EXE. The program
# opens them by relative path, which also works from a DOSBox-mounted build dir.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Images"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# SHOWCASE.EXE and SHOOTER.EXE open indexed BMP sprites and PCM WAVs here.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/SHOWCASE"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# Keep the copied SHOWCASE asset folder up to date on incremental target builds.
add_custom_command(TARGET showcase POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/SHOWCASE"
            "${CMAKE_CURRENT_BINARY_DIR}/SHOWCASE")
add_custom_command(TARGET shooter POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/SHOWCASE"
            "${CMAKE_CURRENT_BINARY_DIR}/SHOWCASE")
