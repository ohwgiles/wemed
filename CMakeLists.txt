# Copyright 2013 Oliver Giles
# This file is part of Wemed. Wemed is licensed under the 
# GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
# for more information
cmake_minimum_required(VERSION 2.6)
project(wemed C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(WEBKIT2GTK3 REQUIRED webkit2gtk-3.0)
pkg_check_modules(GMIME26 REQUIRED gmime-2.6)

include_directories(${GTK3_INCLUDE_DIRS} ${WEBKIT2GTK3_INCLUDE_DIRS} ${GMIME26_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS} ${WEBKIT2GTK3_LIBRARY_DIRS} ${GMIME26_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER} ${WEBKIT2GTK3_CFLAGS_OTHER} ${GMIME26_CFLAGS_OTHER})

add_definitions(-std=gnu99 -Wall -Wextra -Werror -Wno-error=unused-parameter -Wno-error=unused-function -Wno-error=unused-variable)

add_executable(wemed main.c openwith.c mainwindow.c mimeapp.c mimemodel.c wemedpanel.c)
target_link_libraries(wemed ${GTK3_LIBRARIES} ${WEBKIT2GTK3_LIBRARIES} ${GMIME26_LIBRARIES}) 

