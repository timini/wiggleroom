add_library(ChaosFlute_Module STATIC ChaosFlute.cpp)
target_link_libraries(ChaosFlute_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(ChaosFlute_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation
add_faust_dsp(
    TARGET ChaosFlute_Module
    DSP_FILE chaos_flute.dsp
)
