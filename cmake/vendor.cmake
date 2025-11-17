function(add_vendor_library)
    set(options "")
    set(oneValueArgs TARGET)
    set(multiValueArgs SOURCES INCLUDE_DIRS COMPILE_DEFINITIONS)
    cmake_parse_arguments(VENDOR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_library(${VENDOR_TARGET} STATIC ${VENDOR_SOURCES})

    if(MSVC)
        target_compile_options(${VENDOR_TARGET} PRIVATE /w)
    else()
        target_compile_options(${VENDOR_TARGET} PRIVATE -w)
    endif()

    if(VENDOR_INCLUDE_DIRS)
        target_include_directories(${VENDOR_TARGET} PRIVATE ${VENDOR_INCLUDE_DIRS})
    endif()

    if(VENDOR_COMPILE_DEFINITIONS)
        target_compile_definitions(${VENDOR_TARGET} PRIVATE ${VENDOR_COMPILE_DEFINITIONS})
    endif()

    target_compile_features(${VENDOR_TARGET} PRIVATE cxx_std_20)
endfunction()

add_vendor_library(
    TARGET vendor_imgui
    SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
    INCLUDE_DIRS
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

add_vendor_library(
    TARGET vendor_implot
    SOURCES
        ${IMPLOT_DIR}/implot.cpp
        ${IMPLOT_DIR}/implot_items.cpp
    INCLUDE_DIRS
        ${IMGUI_DIR}
        ${IMPLOT_DIR}
)

add_vendor_library(
    TARGET vendor_kissfft
    SOURCES
        ${KISSFFT_DIR}/kiss_fft.c
        ${KISSFFT_DIR}/kiss_fftr.c
    INCLUDE_DIRS
        ${KISSFFT_DIR}
)

if(NOT MSVC)
    target_compile_options(vendor_kissfft PRIVATE -fno-fast-math)
endif()

add_vendor_library(
    TARGET vendor_lodepng
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lodepng/lodepng.cpp
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lodepng
)

if(WIN32)
    add_vendor_library(
        TARGET vendor_imgui_backends
        SOURCES
            ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp
            ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        INCLUDE_DIRS
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
    )
elseif(APPLE)
    add_vendor_library(
        TARGET vendor_imgui_backends
        SOURCES
            ${IMGUI_DIR}/backends/imgui_impl_metal.mm
            ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        INCLUDE_DIRS
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
            ${GLFW_DIR}/include
    )

    set_source_files_properties(
        ${IMGUI_DIR}/backends/imgui_impl_metal.mm
        PROPERTIES COMPILE_FLAGS "-ObjC++ -fobjc-arc -fobjc-weak -w"
    )
else()
    add_vendor_library(
        TARGET vendor_imgui_backends
        SOURCES
            ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
            ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        INCLUDE_DIRS
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
            ${GLFW_DIR}/include
    )
endif()

function(add_vendor_subdirectory directory target_name)
    if(CMAKE_CXX_FLAGS)
        set(SAVED_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    endif()
    if(CMAKE_C_FLAGS)
        set(SAVED_C_FLAGS ${CMAKE_C_FLAGS})
    endif()

    if(MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /w")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /w")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
    endif()

    add_subdirectory(${directory})

    if(SAVED_CXX_FLAGS)
        set(CMAKE_CXX_FLAGS ${SAVED_CXX_FLAGS})
    else()
        unset(CMAKE_CXX_FLAGS)
    endif()
    if(SAVED_C_FLAGS)
        set(CMAKE_C_FLAGS ${SAVED_C_FLAGS})
    else()
        unset(CMAKE_C_FLAGS)
    endif()

    set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} PARENT_SCOPE)
    set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} PARENT_SCOPE)
endfunction()
