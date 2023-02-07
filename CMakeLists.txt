cmake_minimum_required(VERSION 3.5)

project(DatabaseCreator VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Widgets Concurrent LinguistTools REQUIRED)

find_package(vsg REQUIRED)
find_package(vsgXchange REQUIRED)

set(TS_FILES DatabaseCreator_ru_RU.ts)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        ${TS_FILES}
)

qt_add_executable(DatabaseCreator MANUAL_FINALIZATION ${PROJECT_SOURCES})

qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})

target_link_libraries(DatabaseCreator PRIVATE Qt6::Widgets Qt6::Concurrent vsg::vsg vsgXchange::vsgXchange)

if (TARGET objects)
    target_compile_definitions(DatabaseCreator PRIVATE tiles_FOUND)
    target_link_libraries(DatabaseCreator PRIVATE objects)
endif()

set_target_properties(DatabaseCreator PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

qt_finalize_executable(DatabaseCreator)

