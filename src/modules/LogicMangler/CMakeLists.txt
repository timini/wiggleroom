# LogicMangler - Truth table logic processor
# Pure C++ module (no Faust DSP)

add_library(LogicMangler_Module STATIC
    LogicMangler.cpp
)

target_link_libraries(LogicMangler_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(LogicMangler_Module PRIVATE CommonLib)
endif()
