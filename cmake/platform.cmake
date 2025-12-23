if(WIN32)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/app.ico
        ${CMAKE_BINARY_DIR}/app.ico COPYONLY)
    configure_file(${SRC_DIR}/platforms/dx12/resource.h
        ${CMAKE_BINARY_DIR}/resource.h COPYONLY)

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

    file(READ ${CMAKE_SOURCE_DIR}/src/platforms/dx12/app.rc.in APP_RC_TEMPLATE)
    string(REPLACE "@SYN_ASSET_FONT_IBM_PLEX_MONO_MEDIUM@" "${SYN_ASSET_FONT_IBM_PLEX_MONO_MEDIUM}" APP_RC_CONTENT "${APP_RC_TEMPLATE}")
    string(REPLACE "@SYN_ASSET_FONT_IBM_PLEX_MONO_LIGHT@" "${SYN_ASSET_FONT_IBM_PLEX_MONO_LIGHT}" APP_RC_CONTENT "${APP_RC_CONTENT}")
    string(REPLACE "@SYN_ASSET_FONT_AWESOME_SOLID@" "${SYN_ASSET_FONT_AWESOME_SOLID}" APP_RC_CONTENT "${APP_RC_CONTENT}")
    string(REPLACE "@SYN_ASSET_RESYNE_DISPLAY_LUT@" "${SYN_ASSET_RESYNE_DISPLAY_LUT}" APP_RC_CONTENT "${APP_RC_CONTENT}")

    file(WRITE ${CMAKE_BINARY_DIR}/app.rc "${APP_RC_CONTENT}")
endif()

if(WIN32)
    if(VULKAN_WINDOWS)
        message(STATUS "Configuring for Windows (Vulkan)")
        find_package(Vulkan REQUIRED)
        message(STATUS "Found Vulkan: ${Vulkan_LIBRARIES}")
        list(APPEND SOURCES
            ${SRC_DIR}/platforms/vulkan/main.cpp
            ${CMAKE_BINARY_DIR}/app.rc
            ${SRC_DIR}/ui/styling/system_theme/system_theme_detector.cpp
        )
    else()
        message(STATUS "Configuring for Windows (DirectX 12)")
        list(APPEND SOURCES
            ${SRC_DIR}/platforms/dx12/main.cpp
            ${CMAKE_BINARY_DIR}/app.rc
            ${SRC_DIR}/ui/styling/system_theme/system_theme_detector.cpp
        )
        set(DX12_LIBS d3d12 dxgi d3dcompiler)
    endif()
elseif(APPLE)
    message(STATUS "Configuring for macOS (Metal)")
    list(APPEND SOURCES
        ${SRC_DIR}/platforms/metal/main.mm
        ${SRC_DIR}/ui/styling/system_theme/system_theme_detector.mm
        ${SRC_DIR}/ui/input/trackpad_gestures_mac.mm
    )
    set(OBJC_FLAGS "-ObjC++ -fobjc-arc -fobjc-weak")
else()
    message(STATUS "Configuring for Linux (Vulkan)")
    list(APPEND SOURCES
        ${SRC_DIR}/platforms/vulkan/main.cpp
        ${SRC_DIR}/ui/styling/system_theme/system_theme_detector.cpp
    )
endif()

if(APPLE OR UNIX)
    list(APPEND SOURCES
        ${SRC_DIR}/utilities/cli/cli.cpp
        ${SRC_DIR}/utilities/cli/headless.cpp
    )
    if(APPLE)
        message(STATUS "Added CLI sources to build for macOS")
    else()
        message(STATUS "Added CLI sources to build for Linux")
    endif()
endif()

if(WIN32)
    add_executable(${EXECUTABLE_NAME} WIN32 ${SOURCES})
elseif(APPLE)
    if(BUILD_MACOS_BUNDLE)
        add_executable(${EXECUTABLE_NAME} MACOSX_BUNDLE ${SOURCES})
    else()
        add_executable(${EXECUTABLE_NAME} ${SOURCES})
    endif()
else()
    add_executable(${EXECUTABLE_NAME} ${SOURCES})
endif()

if(WIN32 AND SYN_EMBED_ASSET_OUTPUTS)
    list(REMOVE_DUPLICATES SYN_EMBED_ASSET_OUTPUTS)
    add_custom_target(syn_embed_assets DEPENDS ${SYN_EMBED_ASSET_OUTPUTS})
    add_dependencies(${EXECUTABLE_NAME} syn_embed_assets)
endif()

if(WIN32)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
    target_link_options(${EXECUTABLE_NAME} PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
        $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
        $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>
        $<$<CONFIG:Debug>:/ENTRY:mainCRTStartup>
    )
