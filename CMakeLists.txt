# Copyright 2013 Oliver Giles
# This file is part of Wemed. Wemed is licensed under the 
# GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
# for more information
cmake_minimum_required(VERSION 2.6)
project(wemed C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(WEBKITGTK3 REQUIRED webkitgtk-3.0)
pkg_check_modules(GMIME26 REQUIRED gmime-2.6)

include_directories(${GTK3_INCLUDE_DIRS} ${WEBKITGTK3_INCLUDE_DIRS} ${GMIME26_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS} ${WEBKITGTK3_LIBRARY_DIRS} ${GMIME26_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER} ${WEBKITGTK3_CFLAGS_OTHER} ${GMIME26_CFLAGS_OTHER})

add_definitions(-std=gnu99 -Wall -Wextra -Werror -Wno-error=unused -Wno-unused-parameter)# -Wno-missing-field-initializers)

add_executable(wemed main.c openwith.c mainwindow.c mimeapp.c mimemodel.c mimetree.c wemedpanel.c)
target_link_libraries(wemed ${GTK3_LIBRARIES} ${WEBKITGTK3_LIBRARIES} ${GMIME26_LIBRARIES}) 

install(TARGETS wemed RUNTIME DESTINATION bin)
install(FILES res/16.png DESTINATION share/icons/hicolor/16x16/apps RENAME wemed.png)
install(FILES res/22.png DESTINATION share/icons/hicolor/22x22/apps RENAME wemed.png)
install(FILES res/24.png DESTINATION share/icons/hicolor/24x24/apps RENAME wemed.png)
install(FILES res/32.png DESTINATION share/icons/hicolor/32x32/apps RENAME wemed.png)
install(FILES res/48.png DESTINATION share/icons/hicolor/48x48/apps RENAME wemed.png)
install(FILES res/64.png DESTINATION share/icons/hicolor/64x64/apps RENAME wemed.png)
install(FILES res/wemed.desktop DESTINATION share/applications)

