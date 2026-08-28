# MutagenX - native C++, no Faust

add_library(MutagenX_Module STATIC MutagenX.cpp)
target_link_libraries(MutagenX_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(MutagenX_Module PRIVATE CommonLib)
endif()
