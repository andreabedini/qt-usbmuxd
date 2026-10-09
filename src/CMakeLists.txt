include(GNUInstallDirs)

set(public_headers
    ../include/QtUsbmuxd/qtusbmuxdglobal.h
    ../include/QtUsbmuxd/qusbmuxaddress.h
    ../include/QtUsbmuxd/qusbmuxclient.h
    ../include/QtUsbmuxd/qusbmuxdevice.h
    ../include/QtUsbmuxd/qusbmuxsocket.h
    ../include/QtUsbmuxd/QUsbmuxAddress
    ../include/QtUsbmuxd/QUsbmuxClient
    ../include/QtUsbmuxd/QUsbmuxDevice
    ../include/QtUsbmuxd/QUsbmuxSocket
)

add_library(QtUsbmuxd
    ${public_headers}
    qusbmuxaddress.cpp
    qusbmuxclient.cpp
    qusbmuxdevice.cpp
    qusbmuxplist.cpp
    qusbmuxplist_p.h
    qusbmuxprotocol.cpp
    qusbmuxprotocol_p.h
    qusbmuxsocket.cpp
    qusbmuxtransport.cpp
    qusbmuxtransport_p.h
)
add_library(QtUsbmuxd::QtUsbmuxd ALIAS QtUsbmuxd)

set_target_properties(QtUsbmuxd PROPERTIES
    AUTOMOC ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    EXPORT_NAME QtUsbmuxd
)

target_include_directories(QtUsbmuxd
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/QtUsbmuxd>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/QtUsbmuxd>
)

target_compile_definitions(QtUsbmuxd
    PRIVATE
        QTUSBMUXD_BUILD_LIBRARY
        QTUSBMUXD_VERSION_STRING="${PROJECT_VERSION}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
        QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(QtUsbmuxd PUBLIC QTUSBMUXD_STATIC)
endif()

target_link_libraries(QtUsbmuxd PUBLIC Qt6::Core Qt6::Network)

if(MSVC)
    target_compile_options(QtUsbmuxd PRIVATE /W4)
else()
    target_compile_options(QtUsbmuxd PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(QTUSBMUXD_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS QtUsbmuxd
        EXPORT QtUsbmuxdTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES ${public_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/QtUsbmuxd)

    install(EXPORT QtUsbmuxdTargets
        NAMESPACE QtUsbmuxd::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QtUsbmuxd
    )
    configure_package_config_file(
        ${PROJECT_SOURCE_DIR}/cmake/QtUsbmuxdConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/QtUsbmuxdConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QtUsbmuxd
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/QtUsbmuxdConfigVersion.cmake
        COMPATIBILITY SameMinorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/QtUsbmuxdConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/QtUsbmuxdConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QtUsbmuxd
    )
endif()
