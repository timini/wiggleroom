# EucSeq - 4-channel Euclidean sequencer with CV steps
# Pure C++ module (no Faust DSP)

add_library(EucSeq_Module STATIC
    EucSeq.cpp
)

target_link_libraries(EucSeq_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(EucSeq_Module PRIVATE CommonLib)
endif()