elseif(APPLE)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE
        "-Wall" "-Wextra" "-Wformat" "-Wpedantic"
        "-Wunused" "-Wuninitialized" "-Wshadow"
        "-Wconversion" "-Wsign-conversion" "-Wfloat-conversion"
        "-Wnull-dereference" "-Wdouble-promotion"
        "-Wmissing-include-dirs" "-Wundef" "-Wredundant-decls"
        "-Woverloaded-virtual" "-Wnon-virtual-dtor"
        "-O3" "-ffast-math" "-march=native"
    )

    set_source_files_properties(${SRC_DIR}/platforms/metal/main.mm PROPERTIES COMPILE_FLAGS "${OBJC_FLAGS}")
    set_source_files_properties(${SRC_DIR}/ui/styling/system_theme/system_theme_detector.mm PROPERTIES COMPILE_FLAGS "${OBJC_FLAGS}")
    set_source_files_properties(${SRC_DIR}/ui/input/trackpad_gestures_mac.mm PROPERTIES COMPILE_FLAGS "${OBJC_FLAGS}")
    set_source_files_properties(${SRC_DIR}/ui/updating/update.cpp PROPERTIES COMPILE_FLAGS "-Wno-nan-infinity-disabled")

    set_property(SOURCE ${SRC_DIR}/ui/ui.cpp APPEND PROPERTY COMPILE_OPTIONS "-Wno-c99-extensions")
    set_property(SOURCE ${SRC_DIR}/ui/controls/controls.cpp APPEND PROPERTY COMPILE_OPTIONS "-Wno-c99-extensions")
    set_property(SOURCE ${SRC_DIR}/api/common/serialisation.cpp APPEND PROPERTY COMPILE_OPTIONS "-Wno-c99-extensions")
    set_property(SOURCE ${SRC_DIR}/api/common/transport.cpp APPEND PROPERTY COMPILE_OPTIONS "-Wno-c99-extensions")
    set_property(SOURCE ${SRC_DIR}/api/server/api_server.cpp APPEND PROPERTY COMPILE_OPTIONS "-Wno-c99-extensions")
    set_property(SOURCE ${SRC_DIR}/api/synesthesia_api_integration.cpp APPEND PROPERTY COMPILE_OPTIONS "-Wno-c99-extensions")
    set_property(SOURCE ${SRC_DIR}/utilities/cli/headless.cpp APPEND PROPERTY COMPILE_OPTIONS "-Wno-c99-extensions")
else()
    target_compile_options(${EXECUTABLE_NAME} PRIVATE
        "-Wall" "-Wextra" "-Wformat" "-Wpedantic"
        "-O3" "-ffast-math" "-march=native"
    )
endif()

if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(METALKIT_FRAMEWORK MetalKit REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(COREMIDI_FRAMEWORK CoreMIDI REQUIRED)

    target_link_libraries(${EXECUTABLE_NAME} PRIVATE
        ${METAL_FRAMEWORK}
        ${METALKIT_FRAMEWORK}
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${COREMIDI_FRAMEWORK}
        ${GLFW_TARGET}
        ${PORTAUDIO_TARGET}
        nlohmann_json::nlohmann_json
        vendor_imgui
        vendor_implot
        vendor_kissfft
        vendor_lodepng
        vendor_imgui_backends
        m
    )

    if(ENABLE_MIDI AND RTMIDI_TARGET)
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${RTMIDI_TARGET})
    endif()
elseif(WIN32)
    if(VULKAN_WINDOWS)
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE
            ${GLFW_TARGET}
            ${PORTAUDIO_TARGET}
            Vulkan::Vulkan
            nlohmann_json::nlohmann_json
            vendor_imgui
            vendor_implot
            vendor_kissfft
            vendor_lodepng
            vendor_imgui_backends
        )
    else()
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE
            ${GLFW_TARGET}
            ${PORTAUDIO_TARGET}
            ${DX12_LIBS}
            nlohmann_json::nlohmann_json
            vendor_imgui
            vendor_implot
            vendor_kissfft
            vendor_lodepng
            vendor_imgui_backends
            windowsapp
        )
    endif()

    if(ENABLE_MIDI AND RTMIDI_TARGET)
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${RTMIDI_TARGET})
    endif()
else()
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE
        ${GLFW_TARGET}
        ${PORTAUDIO_TARGET}
        Vulkan::Vulkan
        ${ALSA_LIBRARIES}
        nlohmann_json::nlohmann_json
        vendor_imgui
        vendor_implot
        vendor_kissfft
        vendor_lodepng
        vendor_imgui_backends
        dl
        pthread
        m
    )

    if(ENABLE_MIDI AND RTMIDI_TARGET)
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${RTMIDI_TARGET})
    endif()
endif()

if(APPLE AND BUILD_MACOS_BUNDLE)
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
        MACOSX_BUNDLE_ICON_FILE "app.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "Synesthesia"
        MACOSX_BUNDLE_BUNDLE_VERSION "${SYNESTHESIA_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${SYNESTHESIA_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "Synesthesia ${SYNESTHESIA_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 Jack Gannon | MIT License"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.jackgannon.Synesthesia"
        MACOSX_BUNDLE_EXECUTABLE_NAME "${EXECUTABLE_NAME}"
        RESOURCE "${ICON_SRC}"
    )

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/mac/Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/mac/entitlements.plist ${CMAKE_BINARY_DIR}/entitlements.plist COPYONLY)

    set(ICON_SRC "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/app.icns")
    set(ICON_DEST "${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.app/Contents/Resources/app.icns")

    add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.app/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${ICON_SRC}" "${ICON_DEST}"
        COMMENT "Copying app icon to bundle resources"
    )

    add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.app/Contents/Resources/assets"
        COMMENT "Copying assets directory to bundle resources"
    )
endif()

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    install(TARGETS ${EXECUTABLE_NAME}
        RUNTIME DESTINATION bin
    )
    install(FILES
        $<TARGET_RUNTIME_DLLS:${EXECUTABLE_NAME}>
        DESTINATION bin
    )
elseif(APPLE)
    install(TARGETS ${EXECUTABLE_NAME} BUNDLE DESTINATION ".")
else()
    install(TARGETS ${EXECUTABLE_NAME}
        RUNTIME DESTINATION bin
    )
endif()
