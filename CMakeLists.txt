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
    authmanager.cpp
    authmanager.h
    updirproxy.h
    discovery.cpp
    discovery.h
    remotemodel.cpp
    remotemodel.h
    finddialog.cpp
    finddialog.h
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

# Translations: DApplication::loadTranslator() looks for <appName>_<locale>.qm in
# AppDataLocation/translations (e.g. /usr/share/fileshare/translations and
# ~/.local/share/fileshare/translations) and in <bindir>/translations. Install the
# qm system-wide and copy it next to the binary so an uninstalled build run also finds it.
set(FILESHARE_QM ${CMAKE_CURRENT_SOURCE_DIR}/translations/fileshare_zh_CN.qm)
install(FILES ${FILESHARE_QM} DESTINATION share/fileshare/translations)
add_custom_command(TARGET fileshare POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:fileshare>/translations
    COMMAND ${CMAKE_COMMAND} -E copy ${FILESHARE_QM}
            $<TARGET_FILE_DIR:fileshare>/translations/fileshare_zh_CN.qm
    COMMENT "Copying translation file next to the binary")
