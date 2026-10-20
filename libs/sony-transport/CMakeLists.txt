cmake_minimum_required(VERSION 3.14)
project(sony-transport LANGUAGES CXX)

set(SOUNDCONNECT_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../Client" CACHE PATH "Path to Client directory")

add_library(sony-transport
    src/FakeTransport.cpp
    src/BluetoothConnectorTransport.cpp
    src/TransportBluetoothConnector.cpp
    src/Logger.cpp
    src/PlatformTransport.cpp
    src/SonyDeviceFilter.cpp
)

target_compile_features(sony-transport PUBLIC cxx_std_20)

target_include_directories(sony-transport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${SOUNDCONNECT_CLIENT_DIR}"
)

find_package(Threads REQUIRED)
target_link_libraries(sony-transport PUBLIC Threads::Threads)

if(UNIX AND NOT APPLE)
    list(APPEND CMAKE_MODULE_PATH "${SOUNDCONNECT_CLIENT_DIR}/linux")
    find_package(DBus QUIET)
    find_library(BLUETOOTH_LIB NAMES bluetooth)

    if(DBUS_FOUND AND BLUETOOTH_LIB)
        target_sources(sony-transport PRIVATE
            "${SOUNDCONNECT_CLIENT_DIR}/linux/LinuxBluetoothConnector.cpp"
            "${SOUNDCONNECT_CLIENT_DIR}/linux/DBusHelper.cpp"
        )
        target_include_directories(sony-transport PRIVATE
            ${DBUS_INCLUDE_DIRS}
            "${SOUNDCONNECT_CLIENT_DIR}/linux"
        )
        target_link_libraries(sony-transport PUBLIC
            ${DBUS_LIBRARIES}
            ${BLUETOOTH_LIB}
        )
        target_compile_definitions(sony-transport PUBLIC SONY_HAS_LINUX_BLUETOOTH=1)
    endif()
elseif(WIN32)
    target_sources(sony-transport PRIVATE
        "${SOUNDCONNECT_CLIENT_DIR}/windows/WindowsBluetoothConnector.cpp"
        # WindowsBluetoothConnector calls MACStringToLong / MACBytesToString.
        "${SOUNDCONNECT_CLIENT_DIR}/ByteMagic.cpp"
    )
    target_include_directories(sony-transport PRIVATE
        "${SOUNDCONNECT_CLIENT_DIR}/windows"
    )
    target_link_libraries(sony-transport PUBLIC bthprops ws2_32 rpcrt4)
    target_compile_definitions(sony-transport PUBLIC SONY_HAS_WINDOWS_BLUETOOTH=1)
elseif(APPLE)
    set_source_files_properties(
        "${SOUNDCONNECT_CLIENT_DIR}/macos/MacOSBluetoothConnector.mm"
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
    target_sources(sony-transport PRIVATE
        "${SOUNDCONNECT_CLIENT_DIR}/macos/MacOSBluetoothConnector.mm"
    )
    target_include_directories(sony-transport PRIVATE
        "${SOUNDCONNECT_CLIENT_DIR}/macos"
    )
    target_link_libraries(sony-transport PUBLIC "-framework IOBluetooth" "-framework Foundation")
    target_compile_definitions(sony-transport PUBLIC SONY_HAS_MACOS_BLUETOOTH=1)
endif()

