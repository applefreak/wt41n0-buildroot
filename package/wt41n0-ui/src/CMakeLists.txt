# wt41n0-ui: the launcher, its apps and their shared library.
#
# On the unit (Buildroot): LVGL comes from the lvgl package through pkg-config.
# Host screenshot harness:
#   cmake -S . -B build -DWT41N0_UI_HOST=ON -DLVGL_DIR=<lvgl source> -DLV_CONF_PATH=<lv_conf.h>
#   cmake --build build && build/wt41n0-ui-shots <output dir>
cmake_minimum_required(VERSION 3.13)
project(wt41n0-ui C)
set(CMAKE_C_STANDARD 11)
option(WT41N0_UI_HOST "Build the host screenshot harness instead of the unit programs" OFF)
option(WT41N0_UI_EXAMPLE "Install the example app and its manifest" OFF)

set(FONT_C ${CMAKE_CURRENT_BINARY_DIR}/ui_font_data.c)
add_custom_command(OUTPUT ${FONT_C}
	COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/fonts/ShareTechMono-Regular.ttf
		-DOUT=${FONT_C} -DNAME=ui_font_ttf -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
	DEPENDS fonts/ShareTechMono-Regular.ttf embed.cmake)

set(LIB_SOURCES lib/ui_core.c lib/ui_keys.c lib/ui_theme.c lib/ui_conf.c lib/ui_calibrate.c
	lib/ui_widgets.c ${FONT_C})
set(LAUNCHER_SOURCES launcher/home.c launcher/apps.c)
# app name -> source; each becomes /usr/bin/wt41n0-<name>, its manifest apps/<name>/<name>.app
set(APPS power)
if(WT41N0_UI_EXAMPLE OR WT41N0_UI_HOST)
	list(APPEND APPS example)
endif()

if(WT41N0_UI_HOST)
	add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl EXCLUDE_FROM_ALL)
	add_library(wt41n0ui STATIC ${LIB_SOURCES} lib/platform_host.c)
	target_link_libraries(wt41n0ui PUBLIC lvgl)
	target_compile_definitions(wt41n0ui PRIVATE WT41N0_UI_HOST)
	# every app's main() renamed to <app>_main, so one harness can render them all
	set(SHOT_SOURCES host/shots.c ${LAUNCHER_SOURCES})
	foreach(app ${APPS})
		set_source_files_properties(apps/${app}/${app}.c PROPERTIES COMPILE_DEFINITIONS main=${app}_main)
		list(APPEND SHOT_SOURCES apps/${app}/${app}.c)
	endforeach()
	add_executable(wt41n0-ui-shots ${SHOT_SOURCES})
	target_include_directories(wt41n0-ui-shots PRIVATE lib launcher)
	target_compile_definitions(wt41n0-ui-shots PRIVATE WT41N0_UI_HOST WT41N0_UI_SRC="${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(wt41n0-ui-shots wt41n0ui)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(LVGL REQUIRED IMPORTED_TARGET lvgl)
	add_library(wt41n0ui SHARED ${LIB_SOURCES} lib/platform_linux.c)
	target_compile_definitions(wt41n0ui PUBLIC LV_CONF_INCLUDE_SIMPLE)
	target_link_libraries(wt41n0ui PUBLIC PkgConfig::LVGL m)
	set_target_properties(wt41n0ui PROPERTIES VERSION 1 SOVERSION 1 PUBLIC_HEADER include/wt41n0ui.h)
	add_executable(wt41n0-launcher launcher/main.c ${LAUNCHER_SOURCES})
	target_link_libraries(wt41n0-launcher wt41n0ui)
	install(TARGETS wt41n0ui LIBRARY DESTINATION lib PUBLIC_HEADER DESTINATION include)
	install(TARGETS wt41n0-launcher RUNTIME DESTINATION bin)
	foreach(app ${APPS})
		add_executable(wt41n0-${app} apps/${app}/${app}.c)
		target_link_libraries(wt41n0-${app} wt41n0ui)
		install(TARGETS wt41n0-${app} RUNTIME DESTINATION bin)
		install(FILES apps/${app}/${app}.app DESTINATION share/wt41n0/apps)
	endforeach()
	install(FILES apps/terminal/terminal.app DESTINATION share/wt41n0/apps)
endif()
target_include_directories(wt41n0ui PUBLIC include PRIVATE lib)
target_compile_options(wt41n0ui PRIVATE -Wall -Wextra)
