add_library(TR808_Module STATIC TR808.cpp)
target_link_libraries(TR808_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(TR808_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation
add_faust_dsp(
    TARGET TR808_Module
    DSP_FILE tr808.dsp
)
