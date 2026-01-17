add_library(PluckedString_Module STATIC PluckedString.cpp)
target_link_libraries(PluckedString_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(PluckedString_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation
add_faust_dsp(
    TARGET PluckedString_Module
    DSP_FILE plucked_string.dsp
)
