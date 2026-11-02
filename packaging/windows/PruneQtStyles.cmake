if(NOT DEFINED SONY_DEPLOY_ROOT)
    message(FATAL_ERROR "SONY_DEPLOY_ROOT is required")
endif()
get_filename_component(_deploy_root "${SONY_DEPLOY_ROOT}" REALPATH)
if(NOT EXISTS "${_deploy_root}/sony-device-center.exe" OR
   NOT EXISTS "${_deploy_root}/Qt6Core.dll" OR
   NOT EXISTS "${_deploy_root}/qml/QtQuick/Controls/Basic/qmldir")
    message(FATAL_ERROR "Refusing to prune an incomplete application deployment")
endif()

# main.cpp fixes the QML style to Basic. These optional styles cannot be
# selected by the app; retain Basic, Templates, impl and all graphics runtimes.
foreach(_style IN ITEMS Fusion Imagine Material Universal FluentWinUI3 Windows)
    set(_style_path "${_deploy_root}/qml/QtQuick/Controls/${_style}")
    string(FIND "${_style_path}" "${_deploy_root}/" _prefix_position)
    if(NOT _prefix_position EQUAL 0)
        message(FATAL_ERROR "Style path escapes the deployment")
    endif()
    if(IS_SYMLINK "${_style_path}")
        message(FATAL_ERROR "Refusing to prune a linked style directory")
    endif()
    file(REMOVE_RECURSE "${_style_path}")
    file(REMOVE
        "${_deploy_root}/Qt6QuickControls2${_style}.dll"
        "${_deploy_root}/Qt6QuickControls2${_style}StyleImpl.dll")
endforeach()
file(WRITE "${_deploy_root}/qt.conf" "[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\n")
