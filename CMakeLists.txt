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
    src/opengx.h
)
set_target_properties(${TARGET} PROPERTIES
    PUBLIC_HEADER src/opengx.h
)

target_include_directories(${TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${TARGET} PUBLIC
    PkgConfig::SDL
)

configure_file(opengl.pc.in opengl.pc @ONLY)

install(TARGETS ${TARGET}
    LIBRARY
    ARCHIVE
    RUNTIME
    PUBLIC_HEADER
)
install(DIRECTORY "include/GL" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/opengl.pc
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
