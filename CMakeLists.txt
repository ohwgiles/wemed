cmake_minimum_required(VERSION 2.6)
# WEMED : WEbkit Mime EDitor
project(wemed C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(WEBKIT2GTK3 REQUIRED webkit2gtk-3.0)
pkg_check_modules(GMIME26 REQUIRED gmime-2.6)

include_directories(${GTK3_INCLUDE_DIRS} ${WEBKIT2GTK3_INCLUDE_DIRS} ${GMIME26_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS} ${WEBKIT2GTK3_LIBRARY_DIRS} ${GMIME26_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER} ${WEBKIT2GTK3_CFLAGS_OTHER} ${GMIME26_CFLAGS_OTHER})

add_definitions(-std=gnu99 -Wall -Wextra)

add_executable(wemed main.c openwith.c parsemime.c mimeapp.c wemedpanel.c)
target_link_libraries(wemed ${GTK3_LIBRARIES} ${WEBKIT2GTK3_LIBRARIES} ${GMIME26_LIBRARIES}) 

