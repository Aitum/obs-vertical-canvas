# --- Detect if the plugin is build out of tree or not ---
if(CMAKE_PROJECT_NAME STREQUAL "obs-studio")
  set(BUILD_OUT_OF_TREE OFF)
else()
  set(BUILD_OUT_OF_TREE ON)
  cmake_minimum_required(VERSION 3.16...3.26)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${PROJECT_NAME} MODULE)

target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)

if(BUILD_OUT_OF_TREE)
  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE OBS::frontend-api)
endif()

if(BUILD_OUT_OF_TREE)
  find_qt(COMPONENTS Widgets Core)
else()
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)
target_compile_options(
${PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                -Wno-comma>)
set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_target_properties(
${PROJECT_NAME}
PROPERTIES AUTOMOC ON
            AUTOUIC ON
            AUTORCC ON)

if(BUILD_OUT_OF_TREE)
  find_package(CURL REQUIRED)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

target_sources(${PROJECT_NAME} PRIVATE
	vertical-canvas.cpp
	scenes-dock.cpp
	sources-dock.cpp
	source-tree.cpp
	transitions-dock.cpp
	qt-display.cpp
	projector.cpp
	config-dialog.cpp
	hotkey-edit.cpp
	name-dialog.cpp
	audio-wrapper-source.c
	file-updater.c
	multi-canvas-source.c
	resources.qrc
	vertical-canvas.hpp
	scenes-dock.hpp
	sources-dock.hpp
	source-tree.hpp
	transitions-dock.hpp
	qt-display.hpp
	projector.hpp
	display-helpers.hpp
	config-dialog.hpp
	hotkey-edit.hpp
	name-dialog.hpp
	audio-wrapper-source.h
	obs-websocket-api.h
	file-updater.h
	multi-canvas-source.h)

if(BUILD_OUT_OF_TREE)
	set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
else()
	set_target_properties_obs(${PROJECT_NAME} PROPERTIES FOLDER "plugins/aitum" PREFIX "")
endif()
