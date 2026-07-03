# TheWeaver - Scale-Bus-aware arpeggiator (native C++, no Faust)

add_library(TheWeaver_Module STATIC TheWeaver.cpp)
target_link_libraries(TheWeaver_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(TheWeaver_Module PRIVATE CommonLib)
endif()
