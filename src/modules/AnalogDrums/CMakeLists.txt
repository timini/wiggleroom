add_library(AnalogDrums_Module STATIC AnalogDrums.cpp)
target_link_libraries(AnalogDrums_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(AnalogDrums_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation
add_faust_dsp(
    TARGET AnalogDrums_Module
    DSP_FILE analog_drums.dsp
)
