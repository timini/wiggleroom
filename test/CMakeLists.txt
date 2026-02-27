# Test harness for Faust DSP modules
cmake_minimum_required(VERSION 3.15)

# Include Faust helper from parent
include(${CMAKE_SOURCE_DIR}/cmake/Faust.cmake)

# Generate Faust headers for test (reuse from main build)
set(FAUST_GEN_DIR "${CMAKE_BINARY_DIR}/test/faust_gen")
file(MAKE_DIRECTORY ${FAUST_GEN_DIR})

# List of all Faust modules
set(FAUST_MODULES
    LadderLPF
    BigReverb
    SaturationEcho
    SpectralResonator
    ModalBell
    PluckedString
    ChaosFlute
    TriPhaseEnsemble
    InfiniteFolder
    SpaceCello
    TheAbyss
    Matter
    TheCauldron
    VektorX
    AnalogDrums
    ACID9Voice
    TetanusCoil
    NutShaker
    PhysicalChoir
    ChaosPad
    Linkage
    SpectraHenge
)

# Generate Faust headers for the test executable
foreach(MODULE ${FAUST_MODULES})
    string(TOLOWER ${MODULE} MODULE_LOWER)
    # Map module names to their actual .dsp filenames
    if(MODULE STREQUAL "LadderLPF")
        set(DSP_FILE "ladder_lpf.dsp")
        set(HPP_FILE "ladder_lpf.hpp")
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
    elseif(MODULE STREQUAL "TriPhaseEnsemble")
        set(DSP_FILE "tri_phase_ensemble.dsp")
        set(HPP_FILE "tri_phase_ensemble.hpp")
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
    elseif(MODULE STREQUAL "TheCauldron")
        set(DSP_FILE "the_cauldron.dsp")
        set(HPP_FILE "the_cauldron.hpp")
    elseif(MODULE STREQUAL "VektorX")
        set(DSP_FILE "vektorx.dsp")
        set(HPP_FILE "vektorx.hpp")
    elseif(MODULE STREQUAL "AnalogDrums")
        set(DSP_FILE "analog_drums.dsp")
        set(HPP_FILE "analog_drums.hpp")
    elseif(MODULE STREQUAL "ACID9Voice")
        set(DSP_FILE "acid9voice.dsp")
        set(HPP_FILE "acid9voice.hpp")
    elseif(MODULE STREQUAL "TetanusCoil")
        set(DSP_FILE "tetanus_coil.dsp")
        set(HPP_FILE "tetanus_coil.hpp")
    elseif(MODULE STREQUAL "NutShaker")
        set(DSP_FILE "nutshaker.dsp")
        set(HPP_FILE "nutshaker.hpp")
    elseif(MODULE STREQUAL "PhysicalChoir")
        set(DSP_FILE "physical_choir.dsp")
        set(HPP_FILE "physical_choir.hpp")
    elseif(MODULE STREQUAL "ChaosPad")
        set(DSP_FILE "chaos_pad.dsp")
        set(HPP_FILE "chaos_pad.hpp")
    elseif(MODULE STREQUAL "Linkage")
        set(DSP_FILE "linkage.dsp")
        set(HPP_FILE "linkage.hpp")
    elseif(MODULE STREQUAL "SpectraHenge")
        set(DSP_FILE "spectra_henge.dsp")
        set(HPP_FILE "spectra_henge.hpp")
    endif()

    set(DSP_PATH "${CMAKE_SOURCE_DIR}/src/modules/${MODULE}/${DSP_FILE}")
    set(HPP_PATH "${FAUST_GEN_DIR}/${HPP_FILE}")

    if(EXISTS ${DSP_PATH})
        # Build Faust command arguments
        set(FAUST_CMD_ARGS -i -a ${CMAKE_SOURCE_DIR}/faust/vcvrack.cpp)

        # Add library path for modules that need it (e.g., VektorX, ACID9Voice, ChaosPad)
        if(MODULE STREQUAL "VektorX" OR MODULE STREQUAL "ACID9Voice")
            list(APPEND FAUST_CMD_ARGS -I ${CMAKE_SOURCE_DIR}/src/modules/${MODULE}/lib)
        endif()
        if(MODULE STREQUAL "ChaosPad")
            list(APPEND FAUST_CMD_ARGS -I ${CMAKE_SOURCE_DIR}/src/modules/${MODULE})
        endif()

        list(APPEND FAUST_CMD_ARGS ${DSP_PATH} -o ${HPP_PATH})

        add_custom_command(
            OUTPUT ${HPP_PATH}
            COMMAND ${FAUST_EXECUTABLE} ${FAUST_CMD_ARGS}
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

# OctoLFO test executable (standalone C++ test, no Faust dependencies)
add_executable(octolfo_test
    octolfo_test.cpp
)

target_include_directories(octolfo_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
)

set_target_properties(octolfo_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Euclogic test executable (standalone C++ test, no Faust/VCV dependencies)
add_executable(euclogic_test
    euclogic_test.cpp
)

target_include_directories(euclogic_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src  # For modules/Euclogic/*.hpp
    ${CMAKE_SOURCE_DIR}/src/common
)

set_target_properties(euclogic_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
