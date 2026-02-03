# TetanusCoil - Extreme Dispersive Spring Reverb with Faust DSP

add_library(TetanusCoil_Module STATIC TetanusCoil.cpp)
target_link_libraries(TetanusCoil_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(TetanusCoil_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP
add_faust_dsp(
    TARGET TetanusCoil_Module
    DSP_FILE tetanus_coil.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(TetanusCoil_Module_FAUST_MISSING)
    message(WARNING "Excluding TetanusCoil module - Faust not available")
    set_target_properties(TetanusCoil_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
