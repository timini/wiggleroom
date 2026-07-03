# PixelProbe - image-to-CV color sampler (native C++, no Faust)

add_library(PixelProbe_Module STATIC PixelProbe.cpp)
target_link_libraries(PixelProbe_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(PixelProbe_Module PRIVATE CommonLib)
endif()
