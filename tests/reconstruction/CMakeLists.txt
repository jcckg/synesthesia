cmake_minimum_required(VERSION 3.20)
project(ReconstructionTests LANGUAGES C CXX)

enable_testing()
get_filename_component(SYNESTHESIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(reconstruction_tests
    reconstruction_tests.cpp
    ${SYNESTHESIA_ROOT}/src/resyne/encoding/audio/wav_encoder.cpp
    ${SYNESTHESIA_ROOT}/src/resyne/encoding/reconstruction/spectral_resampling.cpp
    ${SYNESTHESIA_ROOT}/vendor/kissfft/kiss_fft.c
    ${SYNESTHESIA_ROOT}/vendor/kissfft/kiss_fftr.c
)
target_compile_features(reconstruction_tests PRIVATE cxx_std_20)
target_include_directories(reconstruction_tests PRIVATE
    ${SYNESTHESIA_ROOT}/src
    ${SYNESTHESIA_ROOT}/vendor/kissfft
)

foreach(scenario unchanged green-edits local stereo silence short invalid speed-up-quality out-of-band)
    add_test(NAME ${scenario} COMMAND reconstruction_tests ${scenario})
endforeach()
