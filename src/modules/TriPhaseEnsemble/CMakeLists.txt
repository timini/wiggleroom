add_library(TriPhaseEnsemble_Module STATIC TriPhaseEnsemble.cpp)
target_link_libraries(TriPhaseEnsemble_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(TriPhaseEnsemble_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation
add_faust_dsp(
    TARGET TriPhaseEnsemble_Module
    DSP_FILE tri_phase_ensemble.dsp
)
