cmake_minimum_required(VERSION 3.15)
project(mtk_usb_driver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    mtk_usb_driver.cpp
)

set(HEADERS
    mtk_usb_driver.h
)

# Build shared library (DLL on Windows)
add_library(mtk_usb_driver SHARED ${SOURCES} ${HEADERS})

# Export macro
target_compile_definitions(mtk_usb_driver PRIVATE MTK_USB_EXPORTS)

if(WIN32)
    # Windows-specific settings
    target_link_libraries(mtk_usb_driver PRIVATE
        setupapi
        cfgmgr32
    )

    # Set output name
    set_target_properties(mtk_usb_driver PROPERTIES
        OUTPUT_NAME "mtk_usb_driver"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    # Install DLL to the Windows directory alongside libusb DLLs
    install(TARGETS mtk_usb_driver
        RUNTIME DESTINATION "${CMAKE_SOURCE_DIR}/../../../Windows"
        LIBRARY DESTINATION "${CMAKE_SOURCE_DIR}/../../../Windows"
    )
else()
    # Linux/macOS stub
    set_target_properties(mtk_usb_driver PROPERTIES
        OUTPUT_NAME "mtk_usb_driver"
        PREFIX "lib"
    )
endif()

# --- Build instructions ---
# Windows (MSVC):
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 17 2022" -A x64
#   cmake --build . --config Release
#
# Windows (MinGW-w64 cross-compile from Linux):
#   mkdir build && cd build
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-mingw64.cmake
#   cmake --build .
#
# Linux (stub build for testing):
#   mkdir build && cd build
#   cmake ..
#   cmake --build .
