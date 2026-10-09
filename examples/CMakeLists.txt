add_executable(usbmux usbmux/main.cpp)
target_link_libraries(usbmux PRIVATE QtUsbmuxd::QtUsbmuxd)
