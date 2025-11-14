if(NOT TARGET ${EXECUTABLE_NAME})
    message(FATAL_ERROR "FFmpeg integration expects target ${EXECUTABLE_NAME} to exist before including cmake/ffmpeg.cmake")
endif()

option(SYN_BUNDLE_FFMPEG "Build and bundle the project-managed FFmpeg binary" ON)

if(NOT SYN_BUNDLE_FFMPEG)
    return()
endif()

set(SYN_EMBEDDED_ASSETS_DIR "${CMAKE_BINARY_DIR}/embedded_assets")

if(WIN32)
    set(SYN_EMBED_ASSET_OUTPUTS "")
    function(syn_register_embedded_asset source relative output_var)
        set(dest "${CMAKE_BINARY_DIR}/${relative}")
        get_filename_component(dest_dir "${dest}" DIRECTORY)
        add_custom_command(OUTPUT "${dest}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${dest_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source}" "${dest}"
            DEPENDS "${source}"
            COMMENT "Staging ${relative} for embedding" VERBATIM)
        set(SYN_EMBED_ASSET_OUTPUTS "${SYN_EMBED_ASSET_OUTPUTS};${dest}" PARENT_SCOPE)
        string(REPLACE "\\" "/" dest_normalized "${dest}")
        set(${output_var} "${dest_normalized}" PARENT_SCOPE)
    endfunction()

    syn_register_embedded_asset("${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Medium.ttf" "embedded_assets/fonts/IBMPlexMono-Medium.ttf" SYN_ASSET_FONT_IBM_PLEX_MONO_MEDIUM)
    syn_register_embedded_asset("${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Light.ttf" "embedded_assets/fonts/IBMPlexMono-Light.ttf" SYN_ASSET_FONT_IBM_PLEX_MONO_LIGHT)
    syn_register_embedded_asset("${CMAKE_SOURCE_DIR}/assets/fonts/icons/fa-solid-900.ttf" "embedded_assets/fonts/icons/fa-solid-900.ttf" SYN_ASSET_FONT_AWESOME_SOLID)
    syn_register_embedded_asset("${CMAKE_SOURCE_DIR}/assets/luts/ReSyne_Display_v1.cube" "embedded_assets/luts/ReSyne_Display_v1.cube" SYN_ASSET_RESYNE_DISPLAY_LUT)
endif()

set(SYN_FFMPEG_SOURCE_DIR "${CMAKE_SOURCE_DIR}/vendor/ffmpeg" CACHE PATH "Path to the FFmpeg submodule")
if(WIN32)
    set(SYN_FFMPEG_SCRIPT "${CMAKE_SOURCE_DIR}/src/utilities/ffmpeg/build_ffmpeg.bat" CACHE FILEPATH "Helper script that configures and installs FFmpeg")
else()
    set(SYN_FFMPEG_SCRIPT "${CMAKE_SOURCE_DIR}/src/utilities/ffmpeg/build_ffmpeg.sh" CACHE FILEPATH "Helper script that configures and installs FFmpeg")
endif()
set(SYN_FFMPEG_BUILD_ROOT "${CMAKE_BINARY_DIR}/ffmpeg" CACHE PATH "Out-of-tree build directory for FFmpeg")
set(SYN_FFMPEG_INSTALL_PREFIX "${SYN_FFMPEG_BUILD_ROOT}/install" CACHE PATH "Installation root for the bundled FFmpeg artefacts")
if(WIN32)
    set(SYN_FFMPEG_EXECUTABLE "${SYN_FFMPEG_INSTALL_PREFIX}/bin/ffmpeg.exe" CACHE FILEPATH "Bundled FFmpeg executable path")
else()
    set(SYN_FFMPEG_EXECUTABLE "${SYN_FFMPEG_INSTALL_PREFIX}/bin/ffmpeg" CACHE FILEPATH "Bundled FFmpeg executable path")
