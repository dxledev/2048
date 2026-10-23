cmake_minimum_required(VERSION 3.21)
project(Game2048 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)

add_library(g2048_core
    src/core/game.cpp
    src/core/storage.cpp
    src/core/color.cpp
    src/core/theme.cpp
    src/core/theme_source.cpp
    src/core/stats.cpp
)
target_include_directories(g2048_core PUBLIC src)
target_link_libraries(g2048_core PUBLIC Qt6::Core Qt6::Gui)

add_library(g2048_ui
    src/ui/chip.cpp
    src/ui/icons.cpp
    src/ui/modal.cpp
    src/ui/board.cpp
    src/ui/power_bar.cpp
    src/ui/won_overlay.cpp
    src/ui/theme_watcher.cpp
    src/ui/stats_view.cpp
    src/ui/window.cpp
)
target_link_libraries(g2048_ui PUBLIC g2048_core Qt6::Widgets)

add_executable(g2048 src/main.cpp src/cli.cpp)
set_target_properties(g2048 PROPERTIES OUTPUT_NAME 2048)
target_link_libraries(g2048 PRIVATE g2048_ui)

foreach(target g2048_core g2048_ui g2048)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(game_tests tests/game_tests.cpp)
    target_link_libraries(game_tests PRIVATE g2048_core Qt6::Test)
    add_test(NAME game COMMAND game_tests)
    add_executable(stats_tests tests/stats_tests.cpp)
    target_link_libraries(stats_tests PRIVATE g2048_core Qt6::Test)
    add_test(NAME stats COMMAND stats_tests)
    add_executable(theme_tests tests/theme_tests.cpp)
    target_link_libraries(theme_tests PRIVATE g2048_core Qt6::Test)
    add_test(NAME theme COMMAND theme_tests)
    add_executable(ui_tests tests/ui_tests.cpp)
    target_link_libraries(ui_tests PRIVATE g2048_ui Qt6::Test)
    add_test(NAME ui COMMAND ui_tests)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME cli COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.sh $<TARGET_FILE:g2048>)
    add_test(NAME install COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_test.sh
        $<TARGET_FILE_DIR:g2048> ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-desktop.sh)
endif()

include(GNUInstallDirs)
install(TARGETS g2048 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/io.github.game2048.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/io.github.game2048.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
