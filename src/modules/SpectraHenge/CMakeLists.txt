# SpectraHenge - 4-Channel Spatial/Spectral Mixer with Faust DSP

add_library(SpectraHenge_Module STATIC SpectraHenge.cpp)
target_link_libraries(SpectraHenge_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(SpectraHenge_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP
add_faust_dsp(
    TARGET SpectraHenge_Module
    DSP_FILE spectra_henge.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(SpectraHenge_Module_FAUST_MISSING)
    message(WARNING "Excluding SpectraHenge module - Faust not available")
    set_target_properties(SpectraHenge_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
