# RackSDK.cmake - Find or download the VCV Rack SDK
#
# This module defines:
#   RACK_SDK_DIR - Path to the Rack SDK
#   RackSDK - Imported target for linking

set(RACK_SDK_VERSION "2.5.2" CACHE STRING "VCV Rack SDK version")

# Platform-specific SDK URL
if(APPLE)
    set(RACK_SDK_URL "https://vcvrack.com/downloads/Rack-SDK-${RACK_SDK_VERSION}-mac-x64+arm64.zip")
elseif(WIN32)
    set(RACK_SDK_URL "https://vcvrack.com/downloads/Rack-SDK-${RACK_SDK_VERSION}-win-x64.zip")
else()
    set(RACK_SDK_URL "https://vcvrack.com/downloads/Rack-SDK-${RACK_SDK_VERSION}-lin-x64.zip")
endif()

# Check for RACK_DIR environment variable or CMake variable
if(DEFINED ENV{RACK_DIR})
    set(RACK_SDK_DIR "$ENV{RACK_DIR}" CACHE PATH "Path to VCV Rack SDK")
elseif(DEFINED RACK_DIR)
    set(RACK_SDK_DIR "${RACK_DIR}" CACHE PATH "Path to VCV Rack SDK")
else()
    # Auto-download SDK if not specified
    set(RACK_SDK_DIR "${CMAKE_BINARY_DIR}/Rack-SDK" CACHE PATH "Path to VCV Rack SDK")

    if(NOT EXISTS "${RACK_SDK_DIR}/include/rack.hpp")
        message(STATUS "Downloading VCV Rack SDK ${RACK_SDK_VERSION}...")

        set(SDK_ZIP "${CMAKE_BINARY_DIR}/Rack-SDK.zip")

        if(NOT EXISTS "${SDK_ZIP}")
            file(DOWNLOAD "${RACK_SDK_URL}" "${SDK_ZIP}"
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS)
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                message(FATAL_ERROR "Failed to download Rack SDK")
            endif()
        endif()

        message(STATUS "Extracting Rack SDK...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${SDK_ZIP}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract Rack SDK")
        endif()
    endif()
endif()

# Verify SDK exists
if(NOT EXISTS "${RACK_SDK_DIR}/include/rack.hpp")
    message(FATAL_ERROR "VCV Rack SDK not found at ${RACK_SDK_DIR}")
endif()

message(STATUS "Using VCV Rack SDK at: ${RACK_SDK_DIR}")

# Create imported target for the Rack SDK
add_library(RackSDK INTERFACE)
target_include_directories(RackSDK INTERFACE
    "${RACK_SDK_DIR}/include"
    "${RACK_SDK_DIR}/dep/include"
)

# Platform-specific configuration
if(APPLE)
    target_compile_definitions(RackSDK INTERFACE ARCH_MAC)
    target_link_directories(RackSDK INTERFACE "${RACK_SDK_DIR}/dep/lib")
elseif(WIN32 OR MINGW)
    target_compile_definitions(RackSDK INTERFACE ARCH_WIN)
    target_link_directories(RackSDK INTERFACE "${RACK_SDK_DIR}/dep/lib")
    # Windows requires explicit linking against the Rack import library
    # Check multiple possible locations for the import library
    file(GLOB RACK_IMPORT_LIBS "${RACK_SDK_DIR}/*.a" "${RACK_SDK_DIR}/*.lib")
    message(STATUS "Windows Rack SDK import libraries found: ${RACK_IMPORT_LIBS}")
    if(EXISTS "${RACK_SDK_DIR}/Rack.dll.a")
        message(STATUS "Linking Rack.dll.a import library")
        target_link_libraries(RackSDK INTERFACE "${RACK_SDK_DIR}/Rack.dll.a")
    elseif(EXISTS "${RACK_SDK_DIR}/libRack.a")
        message(STATUS "Linking libRack.a import library")
        target_link_libraries(RackSDK INTERFACE "${RACK_SDK_DIR}/libRack.a")
    else()
        message(WARNING "No Rack import library found in ${RACK_SDK_DIR} - Windows linking will fail!")
    endif()
else()
    target_compile_definitions(RackSDK INTERFACE ARCH_LIN)
    target_link_directories(RackSDK INTERFACE "${RACK_SDK_DIR}/dep/lib")
endif()

# Common compile definitions
target_compile_definitions(RackSDK INTERFACE
    _USE_MATH_DEFINES
)

# Helper function for packaging
function(rack_package_target TARGET_NAME)
    set(DIST_DIR "${CMAKE_BINARY_DIR}/dist/${TARGET_NAME}")

    add_custom_target(dist
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${TARGET_NAME}>" "${DIST_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/plugin.json" "${DIST_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res" "${DIST_DIR}/res"
        DEPENDS ${TARGET_NAME}
        COMMENT "Packaging plugin to ${DIST_DIR}"
    )
endfunction()
