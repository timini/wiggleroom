# SaturationEcho - Vintage Tape Delay with Faust DSP

add_library(SaturationEcho_Module STATIC SaturationEcho.cpp)
target_link_libraries(SaturationEcho_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(SaturationEcho_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP
add_faust_dsp(
    TARGET SaturationEcho_Module
    DSP_FILE saturation_echo.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(SaturationEcho_Module_FAUST_MISSING)
    message(WARNING "Excluding SaturationEcho module - Faust not available")
    set_target_properties(SaturationEcho_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
