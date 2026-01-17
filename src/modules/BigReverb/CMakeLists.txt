# BigReverb - Studio Quality Stereo Reverb with Faust DSP

add_library(BigReverb_Module STATIC BigReverb.cpp)
target_link_libraries(BigReverb_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(BigReverb_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP
add_faust_dsp(
    TARGET BigReverb_Module
    DSP_FILE big_reverb.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(BigReverb_Module_FAUST_MISSING)
    message(WARNING "Excluding BigReverb module - Faust not available")
    set_target_properties(BigReverb_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
