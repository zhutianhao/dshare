cmake_minimum_required(VERSION 3.13)
project(fileshare VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network HttpServer)
find_package(Dtk6Core REQUIRED)
find_package(Dtk6Gui REQUIRED)
find_package(Dtk6Widget REQUIRED)
find_package(Dtk6Log REQUIRED)

add_executable(fileshare
    main.cpp
    mainwindow.cpp
    fileserver.cpp
    updirproxy.h
)

target_link_libraries(fileshare PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::HttpServer
    Dtk6::Core
    Dtk6::Gui
    Dtk6::Widget
    Dtk6::Log
)

install(TARGETS fileshare RUNTIME DESTINATION bin)
install(FILES fileshare.desktop DESTINATION share/applications)
