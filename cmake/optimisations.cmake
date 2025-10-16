set(ARM64_DETECTED FALSE)
set(NEON_AVAILABLE FALSE)
set(X86_64_DETECTED FALSE)
set(SSE_AVAILABLE FALSE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    set(ARM64_DETECTED TRUE)
    message(STATUS "ARM64 architecture detected: ${CMAKE_SYSTEM_PROCESSOR}")

    if(ENABLE_NEON_OPTIMISATIONS)
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-march=armv8-a" COMPILER_SUPPORTS_ARMV8)

        if(COMPILER_SUPPORTS_ARMV8)
            set(NEON_AVAILABLE TRUE)
            message(STATUS "ARM NEON optimisations enabled")
            add_definitions(-DUSE_NEON_OPTIMISATIONS)
        else()
            message(WARNING "Compiler does not support ARM NEON extensions")
        endif()
    else()
        message(STATUS "ARM NEON optimisations disabled by user")
    endif()
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(ARM64_DETECTED TRUE)
    set(NEON_AVAILABLE TRUE)
    message(STATUS "Apple Silicon (ARM64) detected via CMAKE_OSX_ARCHITECTURES")
    if(ENABLE_NEON_OPTIMISATIONS)
        message(STATUS "ARM NEON optimisations enabled for Apple Silicon")
        add_definitions(-DUSE_NEON_OPTIMISATIONS)
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|x64")
    set(X86_64_DETECTED TRUE)
    message(STATUS "x86_64 architecture detected: ${CMAKE_SYSTEM_PROCESSOR}")
    set(SSE_AVAILABLE TRUE)
    message(STATUS "SSE/AVX optimisations enabled for x86_64")
    add_definitions(-DUSE_SSE_OPTIMISATIONS)
endif()

function(add_neon_sources)
    if(NEON_AVAILABLE AND ENABLE_NEON_OPTIMISATIONS)
        list(APPEND SOURCES
            ${SRC_DIR}/audio/analysis/fft/neon/fft_processor_neon.cpp
            ${SRC_DIR}/colour/neon/colour_mapper_neon.cpp
            ${SRC_DIR}/audio/analysis/spectral/neon/spectral_processor_neon.cpp
        )
        set(SOURCES ${SOURCES} PARENT_SCOPE)
        message(STATUS "Added NEON-optimised source files to build")
    endif()
endfunction()

function(add_sse_sources)
    if(SSE_AVAILABLE)
        list(APPEND SOURCES
            ${SRC_DIR}/audio/analysis/fft/sse/fft_processor_sse.cpp
            ${SRC_DIR}/colour/sse/colour_mapper_sse.cpp
            ${SRC_DIR}/audio/analysis/spectral/sse/spectral_processor_sse.cpp
        )
        set(SOURCES ${SOURCES} PARENT_SCOPE)
        message(STATUS "Added SSE/AVX-optimised source files to build")
    endif()
endfunction()

function(apply_neon_optimisations)
    if(NEON_AVAILABLE AND ENABLE_NEON_OPTIMISATIONS)
        if(APPLE)
            set(NEON_CPU_FLAGS "-mcpu=native -mtune=native")
            message(STATUS "Applied NEON optimisations for Apple Silicon (native)")
        else()
            set(NEON_CPU_FLAGS "-march=armv8-a+simd -mtune=cortex-a72")
            message(STATUS "Applied NEON optimisations for generic ARM64")
        endif()

        set_source_files_properties(
            ${SRC_DIR}/audio/analysis/fft/neon/fft_processor_neon.cpp
            ${SRC_DIR}/colour/neon/colour_mapper_neon.cpp
            ${SRC_DIR}/audio/analysis/spectral/neon/spectral_processor_neon.cpp
            PROPERTIES COMPILE_FLAGS "-O3 -ffast-math ${NEON_CPU_FLAGS}"
        )

        message(STATUS "Applied NEON-specific compiler optimisations")
    endif()
endfunction()

function(apply_sse_optimisations)
    if(SSE_AVAILABLE)
        if(MSVC)
            target_compile_options(${EXECUTABLE_NAME} PRIVATE /arch:AVX2)
            set_source_files_properties(
                ${SRC_DIR}/audio/analysis/fft/sse/fft_processor_sse.cpp
                ${SRC_DIR}/colour/sse/colour_mapper_sse.cpp
                ${SRC_DIR}/audio/analysis/spectral/sse/spectral_processor_sse.cpp
                PROPERTIES COMPILE_FLAGS "/O2 /fp:fast /arch:AVX2"
            )
        else()
            target_compile_options(${EXECUTABLE_NAME} PRIVATE -msse4.2 -mavx2)
            set_source_files_properties(
                ${SRC_DIR}/audio/analysis/fft/sse/fft_processor_sse.cpp
                ${SRC_DIR}/colour/sse/colour_mapper_sse.cpp
                ${SRC_DIR}/audio/analysis/spectral/sse/spectral_processor_sse.cpp
                PROPERTIES COMPILE_FLAGS "-O3 -ffast-math -msse4.2 -mavx2"
            )
        endif()

        message(STATUS "Applied SSE/AVX-specific compiler optimisations")
    endif()
endfunction()
