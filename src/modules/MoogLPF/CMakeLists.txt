# MoogLPF - Moog Ladder Lowpass Filter with Faust DSP
#
# This module requires the Faust compiler to generate DSP code.
# Install via: brew install faust

add_library(MoogLPF_Module STATIC MoogLPF.cpp)
target_link_libraries(MoogLPF_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(MoogLPF_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP (or use pre-generated if Faust not available)
add_faust_dsp(
    TARGET MoogLPF_Module
    DSP_FILE moog_lpf.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(MoogLPF_Module_FAUST_MISSING)
    message(WARNING "Excluding MoogLPF module - Faust not available")
    set_target_properties(MoogLPF_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