endif()
set(SYN_FFMPEG_LICENSE_MD "${SYN_FFMPEG_INSTALL_PREFIX}/licenses/FFMPEG-LICENSE.md" CACHE FILEPATH "FFmpeg licence summary")
set(SYN_FFMPEG_LICENSE_LGPL "${SYN_FFMPEG_INSTALL_PREFIX}/licenses/LGPL-2.1.txt" CACHE FILEPATH "LGPL licence text for FFmpeg")

if(WIN32)
    set(_SYN_FFMPEG_COMMAND ${CMAKE_COMMAND} -E env
        FFMPEG_SOURCE_DIR=${SYN_FFMPEG_SOURCE_DIR}
        FFMPEG_BUILD_ROOT=${SYN_FFMPEG_BUILD_ROOT}
        FFMPEG_INSTALL_PREFIX=${SYN_FFMPEG_INSTALL_PREFIX}
        "${SYN_FFMPEG_SCRIPT}")
else()
    find_program(SYN_FFMPEG_BASH_EXECUTABLE bash)
    if(NOT SYN_FFMPEG_BASH_EXECUTABLE)
        message(WARNING "bash not found on PATH; bundled FFmpeg cannot be built automatically. Install bash or disable SYN_BUNDLE_FFMPEG.")
        return()
    endif()
    set(_SYN_FFMPEG_COMMAND ${CMAKE_COMMAND} -E env
        FFMPEG_SOURCE_DIR=${SYN_FFMPEG_SOURCE_DIR}
        FFMPEG_BUILD_ROOT=${SYN_FFMPEG_BUILD_ROOT}
        FFMPEG_INSTALL_PREFIX=${SYN_FFMPEG_INSTALL_PREFIX}
        ${SYN_FFMPEG_BASH_EXECUTABLE} ${SYN_FFMPEG_SCRIPT})
endif()

set(SYN_FFMPEG_AVAILABLE OFF)
message(STATUS "Ensuring bundled FFmpeg is available via ${SYN_FFMPEG_SCRIPT}")
execute_process(
    COMMAND ${_SYN_FFMPEG_COMMAND}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE SYN_FFMPEG_RESULT
)

if(EXISTS "${SYN_FFMPEG_EXECUTABLE}")
    set(SYN_FFMPEG_AVAILABLE ON)
else()
    if(DEFINED SYN_FFMPEG_RESULT)
        message(WARNING "Bundled FFmpeg is unavailable (build script exited with code ${SYN_FFMPEG_RESULT}). Video export will rely on a system-wide FFmpeg path.")
    else()
        message(WARNING "Bundled FFmpeg is unavailable. Video export will rely on a system-wide FFmpeg path.")
    endif()
endif()

add_custom_target(syn_ffmpeg_build
    COMMAND ${_SYN_FFMPEG_COMMAND}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Forcing bundled FFmpeg rebuild"
)

