# EucMix - 4x4 CV mix matrix
# Pure C++ module (no Faust DSP)

add_library(EucMix_Module STATIC
    EucMix.cpp
)

target_link_libraries(EucMix_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(EucMix_Module PRIVATE CommonLib)
endif()
