find_package(Qt6 REQUIRED COMPONENTS Core Test DBus Widgets)
find_package(KWin REQUIRED)
find_package(KF6 REQUIRED COMPONENTS Config)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# 1. Zone Manager & Action Tests
add_executable(test_zone_manager
    test_zone_manager.cpp
    ../kwin_plugin/zone_manager.cpp
    ../kwin_plugin/action_registry.cpp
    ../kwin_plugin/diagnostics_logger.cpp
    ../kwin_plugin/custom_command_store.cpp
)

target_link_libraries(test_zone_manager PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::DBus
    Qt6::Widgets
    KF6::ConfigCore
    KWin::kwin
)

add_test(NAME test_zone_manager COMMAND test_zone_manager)

# 2. Custom Command Manager & Workflow Tests
add_executable(test_command_manager
    test_command_manager.cpp
    ../kwin_plugin/config/tablet_gestures_config_widget.cpp
    ../kwin_plugin/action_registry.cpp
    ../kwin_plugin/zone_manager.cpp
    ../kwin_plugin/diagnostics_logger.cpp
    ../kwin_plugin/custom_command_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../kwin_effect/contents/ui/config.ui
)

set_target_properties(test_command_manager PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../kwin_effect/contents/ui"
)

target_include_directories(test_command_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../kwin_plugin
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(test_command_manager PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::DBus
    Qt6::Widgets
    KF6::ConfigCore
    KF6::ConfigGui
    KWin::kwin
)

add_test(NAME test_command_manager COMMAND test_command_manager)
