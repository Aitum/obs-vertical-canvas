# --- Detect if the plugin is build out of tree or not ---
if(CMAKE_PROJECT_NAME STREQUAL "obs-studio")
  set(BUILD_OUT_OF_TREE OFF)
  if(OBS_CMAKE_VERSION VERSION_GREATER_EQUAL 3.0.0)
    legacy_check()
  endif()
else()
  set(BUILD_OUT_OF_TREE ON)
  cmake_minimum_required(VERSION 3.18)
endif()

project(vertical-canvas VERSION 1.4.4)
set(PROJECT_FULL_NAME "Vertical Canvas")

# Set new UUIDs when you start to create a new plugin.
set(MACOS_PACKAGE_UUID "EAFC919A-0DBE-401D-BF08-0312294B060A")
set(MACOS_INSTALLER_UUID "7CFAF0A9-B28B-483F-B671-FF60A826C259")

add_library(${PROJECT_NAME} MODULE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

set(CMAKE_AUTORCC ON)

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
  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)
  find_package(CURL REQUIRED)
  include(cmake/ObsPluginHelpers.cmake)
  find_qt(COMPONENTS Widgets COMPONENTS_LINUX Gui)
  set(OBS_FRONTEND_API_NAME "obs-frontend-api")
else()
	find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
	set(OBS_FRONTEND_API_NAME "frontend-api")
endif()

if(OS_WINDOWS)
	get_filename_component(ISS_FILES_DIR "${CMAKE_BINARY_DIR}\\..\\package" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_FILES_DIR}" ISS_FILES_DIR)
	get_filename_component(ISS_PACKAGE_DIR "${CMAKE_PACKAGE_PREFIX}\\.." ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_PACKAGE_DIR}" ISS_PACKAGE_DIR)
	get_filename_component(ISS_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_SOURCE_DIR}" ISS_SOURCE_DIR)
	configure_file("installer.iss.in"
			"${PROJECT_BINARY_DIR}/installer.iss"
		)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/resource.rc.in ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.rc)
	target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.rc)
	set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(OS_MACOS)
	set(MACOS_BUNDLEID "tv.aitum.${PROJECT_NAME}")
	set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
	set(MACOSX_PLUGIN_BUNDLE_VERSION "${PROJECT_VERSION}")
	set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")
	configure_file(cmake/bundle/macos/installer-macos.pkgproj.in ${CMAKE_BINARY_DIR}/installer-macos.generated.pkgproj)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall)
elseif(OS_POSIX)
	target_link_libraries(${PROJECT_NAME} Qt::GuiPrivate)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

target_link_libraries(${PROJECT_NAME}
		OBS::${OBS_FRONTEND_API_NAME}
		CURL::libcurl
		Qt::Widgets
		OBS::libobs)

if(BUILD_OUT_OF_TREE)
    if(NOT LIB_OUT_DIR)
        set(LIB_OUT_DIR "/lib/obs-plugins")
    endif()
    if(NOT DATA_OUT_DIR)
        set(DATA_OUT_DIR "/share/obs/obs-plugins/${PROJECT_NAME}")
    endif()
    set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
    install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${LIB_OUT_DIR})
    install(DIRECTORY data/locale data/images
        DESTINATION ${CMAKE_INSTALL_PREFIX}/${DATA_OUT_DIR})
    setup_plugin_target(${PROJECT_NAME})	
else()
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/UI/obs-frontend-api")
    if(OBS_CMAKE_VERSION VERSION_GREATER_EQUAL 3.0.0)
        set_target_properties_obs(${PROJECT_NAME} PROPERTIES FOLDER "plugins/aitum" PREFIX "")
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "plugins/aitum")
        setup_plugin_target(${PROJECT_NAME})
    endif()
endif()
