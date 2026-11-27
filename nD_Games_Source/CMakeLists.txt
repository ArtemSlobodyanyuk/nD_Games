cmake_minimum_required(VERSION 3.16)
# Опис проєкту та спільний стандарт мови для всіх його цілей збірки.
project(nD_Games LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Спільне ядро гри.
add_library(nd_chess table.cpp move.cpp)
target_include_directories(nd_chess PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# Попередження компілятора; для ядра у Visual Studio явно задаємо кодування UTF-8.
if(MSVC)
    target_compile_options(nd_chess PRIVATE /W4 /utf-8)
else()
    target_compile_options(nd_chess PRIVATE -Wall -Wextra -Wpedantic)
endif()
# Копія правил поруч зі збіркою дозволяє запускати програму з її каталогу.
configure_file(chess_rools.json chess_rools.json COPYONLY)
option(ND_BUILD_CONSOLE "Build the optional console chess application" ON)
# Проєкт можна збирати після видалення каталогу console/.
if(ND_BUILD_CONSOLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/console/main.cpp")
    add_library(nd_console_input STATIC console/command.cpp)
    target_include_directories(nd_console_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/console")
    target_link_libraries(nd_console_input PUBLIC nd_chess)
    add_executable(nd_games console/main.cpp)
    target_link_libraries(nd_games PRIVATE nd_console_input)
    set_target_properties(nd_games PROPERTIES OUTPUT_NAME nd_games_console)
    # Правила лежать поряд з exe також у багатоконфігураційних збірках Visual Studio.
    add_custom_command(TARGET nd_games POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/chess_rools.json"
            "$<TARGET_FILE_DIR:nd_games>/chess_rools.json"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(move_tests tests/move_tests.cpp)
    target_link_libraries(move_tests PRIVATE nd_chess)
    if(TARGET nd_console_input)
        target_link_libraries(move_tests PRIVATE nd_console_input)
        target_compile_definitions(move_tests PRIVATE ND_TEST_CONSOLE_INPUT)
    endif()
    add_test(NAME movement_and_input COMMAND move_tests "${CMAKE_CURRENT_BINARY_DIR}/chess_rools.json")
endif()

# Графічну версію можна вимкнути для збірки лише незалежного від Qt ядра.
option(ND_BUILD_GUI "Build the Qt graphical chess application" ON)
if(ND_BUILD_GUI)
    # Локальне встановлення Qt не змінює системний PATH.
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.deps/Qt/6.8.3/msvc2022_64")
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Svg)
    add_library(nd_gui gui.cpp gui.h)
    set_target_properties(nd_gui PROPERTIES AUTOMOC ON)
    target_link_libraries(nd_gui PUBLIC nd_chess Qt6::Widgets Qt6::Svg)
    if(MSVC)
        target_compile_options(nd_gui PRIVATE /W4 /utf-8)
    endif()
    file(GLOB CHESS_SPRITES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "assets/pieces/*.svg")
    qt_add_resources(nd_gui game_assets PREFIX "/" FILES
        assets/crown-logo.png assets/elemental.png ${CHESS_SPRITES})
    add_executable(nd_games_gui WIN32 gui_main.cpp)
    target_link_libraries(nd_games_gui PRIVATE nd_gui)
    if(MSVC)
        target_compile_options(nd_games_gui PRIVATE /W4 /utf-8)
    endif()
    add_custom_command(TARGET nd_games_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/chess_rools.json"
            "$<TARGET_FILE_DIR:nd_games_gui>/chess_rools.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/CREDITS.md"
            "$<TARGET_FILE_DIR:nd_games_gui>/THIRD_PARTY_ASSETS.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/pieces/LICENSE.txt"
            "$<TARGET_FILE_DIR:nd_games_gui>/LICENSE-CHESSNUT.txt"
        VERBATIM)
    set(GUI_DEPLOY_TARGETS nd_games_gui)
    if(BUILD_TESTING)
        find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
        add_executable(gui_tests tests/gui_tests.cpp)
        target_link_libraries(gui_tests PRIVATE nd_gui Qt6::Test)
        if(MSVC)
            target_compile_options(gui_tests PRIVATE /W4 /utf-8)
        endif()
        add_test(NAME graphical_interface COMMAND gui_tests "${CMAKE_CURRENT_SOURCE_DIR}/chess_rools.json")
        set_tests_properties(graphical_interface PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        list(APPEND GUI_DEPLOY_TARGETS gui_tests)
    endif()
    if(WIN32)
        get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(QT_BIN_DIRECTORY "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
        if(BUILD_TESTING)
            set_property(TEST graphical_interface APPEND PROPERTY ENVIRONMENT
                "QT_PLUGIN_PATH=${QT_BIN_DIRECTORY}/../plugins")
        endif()
        find_program(QT_WINDEPLOYQT windeployqt HINTS "${QT_BIN_DIRECTORY}" REQUIRED)
        foreach(APP_TARGET IN LISTS GUI_DEPLOY_TARGETS)
            # DLL і платформний плагін Qt копіюються поряд з exe автоматично.
            add_custom_command(TARGET ${APP_TARGET} POST_BUILD
                COMMAND "${QT_WINDEPLOYQT}" --no-translations --no-opengl-sw
                    --no-system-d3d-compiler "$<TARGET_FILE:${APP_TARGET}>"
                VERBATIM)
        endforeach()
    endif()
endif()
