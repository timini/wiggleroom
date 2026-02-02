# Euclogic2 - 2-channel Euclidean sequencer with truth table logic (compact version)
# Pure C++ module (no Faust DSP)

add_library(Euclogic2_Module STATIC
    Euclogic2.cpp
)

target_link_libraries(Euclogic2_Module PRIVATE RackSDK)

target_include_directories(Euclogic2_Module PRIVATE
    ${CMAKE_SOURCE_DIR}/src/modules/Euclogic  # For EuclideanEngine.hpp
)

if(TARGET CommonLib)
    target_link_libraries(Euclogic2_Module PRIVATE CommonLib)
endif()
