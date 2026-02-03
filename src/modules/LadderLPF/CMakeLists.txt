# LadderLPF - Ladder Lowpass Filter with Faust DSP
#
# This module requires the Faust compiler to generate DSP code.
# Install via: brew install faust

add_library(LadderLPF_Module STATIC LadderLPF.cpp)
target_link_libraries(LadderLPF_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(LadderLPF_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP (or use pre-generated if Faust not available)
add_faust_dsp(
    TARGET LadderLPF_Module
    DSP_FILE ladder_lpf.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(LadderLPF_Module_FAUST_MISSING)
    message(WARNING "Excluding LadderLPF module - Faust not available")
    set_target_properties(LadderLPF_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
