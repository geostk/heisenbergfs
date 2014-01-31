cmake_minimum_required(VERSION 2.8)

set(CMAKE_C_FLAGS "-Wall -std=gnu99")
set(CMAKE_C_FLAGS_RELEASE "-Werror")

project(heisenbergfs)

find_package(PkgConfig)

pkg_check_modules(UUID REQUIRED uuid)
pkg_check_modules(FUSE REQUIRED fuse)

include_directories(
    ${UUID_LIBRARY_DIRS}
    ${FUSE_LIBRARY_DIRS}
    )

set(heisenbergfs_HEADERS
    )

set(heisenbergfs_SOURCES
    heisenbergfs.c
   )

add_executable(${PROJECT_NAME}
    ${heisenbergfs_HEADERS}
    ${heisenbergfs_SOURCES}
    )

target_link_libraries(${PROJECT_NAME}
    ${UUID_LIBRARIES}
    ${FUSE_LIBRARIES}
    )

target_compile_options(${PROJECT_NAME}
    PRIVATE
    ${UUID_CFLAGS}
    ${FUSE_CFLAGS}
    )
