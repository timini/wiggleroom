# Mutagen - native C++, no Faust

add_library(Mutagen_Module STATIC Mutagen.cpp)
target_link_libraries(Mutagen_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(Mutagen_Module PRIVATE CommonLib)
endif()
