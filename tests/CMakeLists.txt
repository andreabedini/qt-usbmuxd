find_package(Qt6 REQUIRED COMPONENTS Test)

add_library(fakeusbmuxd STATIC fakeusbmuxd.cpp fakeusbmuxd.h)
set_target_properties(fakeusbmuxd PROPERTIES AUTOMOC ON)
# The fake daemon reuses the library's private framing code.
target_include_directories(fakeusbmuxd PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(fakeusbmuxd PUBLIC QtUsbmuxd::QtUsbmuxd Qt6::Network Qt6::Test)

function(qtusbmuxd_add_test name)
    add_executable(${name} ${name}.cpp)
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    target_link_libraries(${name} PRIVATE fakeusbmuxd)
    target_compile_definitions(${name} PRIVATE
        FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        QT_NO_CAST_FROM_ASCII
    )
    add_test(NAME ${name} COMMAND ${name})
    # Keep the environment from pointing the library at a real daemon.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "USBMUXD_SOCKET_ADDRESS=")
endfunction()

qtusbmuxd_add_test(tst_plist)
qtusbmuxd_add_test(tst_protocol)
qtusbmuxd_add_test(tst_qusbmuxaddress)
qtusbmuxd_add_test(tst_qusbmuxclient)
qtusbmuxd_add_test(tst_qusbmuxsocket)
