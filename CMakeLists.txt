cmake_minimum_required(VERSION 3.16)
project(surface_survey_tools LANGUAGES CXX)

# ── Language standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Qt auto-tools ─────────────────────────────────────────────────────────────
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ── Qt6 (forced) ─────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)

# ── QCustomPlot ───────────────────────────────────────────────────────────────
set(QCP_DIR "${CMAKE_SOURCE_DIR}")
if(NOT EXISTS "${QCP_DIR}/qcustomplot.h")
    message(FATAL_ERROR
        "QCustomPlot not found.\n"
        "Download qcustomplot.h and qcustomplot.cpp from https://www.qcustomplot.com\n"
        "and place them in: ${QCP_DIR}/")
endif()

# ── Sources for surfaceViewer ─────────────────────────────────────────────────
set(VIEWER_SOURCES
    SurveyDisplay.cpp
    SurveyData.cpp
    CsvTailReader.cpp
    ColorScatter.cpp
    ScatterWidget.cpp
    ProfileWidget.cpp
    AzimuthWidget.cpp
    RaycastBoundaryWidget.cpp
    MainWindow.cpp
    ${QCP_DIR}/qcustomplot.cpp
)

# ── Executables ───────────────────────────────────────────────────────────────
add_executable(surfaceMonitoring surfaceMonitoring.cpp)

add_executable(surfaceViewer ${VIEWER_SOURCES})

# ── Compiler warnings ─────────────────────────────────────────────────────────
set(WARN_FLAGS
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

target_compile_options(surfaceMonitoring PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

target_compile_options(surfaceViewer PRIVATE ${WARN_FLAGS})

# ── Includes & linking: surfaceViewer ────────────────────────────────────────
target_include_directories(surfaceViewer PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${QCP_DIR}
)
target_link_libraries(surfaceViewer PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
)

# ── Build type default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Export compile commands for clangd / VS Code IntelliSense ────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
