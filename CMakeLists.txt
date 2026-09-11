cmake_minimum_required(VERSION 3.16)

project(Lousa VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sem tipo de build informado, compila otimizado (a física roda por pixel)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

# moc automático (Q_OBJECT) e compilação automática do arquivo .qrc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Avisos do compilador ativados em todos os alvos
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# physics/: C++ puro, depende só do QtCore (QPointF, QRect) — nunca de QtWidgets
add_library(physics STATIC
    src/physics/ChalkSample.h
    src/physics/PhysicsParams.h
    src/physics/Noise.h
    src/physics/BoardSurface.h
    src/physics/BoardSurface.cpp
    src/physics/ChalkStick.h
    src/physics/ChalkStick.cpp
    src/physics/DepositBuffer.h
    src/physics/DepositBuffer.cpp
    src/physics/StrokeEngine.h
    src/physics/StrokeEngine.cpp
    src/physics/Eraser.h
    src/physics/Eraser.cpp
)
target_include_directories(physics PUBLIC src)
target_link_libraries(physics PUBLIC Qt6::Core)

add_executable(lousa
    src/main.cpp
    src/render/BoardCanvas.h
    src/render/BoardCanvas.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/TitleBar.h
    src/ui/TitleBar.cpp
    resources/resources.qrc
)

target_link_libraries(lousa PRIVATE physics Qt6::Widgets)
