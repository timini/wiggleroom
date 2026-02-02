# VektorX - Complex Drone Synthesizer
# Modular Faust implementation with independent library components
# Inspired by Forge TME VHIKK X

add_library(VektorX_Module STATIC VektorX.cpp)
target_link_libraries(VektorX_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(VektorX_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP with library path for component libraries
add_faust_dsp(
    TARGET VektorX_Module
    DSP_FILE vektorx.dsp
    LIBRARY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

# If Faust is missing and no pre-generated file, exclude this module
if(VektorX_Module_FAUST_MISSING)
    message(WARNING "Excluding VektorX module - Faust not available")
    set_target_properties(VektorX_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
