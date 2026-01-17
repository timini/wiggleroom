cmake_minimum_required(VERSION 3.15)
project(WiggleRoom VERSION 2.0.0)

# 1. Setup VCV Rack SDK and Faust
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RackSDK)
include(Faust)

# 2. Global Configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compiler-specific flags (avoid GCC-only flags on Clang)
# Note: -fPIC is handled by CMAKE_POSITION_INDEPENDENT_CODE above
add_compile_options($<$<CXX_COMPILER_ID:GNU>:-Wno-unused-local-typedefs>)

# Optimization flags for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -ffast-math)
endif()

# 3. Shared Library (Common DSP)
file(GLOB_RECURSE COMMON_SRC "src/common/*.cpp")
if(COMMON_SRC)
    add_library(CommonLib STATIC ${COMMON_SRC})
    target_include_directories(CommonLib PUBLIC src/common)
    target_link_libraries(CommonLib PUBLIC RackSDK)
else()
    # Header-only common library
    add_library(CommonLib INTERFACE)
    target_include_directories(CommonLib INTERFACE src/common)
endif()

# 4. Auto-Discover Modules
set(MODULE_TARGETS "")
file(GLOB children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/src/modules src/modules/*)
foreach(child ${children})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/${child})
        add_subdirectory(src/modules/${child})
        if(TARGET ${child}_Module)
            list(APPEND MODULE_TARGETS ${child}_Module)
        endif()
    endif()
endforeach()

# 5. The Plugin Artifact
add_library(${PROJECT_NAME} MODULE src/plugin.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE RackSDK)

# Link CommonLib if it has content
if(TARGET CommonLib)
    target_link_libraries(${PROJECT_NAME} PRIVATE CommonLib)
endif()

# Link discovered modules (skip modules marked for exclusion)
set(ACTIVE_MODULES "")
foreach(module_target ${MODULE_TARGETS})
    get_target_property(IS_EXCLUDED ${module_target} EXCLUDE_FROM_ALL)
    if(NOT IS_EXCLUDED)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${module_target})
        list(APPEND ACTIVE_MODULES ${module_target})
    else()
        message(STATUS "Skipping excluded module: ${module_target}")
    endif()
endforeach()

# Define HAS_* for Faust modules that are being built
if(MoogLPF_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_MOOGLPF=1)
endif()
if(ModalBell_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_MODALBELL=1)
endif()
if(BigReverb_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_BIGREVERB=1)
endif()
if(SaturationEcho_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_SATURATIONECHO=1)
endif()
if(SpectralResonator_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_SPECTRALRESONATOR=1)
endif()
if(PluckedString_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_PLUCKEDSTRING=1)
endif()
if(ChaosFlute_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_CHAOSFLUTE=1)
endif()
if(SolinaEnsemble_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_SOLINAENSEMBLE=1)
endif()
if(InfiniteFolder_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_INFINITEFOLDER=1)
endif()
if(SpaceCello_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_SPACECELLO=1)
endif()
if(TheAbyss_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_THEABYSS=1)
endif()
if(Matter_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_MATTER=1)
endif()
if(Linkage_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_LINKAGE=1)
endif()
if(TheCauldron_Module IN_LIST ACTIVE_MODULES)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_THECAULDRON=1)
endif()
# Set output name without lib prefix
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin"
)

# Platform-specific extension and linker flags
# VCV Rack plugins are loaded by the host, so undefined symbols are resolved at runtime
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".dylib"
        LINK_FLAGS "-undefined dynamic_lookup"
    )
elseif(WIN32 OR MINGW)
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".dll")
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".so"
        LINK_FLAGS "-Wl,--allow-shlib-undefined"
    )
endif()

# 6. Packaging Custom Target
rack_package_target(${PROJECT_NAME})

# 7. Test Harness (optional)
option(BUILD_TESTS "Build audio test harness" ON)
if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt")
    add_subdirectory(test)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "=== WiggleRoom Plugin Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Modules found: ${MODULE_TARGETS}")
message(STATUS "========================================")
message(STATUS "")
