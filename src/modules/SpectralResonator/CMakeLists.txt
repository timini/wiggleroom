# SpectralResonator - 6-Band Chord Resonator with Faust DSP

add_library(SpectralResonator_Module STATIC SpectralResonator.cpp)
target_link_libraries(SpectralResonator_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(SpectralResonator_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP
add_faust_dsp(
    TARGET SpectralResonator_Module
    DSP_FILE spectral_resonator.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(SpectralResonator_Module_FAUST_MISSING)
    message(WARNING "Excluding SpectralResonator module - Faust not available")
    set_target_properties(SpectralResonator_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
