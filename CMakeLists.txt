cmake_minimum_required(VERSION 3.13)

set(SYNESTHESIA_VERSION_MAJOR 2025)
set(SYNESTHESIA_VERSION_MINOR 11)
set(SYNESTHESIA_VERSION_PATCH 14)
set(SYNESTHESIA_VERSION "${SYNESTHESIA_VERSION_MAJOR}.${SYNESTHESIA_VERSION_MINOR}.${SYNESTHESIA_VERSION_PATCH}")

project(Synesthesia VERSION ${SYNESTHESIA_VERSION})

set(EXECUTABLE_NAME "Synesthesia")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui")
set(IMPLOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/implot")
set(KISSFFT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/kissfft")
set(JSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/json")
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw")
set(PORTAUDIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/portaudio")
set(RTMIDI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/rtmidi")

option(BUILD_MACOS_BUNDLE "Build as macOS .app bundle" OFF)
option(ENABLE_NEON_OPTIMISATIONS "Enable ARM NEON SIMD optimisations" ON)
option(ENABLE_API_SERVER "Enable cross-application colour streaming API (macOS only)" OFF)
option(ENABLE_MIDI "Enable MIDI input support" ON)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/updating/version.h.in"
    "${CMAKE_BINARY_DIR}/version.h"
    @ONLY
)

include(cmake/dependencies.cmake)
include(cmake/optimisations.cmake)
include(cmake/sources.cmake)

if(APPLE OR UNIX)
    set(ENABLE_API_SERVER ON)
    if(APPLE)
        message(STATUS "API server enabled for macOS")
    else()
        message(STATUS "API server enabled for Linux")
    endif()
elseif(ENABLE_API_SERVER)
    message(WARNING "API server is only supported on UNIX platforms, disabling")
    set(ENABLE_API_SERVER OFF)
endif()

add_api_sources()
add_midi_sources()
add_neon_sources()
add_sse_sources()

include(cmake/platform.cmake)

configure_include_directories()
apply_neon_optimisations()
apply_sse_optimisations()

target_compile_definitions(${EXECUTABLE_NAME} PRIVATE
    SYNESTHESIA_VERSION_MAJOR=${SYNESTHESIA_VERSION_MAJOR}
    SYNESTHESIA_VERSION_MINOR=${SYNESTHESIA_VERSION_MINOR}
    SYNESTHESIA_VERSION_PATCH=${SYNESTHESIA_VERSION_PATCH}
    SYNESTHESIA_VERSION_STRING="${SYNESTHESIA_VERSION}"
)

if(ENABLE_API_SERVER)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE ENABLE_API_SERVER)
endif()

if(ENABLE_MIDI)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE ENABLE_MIDI)
endif()

if(EXPORT_MANIM_DATA)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE EXPORT_MANIM_DATA)
endif()

set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)