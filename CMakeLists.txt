cmake_minimum_required(VERSION 3.13)
project(OpenGX VERSION 0.1)

set(CMAKE_C_STANDARD 11)

set(TARGET opengx)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl2)

add_library(${TARGET} STATIC
    src/gc_gl.c
    src/image_DXT.c
    src/image_DXT.h
)

target_include_directories(${TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${TARGET} PUBLIC
    PkgConfig::SDL
)

install(TARGETS ${TARGET}
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
