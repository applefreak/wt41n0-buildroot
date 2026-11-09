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

set(LIB_SOURCES lib/ui_core.c lib/ui_frame.c lib/ui_keys.c lib/ui_theme.c lib/ui_conf.c lib/ui_calibrate.c
	lib/ui_widgets.c lib/ui_entry.c lib/ui_style.c lib/ui_icons.c lib/ui_focus.c lib/ui_rows.c lib/ui_controls.c lib/ui_boot.c ${FONT_C})
set(LAUNCHER_SOURCES launcher/home.c launcher/apps.c launcher/readout.c apps/settings/wpa.c)
# each app: apps/<name>/*.c -> /usr/bin/wt41n0-<name>, plus its manifests apps/<name>/*.app
set(APPS power settings scanner system)
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
		file(GLOB app_sources apps/${app}/*.c)
		list(REMOVE_ITEM app_sources ${CMAKE_CURRENT_SOURCE_DIR}/apps/settings/wpa.c)	# in LAUNCHER_SOURCES
		set_source_files_properties(${app_sources} PROPERTIES COMPILE_DEFINITIONS "main=${app}_main;WT41N0_UI_HOST")
		list(APPEND SHOT_SOURCES ${app_sources})
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
	pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
	foreach(app ${APPS})
		file(GLOB app_sources apps/${app}/*.c)
		file(GLOB app_manifests apps/${app}/*.app)
		add_executable(wt41n0-${app} ${app_sources})
		target_link_libraries(wt41n0-${app} wt41n0ui)
		install(TARGETS wt41n0-${app} RUNTIME DESTINATION bin)
		install(FILES ${app_manifests} DESTINATION share/wt41n0/apps)
	endforeach()
	target_link_libraries(wt41n0-settings PkgConfig::ALSA)
	target_sources(wt41n0-system PRIVATE apps/settings/wpa.c)	# the Wi-Fi state
	install(FILES apps/terminal/terminal.app DESTINATION share/wt41n0/apps)
	# the other styles' faces, loaded by TinyTTF at run time (Share Tech Mono is built in)
	install(FILES fonts/Audiowide-Regular.ttf fonts/ChakraPetch-SemiBold.ttf fonts/Aldrich-Regular.ttf
		fonts/SpaceMono-Regular.ttf fonts/SpaceMono-Bold.ttf fonts/DotGothic16-Regular.ttf
		fonts/OFL-audiowide.txt fonts/OFL-chakrapetch.txt fonts/OFL-aldrich.txt fonts/OFL-spacemono.txt
		fonts/OFL-dotgothic16.txt fonts/OFL-sharetechmono.txt DESTINATION share/wt41n0/fonts)
endif()
target_include_directories(wt41n0ui PUBLIC include PRIVATE lib)
target_compile_options(wt41n0ui PRIVATE -Wall -Wextra)
