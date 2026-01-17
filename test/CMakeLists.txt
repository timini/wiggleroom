# Test harness for Faust DSP modules
cmake_minimum_required(VERSION 3.15)

# Include Faust helper from parent
include(${CMAKE_SOURCE_DIR}/cmake/Faust.cmake)

# Generate Faust headers for test (reuse from main build)
set(FAUST_GEN_DIR "${CMAKE_BINARY_DIR}/test/faust_gen")
file(MAKE_DIRECTORY ${FAUST_GEN_DIR})

# List of all Faust modules
set(FAUST_MODULES
    MoogLPF
    BigReverb
    SaturationEcho
    SpectralResonator
    ModalBell
    PluckedString
    ChaosFlute
    SolinaEnsemble
    InfiniteFolder
    SpaceCello
    TheAbyss
    Matter
    Linkage
)

# Generate Faust headers for the test executable
foreach(MODULE ${FAUST_MODULES})
    string(TOLOWER ${MODULE} MODULE_LOWER)
    # Map module names to their actual .dsp filenames
    if(MODULE STREQUAL "MoogLPF")
        set(DSP_FILE "moog_lpf.dsp")
        set(HPP_FILE "moog_lpf.hpp")
    elseif(MODULE STREQUAL "BigReverb")
        set(DSP_FILE "big_reverb.dsp")
        set(HPP_FILE "big_reverb.hpp")
    elseif(MODULE STREQUAL "SaturationEcho")
        set(DSP_FILE "saturation_echo.dsp")
        set(HPP_FILE "saturation_echo.hpp")
    elseif(MODULE STREQUAL "SpectralResonator")
        set(DSP_FILE "spectral_resonator.dsp")
        set(HPP_FILE "spectral_resonator.hpp")
    elseif(MODULE STREQUAL "ModalBell")
        set(DSP_FILE "modal_bell.dsp")
        set(HPP_FILE "modal_bell.hpp")
    elseif(MODULE STREQUAL "PluckedString")
        set(DSP_FILE "plucked_string.dsp")
        set(HPP_FILE "plucked_string.hpp")
    elseif(MODULE STREQUAL "ChaosFlute")
        set(DSP_FILE "chaos_flute.dsp")
        set(HPP_FILE "chaos_flute.hpp")
    elseif(MODULE STREQUAL "SolinaEnsemble")
        set(DSP_FILE "solina_ensemble.dsp")
        set(HPP_FILE "solina_ensemble.hpp")
    elseif(MODULE STREQUAL "InfiniteFolder")
        set(DSP_FILE "infinite_folder.dsp")
        set(HPP_FILE "infinite_folder.hpp")
    elseif(MODULE STREQUAL "SpaceCello")
        set(DSP_FILE "space_cello.dsp")
        set(HPP_FILE "space_cello.hpp")
    elseif(MODULE STREQUAL "TheAbyss")
        set(DSP_FILE "the_abyss.dsp")
        set(HPP_FILE "the_abyss.hpp")
    elseif(MODULE STREQUAL "Matter")
        set(DSP_FILE "matter.dsp")
        set(HPP_FILE "matter.hpp")
    elseif(MODULE STREQUAL "Linkage")
        set(DSP_FILE "linkage.dsp")
        set(HPP_FILE "linkage.hpp")
    endif()

    set(DSP_PATH "${CMAKE_SOURCE_DIR}/src/modules/${MODULE}/${DSP_FILE}")
    set(HPP_PATH "${FAUST_GEN_DIR}/${HPP_FILE}")

    if(EXISTS ${DSP_PATH})
        add_custom_command(
            OUTPUT ${HPP_PATH}
            COMMAND ${FAUST_EXECUTABLE} -i -a ${CMAKE_SOURCE_DIR}/faust/vcvrack.cpp
                    ${DSP_PATH} -o ${HPP_PATH}
            DEPENDS ${DSP_PATH} ${CMAKE_SOURCE_DIR}/faust/vcvrack.cpp
            COMMENT "Generating ${HPP_FILE} for test"
        )
        list(APPEND FAUST_GENERATED_HEADERS ${HPP_PATH})
    endif()
endforeach()

# Create test executable
add_executable(faust_render
    faust_render.cpp
    dsp_wrappers.cpp
    ${FAUST_GENERATED_HEADERS}
)

target_include_directories(faust_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}  # For AbstractDSP.hpp
    ${FAUST_GEN_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_compile_definitions(faust_render PRIVATE
    FAUST_GEN_DIR="${FAUST_GEN_DIR}"
)

# C++17 standard
set_target_properties(faust_render PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
