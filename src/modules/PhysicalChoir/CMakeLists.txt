add_library(PhysicalChoir_Module STATIC PhysicalChoir.cpp)
target_link_libraries(PhysicalChoir_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(PhysicalChoir_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation
add_faust_dsp(
    TARGET PhysicalChoir_Module
    DSP_FILE physical_choir.dsp
)
