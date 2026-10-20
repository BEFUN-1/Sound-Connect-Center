cmake_minimum_required(VERSION 3.14)
project(sonyctl LANGUAGES CXX)

add_executable(sonyctl
    main.cpp
    DesktopSession.cpp
)

target_compile_features(sonyctl PRIVATE cxx_std_20)
target_link_libraries(sonyctl PRIVATE sony-core sony-protocol sony-transport Threads::Threads)

if(MINGW AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_link_options(sonyctl PRIVATE "-Wl,--image-base,0x140000000")
endif()

install(TARGETS sonyctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
