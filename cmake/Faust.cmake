# Faust.cmake - Faust DSP integration for VCV Rack plugins
#
# This module provides:
#   FAUST_EXECUTABLE - Path to faust compiler (if found)
#   FAUST_FOUND - TRUE if faust is available
#   add_faust_dsp() - Function to generate C++ from .dsp files
#
# Usage in module CMakeLists.txt:
#   add_faust_dsp(
#       TARGET MyModule_Module
#       DSP_FILE myfilter.dsp
#       OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/faust_gen
#   )

# Find the faust executable
find_program(FAUST_EXECUTABLE
    NAMES faust
    DOC "Faust DSP compiler"
)

if(FAUST_EXECUTABLE)
    set(FAUST_FOUND TRUE)
    # Get version
    execute_process(
        COMMAND ${FAUST_EXECUTABLE} --version
        OUTPUT_VARIABLE FAUST_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    message(STATUS "Found Faust: ${FAUST_EXECUTABLE}")
    message(STATUS "  ${FAUST_VERSION_OUTPUT}")
else()
    set(FAUST_FOUND FALSE)
    message(WARNING "Faust compiler not found. Install via: brew install faust")
    message(WARNING "Faust modules will use pre-generated headers if available.")
endif()

# Set the architecture file path
set(FAUST_ARCHITECTURE_FILE "${CMAKE_SOURCE_DIR}/faust/vcvrack.cpp"
    CACHE FILEPATH "Faust architecture file for VCV Rack")

# Function to add Faust DSP sources to a target
#
# add_faust_dsp(
#     TARGET <target_name>          # Required: CMake target to add sources to
#     DSP_FILE <file.dsp>           # Required: Faust DSP source file
#     [OUTPUT_DIR <dir>]            # Optional: Output directory (default: CMAKE_CURRENT_BINARY_DIR/faust_gen)
#     [CLASS_NAME <name>]           # Optional: Generated class name (default: filename without extension)
# )
function(add_faust_dsp)
    cmake_parse_arguments(
        FAUST
        ""
        "TARGET;DSP_FILE;OUTPUT_DIR;CLASS_NAME"
        ""
        ${ARGN}
    )

    # Validate required arguments
    if(NOT FAUST_TARGET)
        message(FATAL_ERROR "add_faust_dsp: TARGET argument is required")
    endif()

    if(NOT FAUST_DSP_FILE)
        message(FATAL_ERROR "add_faust_dsp: DSP_FILE argument is required")
    endif()

    # Get absolute path to DSP file
    get_filename_component(DSP_FILE_ABS "${FAUST_DSP_FILE}" ABSOLUTE)
    get_filename_component(DSP_NAME "${FAUST_DSP_FILE}" NAME_WE)

    # Default output directory
    if(NOT FAUST_OUTPUT_DIR)
        set(FAUST_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/faust_gen")
    endif()

    # Default class name from filename
    if(NOT FAUST_CLASS_NAME)
        set(FAUST_CLASS_NAME "${DSP_NAME}")
    endif()

    # Output file path
    set(OUTPUT_HPP "${FAUST_OUTPUT_DIR}/${DSP_NAME}.hpp")

    # Create output directory
    file(MAKE_DIRECTORY "${FAUST_OUTPUT_DIR}")

    if(FAUST_FOUND)
        # Add custom command to generate C++ from Faust DSP
        # Note: We don't use -cn to let Faust use the default 'mydsp' class name
        # which our VCVRackDSP wrapper expects
        add_custom_command(
            OUTPUT "${OUTPUT_HPP}"
            COMMAND ${FAUST_EXECUTABLE}
                -i                                      # Inline all code
                -a "${FAUST_ARCHITECTURE_FILE}"         # Use VCV Rack architecture
                -o "${OUTPUT_HPP}"                      # Output file
                "${DSP_FILE_ABS}"                       # Input DSP file
            DEPENDS "${DSP_FILE_ABS}" "${FAUST_ARCHITECTURE_FILE}"
            COMMENT "Faust: Compiling ${DSP_NAME}.dsp -> ${DSP_NAME}.hpp"
            VERBATIM
        )

        message(STATUS "Faust DSP: ${DSP_NAME}.dsp -> ${OUTPUT_HPP}")

        # Add generated header as source (triggers generation)
        target_sources(${FAUST_TARGET} PRIVATE "${OUTPUT_HPP}")

        # Add output directory to include path
        target_include_directories(${FAUST_TARGET} PRIVATE "${FAUST_OUTPUT_DIR}")
    else()
        # Check if pre-generated file exists in source tree
        set(SRC_GENERATED_HPP "${CMAKE_CURRENT_SOURCE_DIR}/${DSP_NAME}.hpp")
        if(EXISTS "${SRC_GENERATED_HPP}")
            message(STATUS "Using pre-generated Faust DSP: ${SRC_GENERATED_HPP}")
            # Just add include path - the pre-generated file is in source tree
        else()
            message(WARNING "")
            message(WARNING "Faust compiler not found and no pre-generated file!")
            message(WARNING "To build MoogLPF, install Faust: brew install faust")
            message(WARNING "Then re-run cmake to generate the DSP code.")
            message(WARNING "")
            # Set a flag so the module can be conditionally excluded
            set(${FAUST_TARGET}_FAUST_MISSING TRUE PARENT_SCOPE)
        endif()
    endif()

endfunction()
