cmake_minimum_required(VERSION 3.14)
project(sony-protocol LANGUAGES CXX)

option(SONY_BUILD_CAPABILITY_DISCOVERY "Build optional filesystem-backed capability discovery" ON)

add_library(sony-protocol
    src/FrameCodec.cpp
    src/EqualizerPresets.cpp
    src/SonyProtocolSession.cpp
    src/ProtocolV1.cpp
    src/ProtocolV2.cpp
    src/DeviceProfileRegistry.cpp
    src/DeviceEventDispatcher.cpp
)

if(SONY_BUILD_CAPABILITY_DISCOVERY)
    target_sources(sony-protocol PRIVATE src/CapabilityCache.cpp src/CapabilityDiscovery.cpp)
endif()

target_compile_features(sony-protocol PUBLIC cxx_std_20)

target_include_directories(sony-protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(sony-protocol PUBLIC sony-transport)
