# NutShaker - Stochastic Percussion Synthesizer
# PhISM-based particle collision synthesis

add_library(NutShaker_Module STATIC NutShaker.cpp)
target_link_libraries(NutShaker_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(NutShaker_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP
add_faust_dsp(
    TARGET NutShaker_Module
    DSP_FILE nutshaker.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(NutShaker_Module_FAUST_MISSING)
    message(WARNING "Excluding NutShaker module - Faust not available")
    set_target_properties(NutShaker_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
