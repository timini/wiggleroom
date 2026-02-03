# Linkage - Chaotic Percussion Generator (3-mass spring chain)
# Faust-based physical modeling module

add_library(Linkage_Module STATIC Linkage.cpp)
target_link_libraries(Linkage_Module PRIVATE RackSDK)

add_faust_dsp(
    TARGET Linkage_Module
    DSP_FILE linkage.dsp
)

if(TARGET CommonLib)
    target_link_libraries(Linkage_Module PRIVATE CommonLib)
endif()
