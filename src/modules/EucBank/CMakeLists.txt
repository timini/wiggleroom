# EucBank - Pattern storage/recall for EucSeq + LogicMangler
# Pure C++ module (no Faust DSP)

add_library(EucBank_Module STATIC
    EucBank.cpp
)

target_link_libraries(EucBank_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(EucBank_Module PRIVATE CommonLib)
endif()
