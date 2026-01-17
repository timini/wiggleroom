# ModalBell - Physical Model Bell/Marimba with Faust DSP

add_library(ModalBell_Module STATIC ModalBell.cpp)
target_link_libraries(ModalBell_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(ModalBell_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP
add_faust_dsp(
    TARGET ModalBell_Module
    DSP_FILE modal_bell.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(ModalBell_Module_FAUST_MISSING)
    message(WARNING "Excluding ModalBell module - Faust not available")
    set_target_properties(ModalBell_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
