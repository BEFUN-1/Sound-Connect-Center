cmake_minimum_required(VERSION 3.16)
project(sony-device-center LANGUAGES CXX)

# Version comes from the top-level project; keep a fallback so this
# directory can still be configured on its own.
if(NOT DEFINED SonyDeviceCenter_VERSION)
    set(SonyDeviceCenter_VERSION "0.0.0-dev")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(SONY_REQUIRE_QT
    "Fail configuration when Qt 6 is missing instead of skipping the desktop app" OFF)

find_package(Qt6 COMPONENTS Core Gui Widgets Qml Quick QuickControls2 QUIET)

if(NOT Qt6_FOUND)
    if(SONY_REQUIRE_QT)
        message(FATAL_ERROR
            "Qt 6 (Core, Gui, Widgets, Qml, Quick, QuickControls2) not found, and "
            "SONY_REQUIRE_QT is ON. Release packages must contain the desktop app.")
    endif()
    message(WARNING "Qt 6 (Core, Gui, Widgets, Qml, Quick, QuickControls2) not found — skipping sony-device-center target.")
    return()
endif()


add_executable(sony-device-center
    main.cpp
    src/DeviceCenterController.cpp
    src/DeviceBackend.cpp
    include/DeviceBackend.h
    include/DeviceCenterController.h
    src/I18nManager.cpp
    include/I18nManager.h
    qml.qrc
)
sony_windows_version(sony-device-center)

if(WIN32)
    # MinGW windres needs its temporary preprocessor command line when the
    # workspace path contains spaces (for example "E:/AI Workshop").
    set(_sony_icon_object "${CMAKE_CURRENT_BINARY_DIR}/sony-device-center-icon.o")
    add_custom_command(
        OUTPUT "${_sony_icon_object}"
        COMMAND "${CMAKE_RC_COMPILER}" --use-temp-file -O coff
            "apps/device-center/sony-device-center.rc"
            "${_sony_icon_object}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sony-device-center.rc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/windows/sony-device-center.ico"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        VERBATIM)
    set_source_files_properties("${_sony_icon_object}" PROPERTIES
        GENERATED TRUE
        EXTERNAL_OBJECT TRUE)
    target_sources(sony-device-center PRIVATE "${_sony_icon_object}")
endif()

target_compile_features(sony-device-center PRIVATE cxx_std_20)

target_compile_definitions(sony-device-center
    PRIVATE
        SONY_DEVICE_CENTER_VERSION="${SonyDeviceCenter_VERSION}")

target_include_directories(sony-device-center
    PRIVATE
        include
)

target_link_libraries(sony-device-center
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        sony-core
        sony-protocol
        sony-transport
        Threads::Threads
)

# The hero images are compiled in through qml.qrc. On macOS this loose copy
# would land inside the bundle at Contents/MacOS/resources, 21 MB the app
# never reads, so it is skipped there.
if(NOT APPLE)
    add_custom_command(TARGET sony-device-center POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/../../Client/resources/devices"
            "$<TARGET_FILE_DIR:sony-device-center>/resources/devices"
        COMMENT "Copying device hero assets for device-center")
endif()

