add_library(ACID9Voice_Module STATIC ACID9Voice.cpp)
target_link_libraries(ACID9Voice_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(ACID9Voice_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation - include lib directory for Faust imports
add_faust_dsp(
    TARGET ACID9Voice_Module
    DSP_FILE acid9voice.dsp
    LIBRARY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib
)
