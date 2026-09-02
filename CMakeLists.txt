cmake_minimum_required(VERSION 3.13)
project(dshare VERSION 1.0.0 LANGUAGES CXX)

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

add_executable(dshare
    main.cpp
    mainwindow.cpp
    fileserver.cpp
    fileuploader.cpp
    fileuploader.h
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

target_link_libraries(dshare PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::HttpServer
    Dtk6::Core
    Dtk6::Gui
    Dtk6::Widget
    Dtk6::Log
    qrencode
)

install(TARGETS dshare RUNTIME DESTINATION bin)
install(FILES dshare.desktop DESTINATION share/applications)
install(FILES icons/dshare.svg DESTINATION share/icons/hicolor/scalable/apps)

# Translations: DApplication::loadTranslator() looks for <appName>_<locale>.qm in
# AppDataLocation/translations (e.g. /usr/share/dshare/translations and
# ~/.local/share/dshare/translations) and in <bindir>/translations. Install the
# qm system-wide and copy it next to the binary so an uninstalled build run also finds it.
set(DSHARE_QM ${CMAKE_CURRENT_SOURCE_DIR}/translations/dshare_zh_CN.qm)
install(FILES ${DSHARE_QM} DESTINATION share/dshare/translations)
add_custom_command(TARGET dshare POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:dshare>/translations
    COMMAND ${CMAKE_COMMAND} -E copy ${DSHARE_QM}
            $<TARGET_FILE_DIR:dshare>/translations/dshare_zh_CN.qm
    COMMENT "Copying translation file next to the binary")
