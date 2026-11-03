cmake_minimum_required(VERSION 3.16)
project(fnb58d C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(FindPkgConfig)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0 gio-unix-2.0)
include_directories(${GLIB_INCLUDE_DIRS})

# bluez_inc vendored at upstream weliem/bluez_inc 157c604 (see third_party/bluez_inc/VERSION)
add_subdirectory(third_party/bluez_inc/binc)

add_library(fnb58_proto STATIC src/fnb58_proto.c)
target_include_directories(fnb58_proto PUBLIC src)
target_compile_options(fnb58_proto PRIVATE -Wall -Wextra -Wconversion -Wshadow)

add_library(udp_out STATIC src/udp_out.c)
target_include_directories(udp_out PUBLIC src)
target_compile_options(udp_out PRIVATE -Wall -Wextra -Wconversion -Wshadow)

add_executable(fnb58d src/fnb58d.c)
target_link_libraries(fnb58d fnb58_proto udp_out Binc ${GLIB_LIBRARIES})
target_compile_options(fnb58d PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter)

enable_testing()
add_executable(test_proto tests/test_proto.c)
target_link_libraries(test_proto fnb58_proto m)
add_test(NAME proto COMMAND test_proto)
add_executable(test_udp_out tests/test_udp_out.c)
target_link_libraries(test_udp_out udp_out)
add_test(NAME udp_out COMMAND test_udp_out)

install(TARGETS fnb58d DESTINATION bin)
install(FILES systemd/fnb58d.service DESTINATION /etc/systemd/system)
