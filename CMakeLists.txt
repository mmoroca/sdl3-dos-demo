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

# Multi-font Spanish pangram showcase using fonts/font_3x5.h, 5x7.h, 8x8.h.
add_executable(fontdemo fontdemo.c)
target_link_libraries(fontdemo PRIVATE SDL3::SDL3)

# Independent endless procedural platform runner; DOS filename SKYHOP.EXE.
add_executable(skyhop skyhop.c)
target_link_libraries(skyhop PRIVATE SDL3::SDL3)
target_compile_options(skyhop PRIVATE -Wall -Wextra -Werror)

# TETRISDL3: a separate, educational 320x200 game; 8.3 DOS filename TETSDL3.EXE.
add_executable(tetsdl3 tetrisdl3.c)
target_link_libraries(tetsdl3 PRIVATE SDL3::SDL3)
target_compile_options(tetsdl3 PRIVATE -Wall -Wextra -Werror)
# The game loads seven offline-converted 8.3 PCM WAVs, three shared-palette
# 320x200 BMP backgrounds and one 111x25 transparent BMP menu logo.
# Original OGGs stay in docs/ and authored/supplied PNGs in tetris-src/.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/TETRIS"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
# This dependency refreshes BOTH folders even when the EXE itself is already
# current. A corrected WAV/BMP must not require touching C code to be copied.
add_custom_target(tetsdl3_assets
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/SPLASH"
            "${CMAKE_CURRENT_BINARY_DIR}/SPLASH"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/TETRIS"
            "${CMAKE_CURRENT_BINARY_DIR}/TETRIS"
    # Reused build directories may contain the old unused full-screen MENU.BMP.
    # Only this obsolete copy is removed; the three backgrounds + mini logo stay.
    COMMAND "${CMAKE_COMMAND}" -E remove -f
            "${CMAKE_CURRENT_BINARY_DIR}/TETRIS/MENU.BMP"
    COMMENT "Refreshing TETRISDL3 splash, three backgrounds, mini logo and seven PCM tracks")
add_dependencies(tetsdl3 tetsdl3_assets)

# Host-only regression suite: builds without affecting the DOS executable.
option(SKYHOP_BUILD_TESTS "Build native SKYHOP regression tests" OFF)
if(SKYHOP_BUILD_TESTS AND NOT DJGPP)
    enable_testing()
    add_executable(skyhop_tests tests/skyhop_tests.c)
    target_link_libraries(skyhop_tests PRIVATE SDL3::SDL3)
    target_compile_options(skyhop_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME skyhop_regressions COMMAND skyhop_tests)
    set_tests_properties(skyhop_regressions PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy")
    # Shared header-only intro: checks borrowed palette/audio, fade and Esc.
    # Only compiled in the host regression build.
    add_executable(sdl3_splash_tests tests/sdl3_splash_tests.c)
    target_link_libraries(sdl3_splash_tests PRIVATE SDL3::SDL3)
    target_compile_options(sdl3_splash_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME sdl3_splash_regressions COMMAND sdl3_splash_tests)
    set_tests_properties(sdl3_splash_regressions PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy")
endif()

# Independently opt in to TETRISDL3 rules and SDL/offscreen integration tests.
option(TETRIS_BUILD_TESTS "Build native TETRISDL3 regression tests" OFF)
if(TETRIS_BUILD_TESTS AND NOT DJGPP)
    enable_testing()
    # The entire TETRISDL3 board model is SDL-free: test it without a window.
    add_executable(tetris_rules_tests tests/tetris_rules_tests.c)
    target_compile_options(tetris_rules_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME tetris_rules_regressions COMMAND tetris_rules_tests)
    add_executable(tetris_scores_tests tests/tetris_scores_tests.c)
    target_compile_options(tetris_scores_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME tetris_scores_regressions COMMAND tetris_scores_tests)
    add_executable(tetris_app_tests tests/tetris_app_tests.c)
    target_link_libraries(tetris_app_tests PRIVATE SDL3::SDL3)
    target_compile_options(tetris_app_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME tetris_app_regressions COMMAND tetris_app_tests)
    set_tests_properties(tetris_app_regressions PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy")
    # Offline, Pillow-free regression of three backgrounds, transparent logo
    # and shared palette. Python is optional; it is NEVER required for DOS.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME tetris_art_regressions
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_convert_tetris_art.py")
    endif()
endif()

# VGA/VBE versus SDL3 video test card, independent of games and media.
# Built by the normal default/all target alongside the other DOS executables.
add_executable(vtest vtest.c)
target_link_libraries(vtest PRIVATE SDL3::SDL3)
target_compile_options(vtest PRIVATE -Wall -Wextra -Werror)
if(CMAKE_SYSTEM_NAME STREQUAL "DOS")
    target_compile_options(vtest PRIVATE -march=i486 -mtune=i486 -Os)
endif()

# Pure chart regression is opt-in and requires no video mode or media files.
option(VTEST_BUILD_TESTS "Build native regression for the VTEST adjustment card" OFF)
if(VTEST_BUILD_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "DOS")
    enable_testing()
    add_executable(vtest_chart_tests tests/vtest_chart_tests.c)
    target_compile_options(vtest_chart_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME vtest_chart_regressions COMMAND vtest_chart_tests)
endif()

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

# Copy only the PLATFORM art for the new runner; never replace Dino's BMP set.
add_custom_command(TARGET skyhop POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/Images/PLATFORM"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Images/PLATFORM"
            "${CMAKE_CURRENT_BINARY_DIR}/Images/PLATFORM")

# Resources shared by Dino, Shooter and SKYHOP. Keep DOS 8.3 filenames and
# copy again after incremental builds, not only at initial CMake configure.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/SPLASH"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
foreach(splash_game IN ITEMS dino shooter skyhop)
    add_custom_command(TARGET ${splash_game} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/SPLASH"
                "${CMAKE_CURRENT_BINARY_DIR}/SPLASH")
endforeach()
