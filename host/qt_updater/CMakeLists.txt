cmake_minimum_required(VERSION 3.16)

project(stm32f407_iap_qt_updater VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 QUIET COMPONENTS Widgets SerialPort)
if(Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets SerialPort)
    set(QT_PACKAGE Qt5)
endif()

add_executable(stm32f407_iap_qt_updater WIN32
    src/main.cpp
    src/iap_protocol.cpp
    src/iap_protocol.h
    src/firmware_updater.cpp
    src/firmware_updater.h
    src/main_window.cpp
    src/main_window.h
)

target_include_directories(stm32f407_iap_qt_updater PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)
target_link_libraries(stm32f407_iap_qt_updater PRIVATE
    ${QT_PACKAGE}::Widgets
    ${QT_PACKAGE}::SerialPort
)

if(MSVC)
    target_compile_options(stm32f407_iap_qt_updater PRIVATE /W4 /permissive-)
else()
    target_compile_options(stm32f407_iap_qt_updater PRIVATE -Wall -Wextra -Wpedantic)
endif()
