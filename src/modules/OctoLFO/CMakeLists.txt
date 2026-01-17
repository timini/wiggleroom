# OctoLFO - Clock-synced 8-channel LFO with wave shaping
# Pure C++ module (no Faust)

add_library(OctoLFO_Module STATIC OctoLFO.cpp)
target_link_libraries(OctoLFO_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(OctoLFO_Module PRIVATE CommonLib)
endif()
