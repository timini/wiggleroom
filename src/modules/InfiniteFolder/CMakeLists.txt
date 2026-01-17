# InfiniteFolder - West Coast Wavefolder with Faust DSP
#
# This module requires the Faust compiler to generate DSP code.
# Install via: brew install faust

add_library(InfiniteFolder_Module STATIC InfiniteFolder.cpp)
target_link_libraries(InfiniteFolder_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(InfiniteFolder_Module PRIVATE CommonLib)
endif()

# Generate C++ from Faust DSP (or use pre-generated if Faust not available)
add_faust_dsp(
    TARGET InfiniteFolder_Module
    DSP_FILE infinite_folder.dsp
)

# If Faust is missing and no pre-generated file, exclude this module
if(InfiniteFolder_Module_FAUST_MISSING)
    message(WARNING "Excluding InfiniteFolder module - Faust not available")
    set_target_properties(InfiniteFolder_Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
