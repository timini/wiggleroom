add_library(SolinaEnsemble_Module STATIC SolinaEnsemble.cpp)
target_link_libraries(SolinaEnsemble_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(SolinaEnsemble_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation
add_faust_dsp(
    TARGET SolinaEnsemble_Module
    DSP_FILE solina_ensemble.dsp
)
