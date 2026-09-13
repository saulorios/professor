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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets SvgWidgets)
find_package(Qt6 OPTIONAL_COMPONENTS Test)

option(LOUSA_BUILD_TESTS "Compila os testes (precisa do Qt6::Test)" ON)

# Avisos do compilador ativados em todos os alvos; no MSVC, fontes lidos como UTF-8
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Cada camada é uma biblioteca que só enxerga a camada logo abaixo:
# protocol → scene → hand → physics

# physics/: C++ puro, depende só do QtCore (QPointF, QRect) — nunca de QtWidgets
add_library(physics STATIC
    src/physics/Board.h
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

# hand/: mão virtual (polilinhas → ChalkSamples ao longo do tempo)
add_library(hand STATIC
    src/hand/Polyline.h
    src/hand/ChalkPose.h
    src/hand/HandParams.h
    src/hand/VirtualHand.h
    src/hand/VirtualHand.cpp
)
target_link_libraries(hand PUBLIC physics)

# scene/: elementos por id, posicionamento, geometria 2D e objetos 3D
# (QtGui entra por causa de QVector3D/QMatrix4x4 na projeção)
add_library(scene STATIC
    src/scene/SceneParams.h
    src/scene/Geometry2D.h
    src/scene/Geometry2D.cpp
    src/scene/Expression.h
    src/scene/Expression.cpp
    src/scene/ChartGeometry.h
    src/scene/ChartGeometry.cpp
    src/scene/HersheyFont.h
    src/scene/HersheyFont.cpp
    src/scene/Humanizer.h
    src/scene/Humanizer.cpp
    src/scene/JsonHelpers.h
    src/scene/Layout.h
    src/scene/Layout.cpp
    src/scene/Occupancy.h
    src/scene/Occupancy.cpp
    src/scene/TextLayout.h
    src/scene/TextLayout.cpp
    src/scene/solids/Mesh.h
    src/scene/solids/Mesh.cpp
    src/scene/solids/Solids.h
    src/scene/solids/Solids.cpp
    src/scene/Projection.h
    src/scene/Projection.cpp
    src/scene/HiddenLines.h
    src/scene/HiddenLines.cpp
    src/scene/Object3D.h
    src/scene/Object3D.cpp
    src/scene/Scene.h
    src/scene/Scene.cpp
)
target_link_libraries(scene PUBLIC hand handwriting Qt6::Gui)

# handwriting/: banco de gestos de escrita manual (glifos capturados, strokes com
# tempo) e o contrato da WritingTrajectory. Independente da lousa, do giz e da
# mão: só enxerga o QtCore. Quem o consome fica acima (ui/ hoje; scene/ e o
# HumanMotionEngine nas próximas etapas).
add_library(handwriting STATIC
    src/handwriting/HandwritingParams.h
    src/handwriting/types/HandwritingTypes.h
    src/handwriting/types/WritingTrajectory.h
    src/handwriting/data/GlyphGeometry.h
    src/handwriting/data/GlyphGeometry.cpp
    src/handwriting/data/GlyphSerializer.h
    src/handwriting/data/GlyphSerializer.cpp
    src/handwriting/data/GlyphDatabase.h
    src/handwriting/data/GlyphDatabase.cpp
    src/handwriting/recorder/StrokeRecorder.h
    src/handwriting/recorder/StrokeRecorder.cpp
    src/handwriting/engine/HandwritingEngine.h
    src/handwriting/engine/HandwritingEngine.cpp
)
target_include_directories(handwriting PUBLIC src)
target_link_libraries(handwriting PUBLIC Qt6::Core)

# protocol/: parser de JSON Lines, fila de comandos e cliente da IA
# (QtNetwork entra por causa do QNetworkAccessManager do AiClient)
add_library(protocol STATIC
    src/protocol/CommandParser.h
    src/protocol/CommandParser.cpp
    src/protocol/CommandQueue.h
    src/protocol/CommandQueue.cpp
    src/protocol/AiClient.h
    src/protocol/AiClient.cpp
    src/protocol/ProxyLauncher.h
    src/protocol/ProxyLauncher.cpp
)
target_link_libraries(protocol PUBLIC scene Qt6::Network)

add_executable(lousa
    src/main.cpp
    src/render/BoardCanvas.h
    src/render/BoardCanvas.cpp
    src/render/ChalkOverlay.h
    src/render/ChalkOverlay.cpp
    src/ui/AgentPanel.h
    src/ui/AgentPanel.cpp
    src/ui/HandwritingCapture.h
    src/ui/HandwritingCapture.cpp
    src/ui/HandwritingRecorder.h
    src/ui/HandwritingRecorder.cpp
    src/ui/LessonEditor.h
    src/ui/LessonEditor.cpp
    src/ui/LessonPlayer.h
    src/ui/LessonPlayer.cpp
    src/ui/LessonRecorder.h
    src/ui/LessonRecorder.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/PlayerBar.h
    src/ui/PlayerBar.cpp
    src/ui/TitleBar.h
    src/ui/TitleBar.cpp
    src/ui/TuningPanel.h
    src/ui/TuningPanel.cpp
    resources/resources.qrc
)

target_link_libraries(lousa PRIVATE protocol handwriting Qt6::Widgets Qt6::SvgWidgets)

# Testes (ctest --test-dir build)
if(LOUSA_BUILD_TESTS AND TARGET Qt6::Test)
    enable_testing()
    add_executable(handwriting_tests tests/handwriting/tst_handwriting.cpp)
    target_link_libraries(handwriting_tests PRIVATE handwriting Qt6::Test)
    add_test(NAME handwriting COMMAND handwriting_tests)

    add_executable(scene_tests tests/scene/tst_scene.cpp resources/resources.qrc)
    target_link_libraries(scene_tests PRIVATE scene Qt6::Test)
    add_test(NAME scene COMMAND scene_tests)
endif()
