cmake_minimum_required(VERSION 3.20)
project(kwin_touch_gesture_plus VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTING "Build the test suite" ON)

add_subdirectory(kwin_plugin)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
