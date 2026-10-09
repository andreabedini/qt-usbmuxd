# Builds against an installed QtUsbmuxd, to check the CMake package works.
#   cmake -S tests/package -B build-package -DCMAKE_PREFIX_PATH=<install prefix>
cmake_minimum_required(VERSION 3.21)
project(QtUsbmuxdPackageTest LANGUAGES CXX)

find_package(QtUsbmuxd 0.1 REQUIRED)

add_executable(package-test main.cpp)
target_link_libraries(package-test PRIVATE QtUsbmuxd::QtUsbmuxd)
