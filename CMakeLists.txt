cmake_minimum_required(VERSION 3.16)

project(Lousa VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# moc automático (Q_OBJECT) e compilação automática do arquivo .qrc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(lousa
    src/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/TitleBar.h
    src/ui/TitleBar.cpp
    resources/resources.qrc
)

# Includes a partir de src/ (ex.: #include "ui/MainWindow.h")
target_include_directories(lousa PRIVATE src)

target_link_libraries(lousa PRIVATE Qt6::Widgets)

# Avisos do compilador ativados
if(MSVC)
    target_compile_options(lousa PRIVATE /W4)
else()
    target_compile_options(lousa PRIVATE -Wall -Wextra -Wpedantic)
endif()
