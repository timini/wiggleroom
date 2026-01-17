# TheArchitect - Poly-Quantizer & Chord Machine
# Pure C++ module (no Faust)

add_library(TheArchitect_Module STATIC TheArchitect.cpp)
target_link_libraries(TheArchitect_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(TheArchitect_Module PRIVATE CommonLib)
endif()
