# Stems - beat-synced source separation, wavetable voice and granular texturiser
# (native C++, no Faust). All the DSP lives in src/common/stems, which is
# framework-free and unit-tested; this target is the Rack wrapper.

add_library(Stems_Module STATIC Stems.cpp)
target_link_libraries(Stems_Module PRIVATE RackSDK)

if(TARGET CommonLib)
    target_link_libraries(Stems_Module PRIVATE CommonLib)
endif()
