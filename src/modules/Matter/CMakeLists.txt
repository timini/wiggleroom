# Matter - Physical Model (Struck Solid + Tube) with Faust DSP

add_library(Matter_Module STATIC Matter.cpp)
target_link_libraries(Matter_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(Matter_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP
add_faust_dsp(
    TARGET Matter_Module
    DSP_FILE matter.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(Matter_Module_FAUST_MISSING)
    message(WARNING "Excluding Matter module - Faust not available")
    set_target_properties(Matter_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
