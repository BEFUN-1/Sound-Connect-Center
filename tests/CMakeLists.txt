cmake_minimum_required(VERSION 3.14)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SoundConnectCenterTests LANGUAGES CXX)
    include(CTest)
endif()

if(NOT BUILD_TESTING)
    return()
endif()

find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(Catch2
        URL https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.8.1
        URL_HASH SHA256=18b3f70ac80fccc340d8c6ff0f339b2ae64944782f8d2fca2bd705cf47cadb79
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

find_package(Threads REQUIRED)
include(Catch)

if(WIN32 AND TARGET sonyctl)
    add_executable(sony-discovery-soak EXCLUDE_FROM_ALL hardware/WindowsDiscoverySoak.cpp)
    target_link_libraries(sony-discovery-soak PRIVATE sony-transport psapi)
    sony_deploy_mingw_runtime(sony-discovery-soak)
    add_executable(sony-xm6-readonly-probe EXCLUDE_FROM_ALL hardware/Xm6ReadOnlyProbe.cpp)
    target_link_libraries(sony-xm6-readonly-probe PRIVATE sony-core)
    sony_deploy_mingw_runtime(sony-xm6-readonly-probe)
endif()

# On Windows the MinGW runtime is deployed beside the test binaries only
# after linking. Discovering Catch2 tests at build time would launch the
# executable before those DLLs exist, so defer discovery until CTest runs.
set(CMAKE_CATCH_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

if(NOT TARGET sony-transport)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../libs/sony-transport" "${CMAKE_CURRENT_BINARY_DIR}/libs/sony-transport")
endif()

if(NOT TARGET sony-protocol)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../libs/sony-protocol" "${CMAKE_CURRENT_BINARY_DIR}/libs/sony-protocol")
endif()

if(NOT TARGET sony-core)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../libs/sony-core" "${CMAKE_CURRENT_BINARY_DIR}/libs/sony-core")
endif()

set(SOUNDCONNECT_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Client")

add_executable(sony-protocol-tests
    protocol/FrameCodecTests.cpp
    protocol/SonyProtocolSessionTests.cpp
    protocol/ProtocolV1Tests.cpp
    protocol/ProtocolV2Tests.cpp
    protocol/MultipointTests.cpp
    protocol/DeviceProfileTests.cpp
    protocol/CapabilityDiscoveryTests.cpp
    protocol/DeviceStateTests.cpp
    protocol/DeviceEventDispatcherTests.cpp
    protocol/SonyErrorTests.cpp
    protocol/CommandSerializerTests.cpp
    protocol/BluetoothWrapperTests.cpp
    "${SOUNDCONNECT_CLIENT_DIR}/CommandSerializer.cpp"
    "${SOUNDCONNECT_CLIENT_DIR}/ByteMagic.cpp"
    "${SOUNDCONNECT_CLIENT_DIR}/BluetoothWrapper.cpp"
    "${SOUNDCONNECT_CLIENT_DIR}/Headphones.cpp"
)
target_compile_features(sony-protocol-tests PRIVATE cxx_std_20)
target_include_directories(sony-protocol-tests PRIVATE "${SOUNDCONNECT_CLIENT_DIR}")
target_link_libraries(sony-protocol-tests PRIVATE Catch2::Catch2WithMain Threads::Threads sony-transport sony-protocol)
catch_discover_tests(sony-protocol-tests PROPERTIES TIMEOUT 10)

add_executable(sony-transport-tests
    transport/FakeTransportTests.cpp
    transport/TransportAdapterTests.cpp
    transport/LoggerTests.cpp
    transport/SonyDeviceFilterTests.cpp
    "${SOUNDCONNECT_CLIENT_DIR}/CommandSerializer.cpp"
    "${SOUNDCONNECT_CLIENT_DIR}/ByteMagic.cpp"
    "${SOUNDCONNECT_CLIENT_DIR}/BluetoothWrapper.cpp"
)
target_compile_features(sony-transport-tests PRIVATE cxx_std_20)
target_include_directories(sony-transport-tests PRIVATE "${SOUNDCONNECT_CLIENT_DIR}")
target_link_libraries(sony-transport-tests PRIVATE Catch2::Catch2WithMain Threads::Threads sony-transport sony-protocol)
catch_discover_tests(sony-transport-tests PROPERTIES TIMEOUT 10)

add_executable(sony-core-tests
    core/DesktopSessionTests.cpp
    ../apps/sonyctl/DesktopSession.cpp
    core/SonyDeviceTests.cpp
    core/IpcProtocolTests.cpp
    core/ReliabilityTests.cpp
    core/ProtocolSafetyTests.cpp
)
target_compile_features(sony-core-tests PRIVATE cxx_std_20)
target_link_libraries(sony-core-tests PRIVATE Catch2::Catch2WithMain Threads::Threads sony-transport sony-protocol sony-core)
# V1 lifecycle cases intentionally leave some queries unanswered and spend
# about nine seconds in protocol timeouts. Allow headroom on slower runners
# (see #62); the protocol's own request deadlines remain unchanged.
catch_discover_tests(sony-core-tests PROPERTIES TIMEOUT 30)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Keep the MinGW runtime beside those binaries so CTest can launch them
    # without relying on the developer shell's PATH.
    get_filename_component(_mingw_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_test_target IN ITEMS sony-protocol-tests sony-transport-tests sony-core-tests)
        foreach(_runtime IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
            add_custom_command(TARGET ${_test_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_mingw_runtime_dir}/${_runtime}"
                    "$<TARGET_FILE_DIR:${_test_target}>/${_runtime}"
                VERBATIM)
        endforeach()
    endforeach()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND AND TARGET sonyctl AND TARGET sonyd)
        add_test(NAME windows-command-runtime
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/smoke/windows_runtime.py"
                $<TARGET_FILE:sonyctl> $<TARGET_FILE:sonyd>)
        set_tests_properties(windows-command-runtime PROPERTIES TIMEOUT 30)
    endif()
endif()
