# SpaceCello Module - Digital Yaybahar
# Coupled physical model: String -> Spring -> Tube

add_library(SpaceCello_Module STATIC SpaceCello.cpp)
target_link_libraries(SpaceCello_Module PRIVATE RackSDK)
if(TARGET CommonLib)
    target_link_libraries(SpaceCello_Module PRIVATE CommonLib)
endif()

# Faust DSP compilation
add_faust_dsp(
    TARGET SpaceCello_Module
    DSP_FILE space_cello.dsp
)
