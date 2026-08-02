cmake_minimum_required(VERSION 3.16)
project(rawnes_gui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network SerialPort)

include_directories(third_party)

# --- Qt-unabhaengiger Kern -------------------------------------------------
# Diese Dateien sind reines C++ (kein Qt) und lassen sich auch ohne Qt-SDK
# kompilieren/testen -- siehe tests/. Dieser Teil ist gegen die
# Python-Originale gegengeprueft.
add_library(rawnes_core STATIC
    src/Md5.cpp
    src/json.cpp
    src/ra_cache.cpp
    src/ra_client.cpp
    src/ConditionEngine.cpp
    src/RomIndex.cpp
    src/ed_protocol.cpp
    src/fpga_installer.cpp
    third_party/miniz/miniz.c
    third_party/miniz/miniz_zip.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_tdef.c
)

# --- Qt-Schicht -------------------------------------------------------------
# Alle GUI-Widgets + Netzwerk/Serial-Anbindung (QSerialPort/QNetworkAccess-
# Manager). Getrennt von rawnes_core gehalten, damit der Kern ohne Qt-SDK
# baubar/testbar bleibt (siehe oben).
add_executable(rawnes_gui
    src/main.cpp
    src/mainwindow.cpp
    src/intro_window.cpp
    src/resources.qrc
    src/ed_serial_qt.cpp
    src/ra_network.cpp
    src/badge_loader.cpp
    src/achievement_row.cpp
    src/achievement_list_widget.cpp
    src/hardcore_panel.cpp
    src/log_panel.cpp
    src/leaderboard_panel.cpp
    src/live_values_widget.cpp
    src/seven_segment.cpp
    src/fpga_installer_panel.cpp
    src/monitor_worker.cpp
)
target_link_libraries(rawnes_gui PRIVATE rawnes_core Qt6::Core Qt6::Widgets Qt6::Network Qt6::SerialPort)
set_target_properties(rawnes_gui PROPERTIES AUTOMOC ON AUTORCC ON)

# --- Tests (kein Qt noetig) --------------------------------------------------
add_executable(rawnes_tests tests/test_md5.cpp src/Md5.cpp)
add_executable(rawnes_condition_ref tests/run_cpp_reference.cpp src/ConditionEngine.cpp)
add_executable(rawnes_romindex_ref tests/run_romindex_reference.cpp src/RomIndex.cpp src/Md5.cpp
    third_party/miniz/miniz.c third_party/miniz/miniz_zip.c
    third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_tdef.c)
add_executable(rawnes_raclient_test tests/test_ra_client.cpp src/ra_client.cpp src/json.cpp
    src/ra_cache.cpp src/Md5.cpp)
add_executable(rawnes_progress_test tests/test_progress.cpp src/ConditionEngine.cpp)
add_executable(rawnes_fpga_test tests/run_fpga_installer_reference.cpp src/fpga_installer.cpp src/Md5.cpp)
