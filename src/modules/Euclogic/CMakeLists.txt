# Euclogic - 4-channel Euclidean drum sequencer with truth table logic
# Pure C++ module (no Faust DSP)

add_library(Euclogic_Module STATIC
    Euclogic.cpp
)

target_link_libraries(Euclogic_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(Euclogic_Module PRIVATE CommonLib)
endif()
