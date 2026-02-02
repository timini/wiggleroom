# ChaosPad - Kaoss Pad Multi-FX with Faust DSP

add_library(ChaosPad_Module STATIC ChaosPad.cpp)
target_link_libraries(ChaosPad_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(ChaosPad_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP with library path for fx/ sub-libraries
add_faust_dsp(
    TARGET ChaosPad_Module
    DSP_FILE chaos_pad.dsp
    LIBRARY_PATH ${CMAKE_CURRENT_SOURCE_DIR}
)

# If Faust is missing and no pre-generated file, exclude this module
if(ChaosPad_Module_FAUST_MISSING)
    message(WARNING "Excluding ChaosPad module - Faust not available")
    set_target_properties(ChaosPad_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
