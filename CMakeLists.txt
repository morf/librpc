cmake_minimum_required(VERSION 3.0.2)
project(librpc C)
find_package(PkgConfig)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Debug' as none was specified.")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

pkg_check_modules(PC_LIBEVENT REQUIRED libevent)

set(SOURCES
    src/rpc.c
)

add_library(rpc_obj OBJECT ${SOURCES})
target_compile_definitions(rpc_obj PUBLIC "-DGNU_SOURCE -D_POSIX_C_SOURCE=200809L")
set_target_properties(rpc_obj PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
)

target_include_directories(rpc_obj
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_options(rpc_obj PUBLIC -fPIC -Wall -Wextra)
target_compile_options(rpc_obj PUBLIC $<$<CONFIG:DEBUG>:-g3>)
target_compile_options(rpc_obj PUBLIC $<$<CONFIG:RELEASE>:-g3 -O3>)
target_compile_options(rpc_obj PUBLIC
    $<$<CONFIG:PACKAGE>:-g3 -O2 -fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/src=/usr/src/librpc>)

add_library(rpc_static STATIC $<TARGET_OBJECTS:rpc_obj>)
target_link_libraries(rpc_static ${PC_LIBCONFIG_LIBEVENT})

add_library(rpc SHARED $<TARGET_OBJECTS:rpc_obj>)
target_link_libraries(rpc ${PC_LIBCONFIG_LIBEVENT})

target_include_directories(rpc_static PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(rpc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

install(FILES include/rpc.h
    DESTINATION include
)

install(DIRECTORY src/
    DESTINATION /usr/src/librpc
)

install(TARGETS rpc rpc_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/CPackOptions.cmake.in
    ${CMAKE_BINARY_DIR}/CPackOptions.cmake
    @ONLY)

set(CPACK_GENERATOR "DEB")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_BINARY_DIR}/CPackOptions.cmake")
include(CPack)
