cmake_minimum_required(VERSION 3.21)

project(QtUsbmuxd
    VERSION 0.1.0
    DESCRIPTION "Qt client for the usbmuxd daemon: discover iOS devices and open TCP connections to them over USB"
    HOMEPAGE_URL "https://github.com/andreabedini/qt-usbmuxd"
    LANGUAGES CXX)

option(QTUSBMUXD_BUILD_TESTS "Build the QtUsbmuxd tests" ${PROJECT_IS_TOP_LEVEL})
option(QTUSBMUXD_BUILD_EXAMPLES "Build the QtUsbmuxd examples" ${PROJECT_IS_TOP_LEVEL})
option(QTUSBMUXD_INSTALL "Generate install rules for QtUsbmuxd" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network)

add_subdirectory(src)

if(QTUSBMUXD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(QTUSBMUXD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
