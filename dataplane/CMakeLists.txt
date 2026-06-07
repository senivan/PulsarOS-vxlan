cmake_minimum_required(VERSION 3.16)
project(pulsaros-vxlan-dataplane C)

set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

add_library(vxlan-dpdk STATIC
  src/arp.c
  src/config.c
  src/dpdk_port.c
  src/neigh_t.c
)

target_include_directories(vxlan-dpdk PUBLIC
  include
  third_party/jsmn
  ${DPDK_INCLUDE_DIRS}
)

target_link_libraries(vxlan-dpdk PUBLIC
  ${DPDK_LIBRARIES}
)

target_compile_options(vxlan-dpdk PRIVATE
  ${DPDK_CFLAGS_OTHER}
)
