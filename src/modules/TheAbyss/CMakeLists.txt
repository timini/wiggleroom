# TheAbyss Module - Waterphone
# Bowed metal rods with hydro-modulation

add_library(TheAbyss_Module STATIC TheAbyss.cpp)
target_link_libraries(TheAbyss_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(TheAbyss_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation
add_faust_dsp(
    TARGET TheAbyss_Module
    DSP_FILE the_abyss.dsp
)
