# TheCauldron - Fluid Wave Math Processor with Faust DSP
#
# This module requires the Faust compiler to generate DSP code.
# Install via: brew install faust

add_library(TheCauldron_Module STATIC TheCauldron.cpp)
target_link_libraries(TheCauldron_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(TheCauldron_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP (or use pre-generated if Faust not available)
add_faust_dsp(
    TARGET TheCauldron_Module
    DSP_FILE the_cauldron.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(TheCauldron_Module_FAUST_MISSING)
    message(WARNING "Excluding TheCauldron module - Faust not available")
    set_target_properties(TheCauldron_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
