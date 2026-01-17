# GravityClock - Clock-synced bouncing ball physics modulator
# Pure C++ module (no Faust)

add_library(GravityClock_Module STATIC GravityClock.cpp)
target_link_libraries(GravityClock_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(GravityClock_Module PRIVATE CommonLib)
endif()
