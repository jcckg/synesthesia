include_guard(GLOBAL)
option(SYNESTHESIA_FORCE_KISS_FFT "Use the portable FFT backend for verification" OFF)

function(synesthesia_configure_fft target)
    if(MSVC)
        set_property(SOURCE "${SYNESTHESIA_ROOT}/vendor/kissfft/kiss_fft.c" "${SYNESTHESIA_ROOT}/vendor/kissfft/kiss_fftr.c"
            APPEND PROPERTY COMPILE_OPTIONS $<$<NOT:$<CONFIG:Debug>>:/O2> /fp:precise)
    else()
        set_property(SOURCE "${SYNESTHESIA_ROOT}/vendor/kissfft/kiss_fft.c" "${SYNESTHESIA_ROOT}/vendor/kissfft/kiss_fftr.c"
            APPEND PROPERTY COMPILE_OPTIONS -O3 -fno-fast-math)
    endif()
    target_sources(${target} PRIVATE "${SYNESTHESIA_ROOT}/src/audio/analysis/fft/real_fft.cpp"
        "${SYNESTHESIA_ROOT}/src/audio/analysis/fft/bass_analyser.cpp")
    if(APPLE)
        if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64" OR CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
            message(FATAL_ERROR "Native macOS builds require Apple Silicon (arm64).")
        endif()
        find_library(SYNESTHESIA_ACCELERATE_FRAMEWORK Accelerate REQUIRED)
        target_link_libraries(${target} PRIVATE ${SYNESTHESIA_ACCELERATE_FRAMEWORK})
    endif()
    if(SYNESTHESIA_FORCE_KISS_FFT)
        target_compile_definitions(${target} PRIVATE SYNESTHESIA_FORCE_KISS_FFT)
    endif()
endfunction()