if(WIN32)
    if(SYN_FFMPEG_AVAILABLE)
        syn_register_embedded_asset("${SYN_FFMPEG_EXECUTABLE}" "embedded_assets/bin/ffmpeg${CMAKE_EXECUTABLE_SUFFIX}" SYN_FFMPEG_EXECUTABLE_RESOURCE)
        message(STATUS "Configuring app.rc with FFmpeg path: ${SYN_FFMPEG_EXECUTABLE}")
        set(SYN_FFMPEG_RC_LINE "IDR_FFMPEG_BINARY             RCDATA \"${SYN_FFMPEG_EXECUTABLE_RESOURCE}\"")
    else()
        message(WARNING "Continuing without a bundled FFmpeg binary; set RESYNE_FFMPEG_PATH or run src/utilities/ffmpeg/build_ffmpeg.sh before rebuilding to restore video export functionality.")
        set(SYN_FFMPEG_RC_LINE "")
    endif()

    file(READ ${CMAKE_SOURCE_DIR}/src/platforms/dx12/app.rc.in APP_RC_TEMPLATE)
    string(REPLACE "@SYN_FFMPEG_EXECUTABLE_RESOURCE@" "${SYN_FFMPEG_EXECUTABLE_RESOURCE}" APP_RC_CONTENT "${APP_RC_TEMPLATE}")
    string(REPLACE "@SYN_ASSET_FONT_IBM_PLEX_MONO_MEDIUM@" "${SYN_ASSET_FONT_IBM_PLEX_MONO_MEDIUM}" APP_RC_CONTENT "${APP_RC_CONTENT}")
    string(REPLACE "@SYN_ASSET_FONT_IBM_PLEX_MONO_LIGHT@" "${SYN_ASSET_FONT_IBM_PLEX_MONO_LIGHT}" APP_RC_CONTENT "${APP_RC_CONTENT}")
    string(REPLACE "@SYN_ASSET_FONT_AWESOME_SOLID@" "${SYN_ASSET_FONT_AWESOME_SOLID}" APP_RC_CONTENT "${APP_RC_CONTENT}")
    string(REPLACE "@SYN_ASSET_RESYNE_DISPLAY_LUT@" "${SYN_ASSET_RESYNE_DISPLAY_LUT}" APP_RC_CONTENT "${APP_RC_CONTENT}")

    if(NOT SYN_FFMPEG_AVAILABLE)
        string(REGEX REPLACE "IDR_FFMPEG_BINARY[^\n]*\n" "" APP_RC_CONTENT "${APP_RC_CONTENT}")
    endif()

    file(WRITE ${CMAKE_BINARY_DIR}/app.rc "${APP_RC_CONTENT}")

    if(SYN_EMBED_ASSET_OUTPUTS)
        list(REMOVE_DUPLICATES SYN_EMBED_ASSET_OUTPUTS)
        add_custom_target(syn_embed_assets DEPENDS ${SYN_EMBED_ASSET_OUTPUTS})
        add_dependencies(${EXECUTABLE_NAME} syn_embed_assets)
    endif()
endif()

if(NOT SYN_FFMPEG_AVAILABLE)
    return()
endif()

if(APPLE)
    if(BUILD_MACOS_BUNDLE)
        set(SYN_FFMPEG_BUNDLE_BIN "${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.app/Contents/Resources/bin")
        set(SYN_FFMPEG_BUNDLE_RES "${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.app/Contents/Resources")
        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYN_FFMPEG_BUNDLE_RES}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYN_FFMPEG_BUNDLE_BIN}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SYN_FFMPEG_EXECUTABLE}" "${SYN_FFMPEG_BUNDLE_BIN}/ffmpeg"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SYN_FFMPEG_LICENSE_MD}" "${SYN_FFMPEG_BUNDLE_RES}/FFMPEG-LICENSE.md"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SYN_FFMPEG_LICENSE_LGPL}" "${SYN_FFMPEG_BUNDLE_RES}/LGPL-2.1.txt"
            COMMENT "Bundling FFmpeg assets"
        )
    else()
        set(SYN_FFMPEG_RUNTIME_DIR "${CMAKE_BINARY_DIR}/assets/bin")
        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYN_FFMPEG_RUNTIME_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SYN_FFMPEG_EXECUTABLE}" "${SYN_FFMPEG_RUNTIME_DIR}/ffmpeg"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SYN_FFMPEG_LICENSE_MD}" "${CMAKE_BINARY_DIR}/assets/FFMPEG-LICENSE.md"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SYN_FFMPEG_LICENSE_LGPL}" "${CMAKE_BINARY_DIR}/assets/LGPL-2.1.txt"
            COMMENT "Copying FFmpeg for non-bundle macOS builds"
        )
    endif()
else()
    set(SYN_FFMPEG_RUNTIME_DIR "${CMAKE_BINARY_DIR}/assets/bin")
    add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SYN_FFMPEG_RUNTIME_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SYN_FFMPEG_EXECUTABLE}" "${SYN_FFMPEG_RUNTIME_DIR}/ffmpeg${CMAKE_EXECUTABLE_SUFFIX}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SYN_FFMPEG_LICENSE_MD}" "${CMAKE_BINARY_DIR}/assets/FFMPEG-LICENSE.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SYN_FFMPEG_LICENSE_LGPL}" "${CMAKE_BINARY_DIR}/assets/LGPL-2.1.txt"
        COMMENT "Copying FFmpeg for desktop builds"
    )
endif()
