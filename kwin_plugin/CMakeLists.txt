cmake_minimum_required(VERSION 3.20)
project(kwin_touch_gesture_plus_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Qt6 REQUIRED COMPONENTS Core Gui DBus Widgets)
find_package(KWin REQUIRED)
find_package(KF6 REQUIRED COMPONENTS KCMUtils CoreAddons Config ConfigWidgets I18n)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# 1. KWin Plugin Runtime (tablet_gestures)
add_library(tablet_gestures MODULE
    tablet_gestures.cpp
    action_registry.cpp
    zone_manager.cpp
    diagnostics_logger.cpp
    custom_command_store.cpp
)

target_link_libraries(tablet_gestures PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Widgets
    KF6::ConfigCore
    KWin::kwin
)

install(TARGETS tablet_gestures DESTINATION /usr/lib/qt6/plugins/kwin/plugins)

# 2. KWin Effect Configuration Module (kwin_tablet_gestures_config)
add_library(kwin_tablet_gestures_config MODULE
    config/tablet_gestures_config.cpp
    config/tablet_gestures_config_widget.cpp
    action_registry.cpp
    zone_manager.cpp
    diagnostics_logger.cpp
    custom_command_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kwin_effect/contents/ui/config.ui
)

set_target_properties(kwin_tablet_gestures_config PROPERTIES
    PREFIX ""
    OUTPUT_NAME "kwin_tablet_gestures_config"
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../kwin_effect/contents/ui"
)

target_include_directories(kwin_tablet_gestures_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(kwin_tablet_gestures_config PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Widgets
    KF6::KCMUtils
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::ConfigWidgets
    KF6::I18n
    KWin::kwin
)

install(TARGETS kwin_tablet_gestures_config DESTINATION /usr/lib/qt6/plugins/kwin/effects/configs)
