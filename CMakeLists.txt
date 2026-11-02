cmake_minimum_required(VERSION 3.14)

# Qt 6.10 supports macOS 13 and up. Info.plist.in stamps this in as
# LSMinimumSystemVersion, so an older Mac gets a clear refusal from Finder
# instead of a dyld crash. Must precede project().
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()

project(SonyDeviceCenter VERSION 2.0.6 LANGUAGES CXX)

# Keep the compatibility include directory first for the older MinGW toolchain
# commonly available on Windows. Newer libstdc++ versions simply ignore these
# shims because their standard headers are found first by the compiler.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    include_directories(BEFORE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_SANITIZERS "Enable ASan and UBSan compiler instrumentation" OFF)
if(ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

include(CTest)
find_package(Threads REQUIRED)
include(GNUInstallDirs)

function(sony_deploy_mingw_runtime target)
    if(WIN32 AND MINGW)
        get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(_runtime IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_compiler_bin}/${_runtime}" "$<TARGET_FILE_DIR:${target}>/${_runtime}"
                VERBATIM)
        endforeach()
    endif()
endfunction()

function(sony_windows_version target)
    if(NOT WIN32)
        return()
    endif()
    set(SONY_VERSION_TARGET "${target}")
    set(_version_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}-version.rc")
    configure_file("${CMAKE_SOURCE_DIR}/packaging/windows/version.rc.in" "${_version_rc}" @ONLY)
    if(MINGW)
        set(_version_object "${CMAKE_CURRENT_BINARY_DIR}/${target}-version.o")
        add_custom_command(OUTPUT "${_version_object}"
            COMMAND "${CMAKE_RC_COMPILER}" --use-temp-file -O coff "${target}-version.rc" "${target}-version.o"
            DEPENDS "${_version_rc}" WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" VERBATIM)
        set_source_files_properties("${_version_object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
        target_sources(${target} PRIVATE "${_version_object}")
    else()
        target_sources(${target} PRIVATE "${_version_rc}")
    endif()
endfunction()

add_subdirectory(libs/sony-transport)
add_subdirectory(libs/sony-protocol)
add_subdirectory(libs/sony-core)

add_subdirectory(apps/sonyd)
add_subdirectory(apps/sonyctl)
add_subdirectory(apps/device-center)

if(WIN32 AND MINGW)
    get_filename_component(_sony_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_sony_mingw_runtime)
    foreach(_runtime IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        set(_runtime_path "${_sony_compiler_bin}/${_runtime}")
        if(NOT EXISTS "${_runtime_path}")
            message(FATAL_ERROR "Missing compiler runtime: ${_runtime_path}")
        endif()
        list(APPEND _sony_mingw_runtime "${_runtime_path}")
    endforeach()
    install(FILES ${_sony_mingw_runtime} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)

if(WIN32)
    # The binaries link the MSVC runtime dynamically. Without these DLLs beside
    # them the installed .exe dies before main() with "VCRUNTIME140_1.dll was
    # not found" on any machine lacking the VC++ redistributable.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP FALSE)
    include(InstallRequiredSystemLibraries)
endif()

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/sony-device-center.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/sony-device-center.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES packaging/linux/sony-device-center.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
    install(FILES packaging/linux/com.github.befun1.soundconnectcenter.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES packaging/linux/sonyd.service
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user OPTIONAL)
endif()

# Packaging via CPack
set(CPACK_PACKAGE_NAME "sony-device-center")
set(CPACK_PACKAGE_VENDOR "BEFUN-1")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform Sony audio device companion SDK, daemon, CLI, and GUI")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_CONTACT "https://github.com/BEFUN-1/Sound-Connect-Center")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "BEFUN-1")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libbluetooth3, libdbus-1-3")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
elseif(WIN32)
    # CPack provides the portable ZIP. The active per-user NSIS installer is
    # generated by packaging/windows/build-qt-installer.ps1 because CPack's
    # stock NSIS template requests administrator access.
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Sony Device Center")
elseif(APPLE)
    # The shipped artifact is the DMG from the `dmg` target in
    # apps/device-center, which runs macdeployqt first. CPack's DragNDrop
    # would wrap a bundle that still points at the build machine's Qt.
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
