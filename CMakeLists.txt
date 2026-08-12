cmake_minimum_required(VERSION 3.16)
project(UpdateCenter VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

add_executable(UpdateCenter
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/lang.h
    src/lang.cpp
    src/updateitem.h
    src/aptmanager.h
    src/aptmanager.cpp
    src/historymanager.h
    src/historymanager.cpp
    src/repositorymanager.h
    src/repositorymanager.cpp
    resources/resources.qrc
)

target_link_libraries(UpdateCenter PRIVATE Qt6::Widgets Qt6::Network)

set_target_properties(UpdateCenter PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

install(TARGETS UpdateCenter
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
