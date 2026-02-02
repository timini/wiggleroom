# ACID9Seq - Polyrhythmic generative acid sequencer
# Pure C++ module (no Faust DSP)

add_library(ACID9Seq_Module STATIC
    ACID9Seq.cpp
)

target_link_libraries(ACID9Seq_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(ACID9Seq_Module PRIVATE CommonLib)
endif()
