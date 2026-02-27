add_library(PreFlightClock_Module STATIC PreFlightClock.cpp)
target_link_libraries(PreFlightClock_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(PreFlightClock_Module PRIVATE CommonLib)
endif()
