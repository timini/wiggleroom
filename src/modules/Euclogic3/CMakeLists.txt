# Euclogic3 - 3-channel Euclidean sequencer with truth table logic
# Pure C++ module (no Faust DSP)

add_library(Euclogic3_Module STATIC
    Euclogic3.cpp
)

target_link_libraries(Euclogic3_Module PRIVATE RackSDK)

target_include_directories(Euclogic3_Module PRIVATE
    ${CMAKE_SOURCE_DIR}/src/modules/Euclogic  # For EuclideanEngine.hpp, ProbabilityGate.hpp
)

if(TARGET CommonLib)
    target_link_libraries(Euclogic3_Module PRIVATE CommonLib)
endif()
