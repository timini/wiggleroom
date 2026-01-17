# Toolchain-Mac.cmake - Cross-compilation for macOS
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# OSXCross toolchain (set by Docker environment)
if(DEFINED ENV{OSXCROSS_ROOT})
    set(OSXCROSS_ROOT "$ENV{OSXCROSS_ROOT}")
else()
    set(OSXCROSS_ROOT "/opt/osxcross")
endif()

set(CMAKE_C_COMPILER "${OSXCROSS_ROOT}/bin/x86_64-apple-darwin21.4-clang")
set(CMAKE_CXX_COMPILER "${OSXCROSS_ROOT}/bin/x86_64-apple-darwin21.4-clang++")

set(CMAKE_OSX_SYSROOT "${OSXCROSS_ROOT}/SDK/MacOSX12.3.sdk")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# macOS-specific flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
