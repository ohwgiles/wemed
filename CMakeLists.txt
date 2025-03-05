# Copyright 2013-2022 Oliver Giles
# This file is part of Wemed. Wemed is licensed under the 
# GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
# for more information
cmake_minimum_required(VERSION 3.11)
project(wemed C)

	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Config
set(WEMED_WEBEXT_DIR "lib/wemed" CACHE STRING "Where to install and load the web extension relative to CMAKE_INSTALL_PREFIX")

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(WEBKITGTK3 REQUIRED webkit2gtk-4.0)
pkg_check_modules(GMIME3 REQUIRED gmime-3.0)
pkg_check_modules(GTKSOURCEVIEW4 REQUIRED gtksourceview-4)

# Application
include_directories(${GTK3_INCLUDE_DIRS} ${WEBKITGTK3_INCLUDE_DIRS} ${GMIME3_INCLUDE_DIRS} ${GTKSOURCEVIEW4_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS} ${WEBKITGTK3_LIBRARY_DIRS} ${GMIME3_LIBRARY_DIRS} ${GTKSOURCEVIEW4_LIBRARY_DIRS})
add_definitions(-std=gnu99 ${GTK3_CFLAGS_OTHER} ${WEBKITGTK3_CFLAGS_OTHER} ${GMIME3_CFLAGS_OTHER})
set(CMAKE_C_FLAGS "-DWEMED_WEBEXT_DIR=\\\"${CMAKE_INSTALL_PREFIX}/${WEMED_WEBEXT_DIR}\\\" ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-Wall -Wextra -Werror -Wno-error=unused -Wno-error=unused-function -Wno-unused-parameter -Wno-missing-field-initializers -Wno-error=unused-result ${CMAKE_C_FLAGS_DEBUG}")
set(sources main.c exec.c openwith.c mainwindow.c mimeapp.c mimemodel.c mimetree.c wemedpanel.c)
add_executable(wemed ${sources})
set_target_properties(wemed PROPERTIES COMPILE_DEFINITIONS "_GNU_SOURCE")
target_link_libraries(wemed ${GTK3_LIBRARIES} ${WEBKITGTK3_LIBRARIES} ${GMIME3_LIBRARIES} ${GTKSOURCEVIEW4_LIBRARIES})

# Translations
add_custom_target(build-pot
	COMMAND xgettext -LC -k_ -kN_ ${sources} -o po/wemed.pot --package-name=wemed
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	COMMENT "Building the translation template" VERBATIM)
find_package(Gettext REQUIRED)
set(po_files po/cs.po po/he.po po/de.po po/zh_CN.po)
GETTEXT_CREATE_TRANSLATIONS(${PROJECT_SOURCE_DIR}/po/wemed.pot ALL ${po_files})

# Web Extension
add_library(wemed-webext SHARED webext.c)

# Installation
install(TARGETS wemed RUNTIME DESTINATION bin)
install(TARGETS wemed-webext LIBRARY DESTINATION ${WEMED_WEBEXT_DIR})
install(FILES res/16.png DESTINATION share/icons/hicolor/16x16/apps RENAME wemed.png)
install(FILES res/22.png DESTINATION share/icons/hicolor/22x22/apps RENAME wemed.png)
install(FILES res/24.png DESTINATION share/icons/hicolor/24x24/apps RENAME wemed.png)
install(FILES res/32.png DESTINATION share/icons/hicolor/32x32/apps RENAME wemed.png)
install(FILES res/48.png DESTINATION share/icons/hicolor/48x48/apps RENAME wemed.png)
install(FILES res/64.png DESTINATION share/icons/hicolor/64x64/apps RENAME wemed.png)
install(FILES res/wemed.desktop DESTINATION share/applications)