if(APPLE)
    # Ships the brand icon inside the .app and makes the previously unused
    # packaging/macos/Info.plist.in the bundle's real property list.
    set(SONY_APP_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/macos/AppIcon.icns")
    set(MACOSX_BUNDLE_ICON_FILE "AppIcon.icns")
    set_source_files_properties("${SONY_APP_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(sony-device-center PRIVATE "${SONY_APP_ICNS}")

    set_target_properties(sony-device-center PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "Sony Device Center"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.befun1.soundconnectcenter"
        MACOSX_BUNDLE_BUNDLE_VERSION "${SonyDeviceCenter_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${SonyDeviceCenter_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/macos/Info.plist.in")

    # `cmake --build build --target dmg` produces the drag-to-Applications
    # disk image: macdeployqt, sonyd/sonyctl folded into the bundle, signing,
    # dmgbuild. See packaging/macos/build-dmg.sh for the knobs.
    add_custom_target(dmg
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/macos/build-dmg.sh" "${CMAKE_BINARY_DIR}"
        DEPENDS sony-device-center sonyd sonyctl
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Building the macOS disk image"
        VERBATIM)
endif()

install(TARGETS sony-device-center
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .)

if(WIN32)
    # A console-subsystem binary pops a terminal window next to the GUI on
    # every launch. The app has no console UI, so build it as a GUI-subsystem
    # executable; Qt6::Core supplies the WinMain shim.
    set_target_properties(sony-device-center PROPERTIES WIN32_EXECUTABLE ON)

    # A bare .exe cannot start without the Qt DLLs and QML module tree beside
    # it. Deploy the runtime both beside the build executable (so a freshly
    # built Release target is runnable) and into the install prefix.
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin}")

    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR
            "windeployqt was not found beside Qt6::qmake. "
            "A Windows Qt build cannot produce a runnable application without "
            "the Qt runtime. Check CMAKE_PREFIX_PATH or install Qt's deployment tools.")
    endif()

    add_custom_command(TARGET sony-device-center POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --release
            --no-translations
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
            "$<TARGET_FILE:sony-device-center>"
        COMMAND "${CMAKE_COMMAND}"
            "-DSONY_DEPLOY_ROOT=$<TARGET_FILE_DIR:sony-device-center>"
            -P "${CMAKE_SOURCE_DIR}/packaging/windows/PruneQtStyles.cmake"
        COMMENT "Deploying Qt runtime beside sony-device-center.exe"
        VERBATIM)

    # Keep install-time deployment strict as well. `execute_process()` otherwise
    # hides a failed windeployqt invocation and leaves an unusable installer
    # payload behind.
    set(_qt_install_root "\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
    set(_qt_install_code "
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --release --no-translations
                --qmldir \"${CMAKE_CURRENT_SOURCE_DIR}/qml\"
                \"${_qt_install_root}/sony-device-center.exe\"
            RESULT_VARIABLE _sony_windeployqt_result
            OUTPUT_VARIABLE _sony_windeployqt_stdout
            ERROR_VARIABLE _sony_windeployqt_stderr)
        if(NOT _sony_windeployqt_result EQUAL 0)
            message(FATAL_ERROR
                \"windeployqt failed with exit code \${_sony_windeployqt_result}: \"
                \"\${_sony_windeployqt_stderr}\")
        endif()
        foreach(_sony_required_qt_file IN ITEMS
            Qt6Core.dll
            Qt6Gui.dll
            Qt6Qml.dll
            Qt6Quick.dll
            Qt6QuickControls2.dll
            platforms/qwindows.dll
            qml/QtQml/qmldir
            qml/QtQuick/qmldir
            qml/QtQuick/Controls/qmldir)
            if(NOT EXISTS \"${_qt_install_root}/\${_sony_required_qt_file}\")
                message(FATAL_ERROR
                    \"Qt deployment is incomplete; missing \${_sony_required_qt_file} in \"
                    \"${_qt_install_root}\")
            endif()
        endforeach()
    ")
    install(CODE "${_qt_install_code}")
    install(CODE "
        set(SONY_DEPLOY_ROOT \"${_qt_install_root}\")
        include(\"${CMAKE_SOURCE_DIR}/packaging/windows/PruneQtStyles.cmake\")
    ")
endif()


if(BUILD_TESTING)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(TARGET Qt6::Test)
        add_executable(sony-ui-tests
            ../../tests/ui/DeviceControllerTests.cpp
            src/DeviceCenterController.cpp include/DeviceCenterController.h
            src/DeviceBackend.cpp include/DeviceBackend.h
            src/I18nManager.cpp include/I18nManager.h)
        target_include_directories(sony-ui-tests PRIVATE include)
        target_compile_definitions(sony-ui-tests PRIVATE SONY_DEVICE_CENTER_VERSION="${SonyDeviceCenter_VERSION}")
        target_link_libraries(sony-ui-tests PRIVATE Qt6::Test Qt6::Gui sony-core)
        if(WIN32)
            # UI tests are real Qt executables. Deploy their runtime beside
            # the binary so CTest and direct validation runs do not depend on
            # the developer shell having Qt's bin directory in PATH.
            add_custom_command(TARGET sony-ui-tests POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --release
                    --no-translations
                    --compiler-runtime
                    "$<TARGET_FILE:sony-ui-tests>"
                COMMENT "Deploying Qt runtime beside sony-ui-tests.exe"
                VERBATIM)
        endif()
        add_test(NAME "device-controller-worker-integration" COMMAND sony-ui-tests
            -o ui-controller-results.txt,txt -o -,txt)
        set_tests_properties("device-controller-worker-integration" PROPERTIES TIMEOUT 45)
        add_executable(sony-ui-lifecycle-tests
            main.cpp ../../tests/ui/UiScenario.cpp
            src/DeviceCenterController.cpp include/DeviceCenterController.h
            src/DeviceBackend.cpp include/DeviceBackend.h
            src/I18nManager.cpp include/I18nManager.h qml.qrc)
        target_include_directories(sony-ui-lifecycle-tests PRIVATE include ../../tests/ui)
        target_compile_definitions(sony-ui-lifecycle-tests PRIVATE
            SONY_UI_INTEGRATION_TEST SONY_DEVICE_CENTER_VERSION="${SonyDeviceCenter_VERSION}")
        target_link_libraries(sony-ui-lifecycle-tests PRIVATE
            Qt6::Test Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 sony-core)
        if(WIN32)
            target_link_libraries(sony-ui-lifecycle-tests PRIVATE psapi)
        endif()
        add_dependencies(sony-ui-lifecycle-tests sony-device-center sony-ui-tests)
        add_test(NAME device-ui-lifecycle COMMAND sony-ui-lifecycle-tests)
        if(WIN32)
            set_tests_properties(device-ui-lifecycle PROPERTIES TIMEOUT 30
                ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=software")
        else()
            set_tests_properties(device-ui-lifecycle PROPERTIES TIMEOUT 30
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
        endif()
    endif()
endif()

if(BUILD_TESTING AND UNIX AND NOT APPLE)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME simulated-app-smoke
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/smoke/simulated.py
                $<TARGET_FILE:sonyd> $<TARGET_FILE:sonyctl> $<TARGET_FILE:sony-device-center>)
        set_tests_properties(simulated-app-smoke PROPERTIES TIMEOUT 50)
    endif()
endif()
