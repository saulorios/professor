cmake_minimum_required(VERSION 3.16)

project(Professor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# moc automático (Q_OBJECT) e compilação automática do arquivo .qrc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(Professor
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/TitleBar.h
    src/TitleBar.cpp
    resources/resources.qrc
)

target_link_libraries(Professor PRIVATE Qt6::Widgets)
